#pragma once

#include "includes.h"

namespace hv::stealth
{
	/// @brief VMX MSR entry structure; 26.7.2 VM-Exit Controls for MSRs
	struct vmx_msr_entry
	{
		uint32_t msr_index;
		uint32_t reserved;
		uint64_t msr_data;
	};

	/// @brief Maximum number of MSR entries that can be used for stealth purposes
	/// MPERF + APERF + IA32_TIME_STAMP_COUNTER = 3
	constexpr size_t max_msr_entries = 3;

	/// @brief tsc_data structure synchronized with the one in vmexit_stub.asm. Make sure all changes to this structure are reflected in the assembly code as well.
	struct tsc_data
	{
		// Current TSC offset
		int64_t tsc_offset;

		// IA32_TIME_STAMP_COUNTER value from msr_entries at VMEXIT
		uint64_t stored_tsc;

		// Overhead of VMX transitions starting from the exiting instruction up until storing the TSC
		size_t vmexit_to_store_overhead;

		// The time it takes to execute the native instruction
		size_t instruction_overhead;

		// Overhead of just the operations after VMRESUME measured by bench_tsc_overhead
		size_t vmentry_overhead;
	};

	struct stealth_data
	{
		alignas(16) vmx_msr_entry msr_entries[max_msr_entries];

		size_t msr_store_count;

		size_t msr_load_count;

		tsc_data tsc_data;

		volatile bool bench_completed;
	};

	/// @brief Prepares the MSR store/load entries
	/// @param data The stealth data structure to populate with MSR entries
	/// @return True if the preparation was successful, false otherwise
	void prepare_msr_entries(stealth_data& data);

	/// @brief Benchmarks the TSC overhead of VMX transitions and updates the minimum overhead in the stealth data
	/// @param data The stealth data structure to update with the minimum TSC overhead
	void bench_tsc_overhead(stealth_data& data);

	/// @brief Called on each VMEXIT to update the stored TSC value in the stealth data and reset instruction overhead
	/// @param data The stealth data structure to update with the current TSC value
	void on_vmexit(stealth_data& data);
}  // namespace hv::stealth