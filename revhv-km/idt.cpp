#include "idt.h"
#include "gdt.h"
#include "vcpu.h"

namespace hv::idt
{
	static void define_gate(segment_descriptor_interrupt_gate_64* idt, uint8_t interrupt_number, const void* handler, uint8_t ist_index = 0, bool trap_gate = false)
	{
		//
		// 7 Interrupt and Exception Handling & 7.14.1 64-Bit Mode IDT
		//

		auto* gate = &idt[interrupt_number];

		gate->offset_low = reinterpret_cast<uint64_t>(handler) & 0xFFFF;
		gate->offset_middle = (reinterpret_cast<uint64_t>(handler) >> 16) & 0xFFFF;
		gate->offset_high = (reinterpret_cast<uint64_t>(handler) >> 32);

		gate->interrupt_stack_table = ist_index;
		gate->type = trap_gate ? SEGMENT_DESCRIPTOR_TYPE_TRAP_GATE : SEGMENT_DESCRIPTOR_TYPE_INTERRUPT_GATE;
		gate->present = 1;
		gate->segment_selector = gdt::host_cs.flags;
		gate->descriptor_privilege_level = 0;
	}

	void initialize(segment_descriptor_interrupt_gate_64* idt)
	{
		memset(idt, 0, sizeof(segment_descriptor_interrupt_gate_64) * idt_gate_count);

		// 7.15 Exception and Interrupt Reference

		// A trap gate for #DB and #BP is used to support debugging interrupt paths in some way in the future

		define_gate(idt, divide_error, isr::isr_0);
		define_gate(idt, debug, isr::isr_1, 0, true);			 // We use a trap gate for #DB
		define_gate(idt, nmi, isr::isr_2, vcpu::ist_index_nmi);	 // We use an IST for NMI
		define_gate(idt, breakpoint, isr::isr_3, 0, true);		 // We use a trap gate for #BP
		define_gate(idt, overflow, isr::isr_4);
		define_gate(idt, bound_range_exceeded, isr::isr_5);
		define_gate(idt, invalid_opcode, isr::isr_6);
		define_gate(idt, device_not_available, isr::isr_7);
		define_gate(idt, double_fault, isr::isr_8, vcpu::ist_index_df);	 // We use an IST for Double Fault
		// coprocessor_segment_overrun is reserved therefore not defined
		define_gate(idt, invalid_tss, isr::isr_10);
		define_gate(idt, segment_not_present, isr::isr_11);
		define_gate(idt, stack_segment_fault, isr::isr_12);	 // a.k.a stack fault exception
		define_gate(idt, general_protection, isr::isr_13);
		define_gate(idt, page_fault, isr::isr_14);
		define_gate(idt, x87_floating_point_error, isr::isr_16);
		define_gate(idt, alignment_check, isr::isr_17);
		define_gate(idt, machine_check, isr::isr_18, vcpu::ist_index_mc);  // We use an IST for Machine Check
		define_gate(idt, simd_floating_point_error, isr::isr_19);
		define_gate(idt, virtualization_exception, isr::isr_20);
		define_gate(idt, control_protection, isr::isr_21);
		// 32-255 are user-defined interrupts therefore not defined
	}
}  // namespace hv::idt