#include "apic.h"

namespace apic
{
	constexpr uint64_t ICR_DELIVERY_NMI = 0b100 << 8;				   // bits 10:8
	constexpr uint64_t ICR_DEST_SHORTHAND_ALL_EXCL_SELF = 0b11 << 18;  // bits 20:18
	constexpr uint64_t ICR_LEVEL_ASSERT = 1u << 14;					   // bit 14
	constexpr uint64_t ICR_DELIVERY_STATUS = 1u << 12;				   // bit 12 (xAPIC ICR low read)
	constexpr auto LAPIC_ICR_LOW = APIC_INTERRUPT_COMMAND_BITS_0_31;
	constexpr auto LAPIC_ICR_HIGH = APIC_INTERRUPT_COMMAND_BITS_32_63;

	// Helper function for xAPIC MMIO access, assumes lapic_mmio is UC memory
	static inline uint32_t lapic_mmio_read32(volatile uint8_t* lapic_mmio, uint32_t off)
	{
		volatile uint32_t* reg = (volatile uint32_t*)(lapic_mmio + off);
		uint32_t v = *reg;
		_ReadWriteBarrier();
		return v;
	}

	// Helper function for xAPIC MMIO access, assumes lapic_mmio is UC memory
	static inline void lapic_mmio_write32(volatile uint8_t* lapic_mmio, uint32_t off, uint32_t v)
	{
		volatile uint32_t* reg = (volatile uint32_t*)(lapic_mmio + off);
		_ReadWriteBarrier();
		*reg = v;
		_ReadWriteBarrier();
	}

	bool is_x2apic_enabled()
	{
		//
		// This function assumes local apic is already enabled and only checks for x2apic support and enablement
		//

		cpuid_eax_01 result = {};
		__cpuid(reinterpret_cast<int*>(&result), 0x1);

		if (!result.cpuid_feature_information_ecx.x2apic_support)
		{
			return false;
		}

		ia32_apic_base_register apic_base = {};
		apic_base.flags = __readmsr(IA32_APIC_BASE);

		if (!apic_base.enable_x2apic_mode)
		{
			return false;
		}

		return true;
	}

	void x2apic_send_nmi_all_others()
	{
		// 12 APIC & 12.12 EXTENDED XAPIC (X2APIC)
		uint64_t icr = (ICR_DELIVERY_NMI | ICR_LEVEL_ASSERT | ICR_DEST_SHORTHAND_ALL_EXCL_SELF);
		__writemsr(IA32_X2APIC_ICR, icr);
	}

	void xapic_send_nmi_all_others(volatile uint8_t* lapic_mmio)
	{
		// 12 APIC

		// Wait until ICR is idle (Delivery Status == 0).
		// (Delivery status is bit 12 in the ICR low register when read back.)
		for (int i = 0; i < 100000; i++)
		{
			uint32_t icr_low = lapic_mmio_read32(lapic_mmio, LAPIC_ICR_LOW);
			if ((icr_low & ICR_DELIVERY_STATUS) == 0)
				break;

			_mm_pause();
		}

		// With destination shorthand "all excluding self", ICR high is ignored, regardless write 0
		lapic_mmio_write32(lapic_mmio, LAPIC_ICR_HIGH, 0);

		uint32_t icr_low = (static_cast<uint32_t>(ICR_DELIVERY_NMI) | static_cast<uint32_t>(ICR_LEVEL_ASSERT) | static_cast<uint32_t>(ICR_DEST_SHORTHAND_ALL_EXCL_SELF));

		lapic_mmio_write32(lapic_mmio, LAPIC_ICR_LOW, icr_low);

		// No need to wait for completion
	}

	volatile uint8_t* get_lapic_mmio_phys_base()
	{
		ia32_apic_base_register apic_base = {};
		apic_base.flags = __readmsr(IA32_APIC_BASE);

		uint64_t lapic_phys_base = apic_base.apic_base << 12;  // bits 47:12

		return reinterpret_cast<volatile uint8_t*>(lapic_phys_base);
	}
}  // namespace apic