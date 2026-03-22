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

		vcpu::devirtualize(vcpu);

		utils::switch_host_cs(vcpu->restore_context.cs.flags);

		// Acknowledge 1 for this core
		sync::atomic_increment(hv::g_hv.crash_ack_core_count);

		// Send NMI to all other cores to notify them of the crash

		// Check if x2APIC is enabled
		if (apic::is_x2apic_enabled())
		{
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
				// Try to bugcheck anyways, we're hopeless at this point
				KeBugCheckEx(MANUALLY_INITIATED_CRASH, 'rvhv', 0xDEADDEAD, reinterpret_cast<ULONG_PTR>(hv::g_hv.logger.messages), vcpu->core_id);
			}

			apic::xapic_send_nmi_all_others(lapic_mmio_mapped);
		}

		// Busy wait for all other cores to acknowledge the crash
		size_t expected_ack_count = hv::g_hv.vcpu_count;
		while (sync::atomic_load(hv::g_hv.crash_ack_core_count) < expected_ack_count)
		{
			_mm_pause();
		}

		KeBugCheckEx(MANUALLY_INITIATED_CRASH, 'rvhv', 0xDEADDEAD, reinterpret_cast<ULONG_PTR>(hv::g_hv.logger.messages), vcpu->core_id);
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