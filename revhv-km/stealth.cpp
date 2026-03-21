#include "stealth.h"
#include "utils.hpp"

namespace hv::stealth
{
	extern "C" uint64_t stealth_vmexit_bench_one(uint64_t* vmentry_overhead);

	static void add_msr_entry(stealth_data& data, uint32_t msr, uint32_t entry_idx)
	{
		auto& entry = data.msr_entries[entry_idx];
		entry.msr_index = msr;
		entry.reserved = 0;
		entry.msr_data = __readmsr(msr);
	}

	void prepare_msr_entries(stealth_data& data)
	{
		utils::memset(&data, 0, sizeof(data));

		auto& msr_entries = data.msr_entries;

		add_msr_entry(data, IA32_MPERF, 0);
		add_msr_entry(data, IA32_APERF, 1);

		// This must be the last entry to avoid loading it on vmentry
		add_msr_entry(data, IA32_TIME_STAMP_COUNTER, 2);

		data.msr_store_count = max_msr_entries;
		data.msr_load_count = max_msr_entries - 1;	// Don't load TSC, we use TSC offset instead

		//
		// For other performance counters detailed in Chapter 21
		//
		// Since we load/store PERF_GLOBAL_CTRL and have all PMCs disabled on vmx-root, we don't load/store additional PMCs or PERFEVTSELx MSRs,
		// as they only count until the host PERF_GLOBAL_CTRL is loaded by the CPU. This means that the only timing overhead they can record is the CPU time
		// itself spent on doing the VMEXIT, not the time spent by our handler. This is still noticable, but for the sake of simplicity we just don't load/store them at all.
		//
	}

	void bench_tsc_overhead(stealth_data& data)
	{
		constexpr size_t warmup_iterations = 100;
		constexpr size_t iterations = 10000;

		// CLI
		_disable();

		uint64_t vmentry_overhead = 0;

		// Do the warmup iterations
		for (size_t i = 0; i < warmup_iterations; i++)
		{
			stealth_vmexit_bench_one(&vmentry_overhead);
		}

		// Actual benchmark iterations
		uint64_t min_vmexit_to_store_overhead = 0;
		uint64_t min_vmentry_overhead = 0;
		for (size_t i = 0; i < iterations; i++)
		{
			auto pre_exit_tsc = stealth_vmexit_bench_one(&vmentry_overhead);
			auto diff = static_cast<int64_t>(data.msr_entries[max_msr_entries - 1].msr_data) - pre_exit_tsc;

			if (diff < min_vmexit_to_store_overhead || min_vmexit_to_store_overhead == 0)
			{
				min_vmexit_to_store_overhead = diff;
			}

			if (vmentry_overhead < min_vmentry_overhead || min_vmentry_overhead == 0)
			{
				min_vmentry_overhead = vmentry_overhead;
			}
		}

		data.tsc_data.vmexit_to_store_overhead = min_vmexit_to_store_overhead;
		data.tsc_data.vmentry_overhead = min_vmentry_overhead;
		data.bench_completed = true;

		// STI
		_enable();

		LOG_INFO("Measured overheads : VMEXIT: %llu, VMENTRY: %llu", data.tsc_data.vmexit_to_store_overhead, data.tsc_data.vmentry_overhead);
	}

	void on_vmexit(stealth_data& data)
	{
		data.tsc_data.stored_tsc = static_cast<int64_t>(data.msr_entries[max_msr_entries - 1].msr_data);  // IA32_TIME_STAMP_COUNTER is the last entry

		data.tsc_data.instruction_overhead = 0;
	}
}  // namespace hv::stealth