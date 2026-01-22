#pragma once
#include "includes.h"

namespace apic
{
	/// @brief Checks if x2APIC mode is enabled
	/// @return true if x2APIC mode is enabled, false otherwise
	bool is_x2apic_enabled();

	/// @brief Sends an NMI to all other processors using x2APIC
	/// @note Assumes x2APIC is enabled
	void x2apic_send_nmi_all_others();

	/// @brief Sends an NMI to all other processors using xAPIC MMIO
	/// @param lapic_mmio Pointer to the local APIC MMIO base (MUST be UC memory to avoid caching issues)
	void xapic_send_nmi_all_others(volatile uint8_t* lapic_mmio);

	/// @brief Gets the local APIC MMIO base address for xAPIC
	/// @return Pointer to the local APIC MMIO PHYSICAL base
	volatile uint8_t* get_lapic_mmio_phys_base();
}  // namespace apic
