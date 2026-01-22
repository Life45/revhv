#include "error.h"
#include "hv.h"
#include "apic.h"

namespace hv::error
{
	static bool raise_host_crash(vcpu::vcpu* vcpu)
	{
		// Check if a crash is already in progress
		if (sync::atomic_compare_exchange(hv::g_hv.crash_in_progress, 1, 0) == 1)
		{
			// Another core has already initiated the crash
			return false;
		}

		LOG_ERROR("Raising host crash from vCPU %lu", vcpu->core_id);
		vcpu::devirtualize(vcpu);

		// Acknowledge 1 for this core
		sync::atomic_increment(hv::g_hv.crash_ack_core_count);

		// Send NMI to all other cores to notify them of the crash
		LOG_INFO("Sending NMIs to all other processors");

		// Check if x2APIC is enabled
		if (apic::is_x2apic_enabled())
		{
			LOG_INFO("Sending NMIs via x2APIC");
			apic::x2apic_send_nmi_all_others();
		}
		else
		{
			volatile uint8_t* lapic_mmio = apic::get_lapic_mmio_phys_base();
			// Since we're devirtualized, use MmMapIoSpace to map the LAPIC MMIO region
			PHYSICAL_ADDRESS lapic_phys_addr = {};
			lapic_phys_addr.QuadPart = reinterpret_cast<uint64_t>(lapic_mmio);
			volatile uint8_t* lapic_mmio_mapped = reinterpret_cast<volatile uint8_t*>(MmMapIoSpace(lapic_phys_addr, 0x1000, MmNonCached));

			if (!lapic_mmio_mapped)
			{
				LOG_ERROR("Failed to map LAPIC MMIO space for sending NMIs");
				// Halt since we cannot notify other cores
				__halt();
			}

			LOG_INFO("Sending NMIs via xAPIC MMIO");
			apic::xapic_send_nmi_all_others(lapic_mmio_mapped);
		}

		// Busy wait for all other cores to acknowledge the crash
		size_t expected_ack_count = hv::g_hv.vcpu_count;
		while (sync::atomic_load(hv::g_hv.crash_ack_core_count) < expected_ack_count)
		{
			_mm_pause();
		}

		LOG_INFO("All cores have acknowledged the crash. Triggering bugcheck.");
		KeBugCheckEx(MANUALLY_INITIATED_CRASH, 'rvhv', 0xDEADDEAD, 0, vcpu->core_id);
	}

	void __declspec(noreturn) unrecoverable_host_error(vcpu::vcpu* vcpu)
	{
		if (!raise_host_crash(vcpu))
		{
			// Another core has already initiated the crash, just devirtualize and wait
			give_control_on_unrecoverable_error(vcpu);
		}
	}

	void give_control_on_unrecoverable_error(vcpu::vcpu* vcpu)
	{
		if (sync::atomic_load(hv::g_hv.crash_in_progress) != 0)
		{
			vcpu::devirtualize(vcpu);

			// Acknowledge the crash
			sync::atomic_increment(hv::g_hv.crash_ack_core_count);

			// Unblock NMI with IRETQ and busy wait with interrupts enabled
			utils::cpu_hang_unblock_nmi(vcpu->restore_context.cs.flags);
		}
	}
}  // namespace hv::error