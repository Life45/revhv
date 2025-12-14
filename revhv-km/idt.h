#pragma once
#include "includes.h"
#include "isr.h"

namespace hv::idt
{
	/// @brief Number of interrupt gates in the IDT (present or not, architectural limit)
	// @note: See SDM Volume 3A, 7.2 EXCEPTION AND INTERRUPT VECTORS
	constexpr size_t idt_gate_count = 256;

	void initialize(segment_descriptor_interrupt_gate_64* idt);
}  // namespace hv::idt