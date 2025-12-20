#pragma once
#include "includes.h"
#include "gdt.h"
#include "idt.h"
#include "exception.h"

namespace hv::vcpu
{
	constexpr size_t host_stack_size = 0x8000;	// 32 KB
	constexpr size_t ist_size = 0x4000;			// 16 KB
	constexpr int ist_index_nmi = 1;
	constexpr int ist_index_df = 2;
	constexpr int ist_index_mc = 3;

	// TODO: Move to a separate header
	struct alignas(16) guest_context
	{
		// general purpose registers
		union
		{
			uint64_t rax;
			uint32_t eax;
			uint16_t ax;
			uint8_t al;
		};
		union
		{
			uint64_t rcx;
			uint32_t ecx;
			uint16_t cx;
			uint8_t cl;
		};
		union
		{
			uint64_t rdx;
			uint32_t edx;
			uint16_t dx;
			uint8_t dl;
		};
		union
		{
			uint64_t rbx;
			uint32_t ebx;
			uint16_t bx;
			uint8_t bl;
		};
		union
		{
			uint64_t rbp;
			uint32_t ebp;
			uint16_t bp;
			uint8_t bpl;
		};
		union
		{
			uint64_t rsi;
			uint32_t esi;
			uint16_t si;
			uint8_t sil;
		};
		union
		{
			uint64_t rdi;
			uint32_t edi;
			uint16_t di;
			uint8_t dil;
		};
		union
		{
			uint64_t r8;
			uint32_t r8d;
			uint16_t r8w;
			uint8_t r8b;
		};
		union
		{
			uint64_t r9;
			uint32_t r9d;
			uint16_t r9w;
			uint8_t r9b;
		};
		union
		{
			uint64_t r10;
			uint32_t r10d;
			uint16_t r10w;
			uint8_t r10b;
		};
		union
		{
			uint64_t r11;
			uint32_t r11d;
			uint16_t r11w;
			uint8_t r11b;
		};
		union
		{
			uint64_t r12;
			uint32_t r12d;
			uint16_t r12w;
			uint8_t r12b;
		};
		union
		{
			uint64_t r13;
			uint32_t r13d;
			uint16_t r13w;
			uint8_t r13b;
		};
		union
		{
			uint64_t r14;
			uint32_t r14d;
			uint16_t r14w;
			uint8_t r14b;
		};
		union
		{
			uint64_t r15;
			uint32_t r15d;
			uint16_t r15w;
			uint8_t r15b;
		};

		// control registers
		uint64_t cr2;
		uint64_t cr8;

		// debug registers
		uint64_t dr0;
		uint64_t dr1;
		uint64_t dr2;
		uint64_t dr3;
		uint64_t dr6;

		// xmm registers
		M128A xmm0;
		M128A xmm1;
		M128A xmm2;
		M128A xmm3;
		M128A xmm4;
		M128A xmm5;
		M128A xmm6;
		M128A xmm7;
		M128A xmm8;
		M128A xmm9;
		M128A xmm10;
		M128A xmm11;
		M128A xmm12;
		M128A xmm13;
		M128A xmm14;
		M128A xmm15;

		// mxcsr register
		uint32_t mxcsr;
		// explicit 12 byte padding
		uint32_t _pad[3];
	};

	struct vcpu
	{
		alignas(0x1000) uint8_t host_stack[host_stack_size];

		alignas(0x1000) task_state_segment_64 host_tss;

		alignas(0x1000) segment_descriptor_32 host_gdt[gdt::host_descriptor_count];

		alignas(0x1000) segment_descriptor_interrupt_gate_64 host_idt[idt::idt_gate_count];

		// IST(1) for NMI
		alignas(0x1000) uint8_t host_ist1_nmi[ist_size];

		// IST(2) for Double Fault
		alignas(0x1000) uint8_t host_ist2_df[ist_size];

		// IST(3) for Machine Check
		alignas(0x1000) uint8_t host_ist3_mc[ist_size];

		alignas(0x1000) vmxon vmxon_region;

		alignas(0x1000)::vmcs vmcs_region;

		alignas(0x1000) vmx_msr_bitmap msr_bitmap;

		guest_context* guest_context;

		size_t core_id;

		exception::exception_info exception_info;

		// queued NMIs to be injected into the guest
		size_t queued_nmi_count;
	};

	bool virtualize(vcpu* vcpu);

	void devirtualize(vcpu* vcpu);
}  // namespace hv::vcpu