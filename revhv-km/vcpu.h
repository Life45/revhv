#pragma once
#include "includes.h"
#include "gdt.h"

namespace hv::vcpu
{
	constexpr size_t host_stack_size = 0x8000;	// 32 KB
	constexpr size_t ist_size = 0x4000;			// 16 KB

	struct vcpu
	{
		alignas(0x1000) uint8_t host_stack[host_stack_size];

		alignas(0x1000) task_state_segment_64 host_tss;

		alignas(0x1000) segment_descriptor_32 host_gdt[gdt::host_descriptor_count];

		// IST(1) for NMI
		alignas(0x1000) uint8_t host_ist1_nmi[ist_size];

		// IST(2) for Double Fault
		alignas(0x1000) uint8_t host_ist2_df[ist_size];

		// IST(3) for Machine Check
		alignas(0x1000) uint8_t host_ist3_mc[ist_size];

		alignas(0x1000) vmxon vmxon_region;

		alignas(0x1000)::vmcs vmcs_region;

		alignas(0x1000) vmx_msr_bitmap msr_bitmap;
	};

	bool virtualize(vcpu* vcpu);

	void devirtualize(vcpu* vcpu);
}  // namespace hv::vcpu