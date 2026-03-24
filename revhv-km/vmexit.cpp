#include "vmexit.h"
#include "vmx.h"
#include "vmcs.h"
#include "serial.h"
#include "utils.hpp"
#include "exception_wrappers.h"
#include "error.h"
#include "hypercall.h"
#include "trace.h"

namespace hv::vmexit
{
	static void advance_guest_rip()
	{
		uint64_t rip = vmx::vmx_vmread(VMCS_GUEST_RIP);
		size_t instruction_length = vmx::vmx_vmread(VMCS_VMEXIT_INSTRUCTION_LENGTH);
		uint64_t new_rip = rip + instruction_length;

		if (rip < (1ull << 32) && new_rip >= (1ull << 32))
		{
			vmx_segment_access_rights cs_access_rights;
			cs_access_rights.flags = static_cast<uint32_t>(vmx::vmx_vmread(VMCS_GUEST_CS_ACCESS_RIGHTS));

			if (!cs_access_rights.long_mode)
			{
				// The RIP just wrapped around. This actually shouldn't happen in practice, but we need to clear the upper bits to avoid issues on vmentry.
				new_rip = new_rip & 0xFFFFFFFF;
				LOG_WARNING("Guest RIP wrapped around from %llx to %llx", rip, new_rip);
			}
		}

		if (!vmx::vmx_vmwrite(VMCS_GUEST_RIP, new_rip))
		{
			LOG_ERROR("Failed to advance guest RIP");
			// TODO: Fail appropriately
			__halt();
		}

		// Unblock interrupts due to STI or MOV SS, since we just emulated an instruction
		vmx_interruptibility_state interruptibility_state = {0};
		interruptibility_state.flags = vmx::vmx_vmread(VMCS_GUEST_INTERRUPTIBILITY_STATE);
		if (interruptibility_state.blocking_by_mov_ss || interruptibility_state.blocking_by_sti)
		{
			interruptibility_state.blocking_by_mov_ss = 0;
			interruptibility_state.blocking_by_sti = 0;

			if (!vmx::vmx_vmwrite(VMCS_GUEST_INTERRUPTIBILITY_STATE, interruptibility_state.flags))
			{
				LOG_ERROR("Failed to update guest interruptibility state");
			}
		}

		rflags guest_rflags = {0};
		guest_rflags.flags = vmx::vmx_vmread(VMCS_GUEST_RFLAGS);

		vmx_pending_debug_exceptions pending_dbg_exceptions = {0};
		pending_dbg_exceptions.flags = vmx::vmx_vmread(VMCS_GUEST_PENDING_DEBUG_EXCEPTIONS);

		if (guest_rflags.trap_flag && !pending_dbg_exceptions.bs)
		{
			// Inject a pending debug exception for single-stepping
			pending_dbg_exceptions.bs = 1;
			if (!vmx::vmx_vmwrite(VMCS_GUEST_PENDING_DEBUG_EXCEPTIONS, pending_dbg_exceptions.flags))
			{
				LOG_ERROR("Failed to set pending debug exceptions for single-stepping");
			}
		}
	}

	/// @brief Injects a host hardware exception to the guest, and clears host exception information
	/// @param vcpu VCPU
	static void inject_host_hw_exception_to_guest(vcpu::vcpu* vcpu)
	{
		// LOG_INFO("Injecting host hardware exception to guest: vector: %llx, error code: %llx", vcpu->exception_info.exception_vector, vcpu->exception_info.error_code);
		if (isr::vector_has_error_code(vcpu->exception_info.exception_vector))
		{
			vmx::inject_hw_exception(vcpu->exception_info.exception_vector, vcpu->exception_info.error_code);

			vcpu->exception_info.exception_occurred = false;
			vcpu->exception_info.exception_vector = 0;
			vcpu->exception_info.error_code = 0;
			vcpu->exception_info.additional_info1 = 0;
			return;
		}

		vmx::inject_hw_exception(vcpu->exception_info.exception_vector);

		vcpu->exception_info.exception_occurred = false;
		vcpu->exception_info.exception_vector = 0;
		vcpu->exception_info.error_code = 0;
		vcpu->exception_info.additional_info1 = 0;
	}

	static _declspec(noreturn) void handle_vm_entry_failure(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu, const vmx_vmexit_reason reason)
	{
		// 28.8 VM-ENTRY FAILURES DURING OR AFTER LOADING GUEST STATE

		uint64_t exit_qualification = vmx::vmx_vmread(VMCS_EXIT_QUALIFICATION);

		if (reason.basic_exit_reason == VMX_EXIT_REASON_ERROR_INVALID_GUEST_STATE)
		{
			switch (exit_qualification)
			{
			case 0:
				LOG_ERROR("VM-ENTRY FAILURE VMEXIT: Invalid guest state with no additional information");
				break;
			case 2:
				LOG_ERROR("VM-ENTRY FAILURE VMEXIT: Failure due to loading PDPTEs");
				break;
			case 3:
				LOG_ERROR("VM-ENTRY FAILURE VMEXIT: Failure was due to an attempt to inject a non-maskable interrupt (NMI) into a guest that is blocking events through the STI blocking bit in the interruptibility-state field.");
				break;
			case 4:
				LOG_ERROR("VM-ENTRY FAILURE VMEXIT: Failure was due to an invalid VMCS link pointer");
				break;
			default:
				LOG_ERROR("VM-ENTRY FAILURE VMEXIT: Unknown exit qualification: %llx", exit_qualification);
				break;
			}
		}
		else if (reason.basic_exit_reason == VMX_EXIT_REASON_ERROR_MSR_LOAD)
		{
			LOG_ERROR("VM-ENTRY FAILURE VMEXIT: Failure was due to an attempt to load MSRs. Problematic entry : %llx", exit_qualification);
		}
		else if (reason.basic_exit_reason == VMX_EXIT_REASON_ERROR_MACHINE_CHECK)
		{
			LOG_ERROR("VM-ENTRY FAILURE VMEXIT: Failure was due to a machine check exception");
		}
		else
		{
			LOG_ERROR("VM-ENTRY FAILURE VMEXIT: Unknown basic exit reason: %llx", reason.basic_exit_reason);
		}

		error::unrecoverable_host_error(vcpu);
	}

	static void handle_cpuid(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// cpuid_eax_10_ecx_02 is used as a generic struct in this case, independent of the actual CPUID function
		cpuid_eax_10_ecx_02 cpuid = {0};

		_mm_lfence();
		auto start = __rdtsc();
		_mm_lfence();

		__cpuidex(reinterpret_cast<int*>(&cpuid), vcpu->guest_context->eax, vcpu->guest_context->ecx);

		_mm_lfence();
		auto end = __rdtsc();

		vcpu->stealth_data.tsc_data.instruction_overhead = end - start;

		vcpu->guest_context->eax = cpuid.eax.flags;
		vcpu->guest_context->ebx = cpuid.ebx.flags;
		vcpu->guest_context->ecx = cpuid.ecx.flags;
		vcpu->guest_context->edx = cpuid.edx.flags;

		advance_guest_rip();
	}

	static void handle_rdmsr(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// Guest CPL check not performed since:
		// 27.1.1 Relative Priority of Faults and VM Exits:
		// Certain exceptions have priority over VM exits. These include invalid-opcode exceptions, faults based on
		// privilege level, and general-protection exceptions that are based on checking I/O permission bits in the taskstate segment (TSS).

		uint32_t msr = guest_context->ecx;

		// Check if it's one of the MSRs that we use differently on host
		// Since we don't exit for MSRs in the range 00000000H – 00001FFFH nor in the range C0000000H – C0001FFFH, we can skip those MSRs
		// unless a future implementation decides to exit for them, in that case a check is needed

		// This specific syntetic MSR costed me 2 days to debug. Thanks to HyperDbg, I discovered that in some NMI scenarios
		// such as Windbg attempting to break in, or probably any host NMI hitting at this point results in VMWare or Hyper-V halting the VCPU.
		// For that reason, we ignore this MSR and ABSOLUTELY DO NOT read it until my brain works again and thinks of a better fix.
		constexpr auto HV_X64_MSR_GUEST_IDLE = 0x400000f0;

		_mm_lfence();
		auto start = __rdtsc();
		_mm_lfence();
		uint64_t msr_result = msr == HV_X64_MSR_GUEST_IDLE ? 0 : exception_wrappers::rdmsr_wrapper(msr);

		_mm_lfence();
		auto end = __rdtsc();

		vcpu->stealth_data.tsc_data.instruction_overhead = end - start;

		// Check if an exception has occurred
		if (vcpu->exception_info.exception_occurred)
		{
			inject_host_hw_exception_to_guest(vcpu);
			return;
		}

		// Store the result in the guest context
		vcpu->guest_context->edx = msr_result >> 32;
		vcpu->guest_context->eax = msr_result & 0xFFFFFFFF;

		advance_guest_rip();
	}

	static bool is_mtrr_msr(uint32_t msr)
	{
		if (msr == IA32_MTRR_DEF_TYPE)
			return true;
		if (msr == IA32_MTRR_FIX64K_00000)
			return true;
		if (msr >= IA32_MTRR_FIX16K_80000 && msr <= IA32_MTRR_FIX16K_A0000)
			return true;
		if (msr >= IA32_MTRR_FIX4K_C0000 && msr <= IA32_MTRR_FIX4K_F8000)
			return true;
		if (msr >= IA32_MTRR_PHYSBASE0 && msr <= (IA32_MTRR_PHYSBASE0 + 19))
			return true;
		return false;
	}

	static void handle_wrmsr(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// Guest CPL check not performed since:
		// 27.1.1 Relative Priority of Faults and VM Exits:
		// Certain exceptions have priority over VM exits. These include invalid-opcode exceptions, faults based on
		// privilege level, and general-protection exceptions that are based on checking I/O permission bits in the taskstate segment (TSS).

		uint32_t msr = guest_context->ecx;

		// Check if it's one of the MSRs that we use differently on host
		// Since we don't exit for MSRs in the range 00000000H – 00001FFFH nor in the range C0000000H – C0001FFFH, we can skip those MSRs
		// unless a future implementation decides to exit for them, in that case a check is needed

		uint64_t value = (static_cast<uint64_t>(guest_context->edx) << 32) | guest_context->eax;

		_mm_lfence();
		auto start = __rdtsc();
		_mm_lfence();
		exception_wrappers::wrmsr_wrapper(msr, value);

		_mm_lfence();
		auto end = __rdtsc();

		vcpu->stealth_data.tsc_data.instruction_overhead = end - start;

		// Check if an exception has occurred
		if (vcpu->exception_info.exception_occurred)
		{
			inject_host_hw_exception_to_guest(vcpu);
			return;
		}

		if (is_mtrr_msr(msr))
		{
			LOG_INFO("Guest wrote to MTRR MSR: 0x%X. EPT memory types will be updated.", msr);
			memory::read_mtrrs(vcpu->mtrr_state);
			ept::update_ept_memory_types(vcpu->ept_pages_normal, vcpu->mtrr_state);
			ept::update_ept_memory_types(vcpu->ept_pages_target, vcpu->mtrr_state);
			vmx::invept(invept_all_context, 0);

			// Making sure MTRRs are consistent across cores is the guest's responsibility.
			// Therefore, we don't do any synchronization between cores here.
		}

		advance_guest_rip();
	}

	static void handle_invd(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// There are couple of checks involving INVD involving BIOS execution, SGX(and EPC pages), as well as TDX(and SEAM)
		// We'll just use an exception wrapper and blindly execute it for simplicity
		// We don't really want to execute INVD since it shouldn't really be used in most cases, and more than likely unsafe at this point.
		// However we'll comply with the guest for the sake of staying as close as possible to the real hardware.
		exception_wrappers::invd_wrapper();

		// Check if an exception has occurred
		if (vcpu->exception_info.exception_occurred)
		{
			inject_host_hw_exception_to_guest(vcpu);
			return;
		}

		// Log this so we at least know that this executed successfully(unfortunately)
		LOG_INFO("INVD executed successfully by guest at RIP: %llx", vmx::vmx_vmread(VMCS_GUEST_RIP));

		advance_guest_rip();
	}

	static void handle_getsec(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// SMX must already be disabled at this point. Reaching here doesn't make sense since #UD is thrown in case CR4.SMXE = 0 instead of a VMEXIT
		LOG_ERROR("GETSEC executed. This should not happen since SMX must already be disabled.");
		// Still inject #UD
		vmx::inject_hw_exception(invalid_opcode);
		return;
	}

	static void handle_xsetbv(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// TODO: Check if the guest is attempting to disable SSE, which might be needed by our host (depending on if MSVC emits SSE instructions)

		uint32_t index = guest_context->ecx;
		uint64_t value = (static_cast<uint64_t>(guest_context->edx) << 32) | guest_context->eax;

		_mm_lfence();
		auto start = __rdtsc();
		_mm_lfence();

		exception_wrappers::xsetbv_wrapper(index, value);

		_mm_lfence();
		auto end = __rdtsc();

		vcpu->stealth_data.tsc_data.instruction_overhead = end - start;

		// Check if an exception has occurred
		if (vcpu->exception_info.exception_occurred)
		{
			inject_host_hw_exception_to_guest(vcpu);
			return;
		}

		advance_guest_rip();
	}

	static void handle_nmi(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// If a host crash is in progress, devirtualize and give control back to the host OS
		error::give_control_on_unrecoverable_error(vcpu);

		LOG_INFO("Guest NMI received on core %i", vcpu->core_id);

		// we should normally check the interruptibility state to see if we can inject the NMI now or need to wait
		// however for consistency, we'll always enqueue the NMI and let the nmi window handler handle it

		// enqueue the NMI to be injected into the guest when NMIs are unblocked
		vcpu->queued_nmi_count++;

		// enable NMI window exiting
		vmcs::enable_nmi_window_exiting();
	}

	static void handle_nmi_window(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		--vcpu->queued_nmi_count;

		// inject the NMI into the guest
		vmx::inject_nmi();

		LOG_INFO("Injected NMI into guest on core %i, remaining queued NMIs: %lu", vcpu->core_id, vcpu->queued_nmi_count);

		if (vcpu->queued_nmi_count == 0)
		{
			// disable NMI window exiting
			vmcs::disable_nmi_window_exiting();

			// in case a host NMI occurred while we were handling the NMI window, we need to re-enable NMI window exiting
			if (vcpu->queued_nmi_count != 0)
				vmcs::enable_nmi_window_exiting();
		}
	}

	static void handle_vmx_instruction(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// Inject #UD for all VMX instructions
		vmx::inject_hw_exception(invalid_opcode);
	}

	static void handle_mov_to_cr(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu, const vmx_exit_qualification_mov_cr qualification)
	{
		// This is an easy index into the guest context, but I made it more explicit in case ordering somehow changed later
		auto get_gpr_value = [guest_context = guest_context](uint64_t reg) -> uint64_t
		{
			switch (reg)
			{
			case VMX_EXIT_QUALIFICATION_GENREG_RAX:
				return guest_context->rax;
			case VMX_EXIT_QUALIFICATION_GENREG_RCX:
				return guest_context->rcx;
			case VMX_EXIT_QUALIFICATION_GENREG_RDX:
				return guest_context->rdx;
			case VMX_EXIT_QUALIFICATION_GENREG_RBX:
				return guest_context->rbx;
			case VMX_EXIT_QUALIFICATION_GENREG_RSP:
				return vmx::vmx_vmread(VMCS_GUEST_RSP);
			case VMX_EXIT_QUALIFICATION_GENREG_RBP:
				return guest_context->rbp;
			case VMX_EXIT_QUALIFICATION_GENREG_RSI:
				return guest_context->rsi;
			case VMX_EXIT_QUALIFICATION_GENREG_RDI:
				return guest_context->rdi;
			case VMX_EXIT_QUALIFICATION_GENREG_R8:
				return guest_context->r8;
			case VMX_EXIT_QUALIFICATION_GENREG_R9:
				return guest_context->r9;
			case VMX_EXIT_QUALIFICATION_GENREG_R10:
				return guest_context->r10;
			case VMX_EXIT_QUALIFICATION_GENREG_R11:
				return guest_context->r11;
			case VMX_EXIT_QUALIFICATION_GENREG_R12:
				return guest_context->r12;
			case VMX_EXIT_QUALIFICATION_GENREG_R13:
				return guest_context->r13;
			case VMX_EXIT_QUALIFICATION_GENREG_R14:
				return guest_context->r14;
			case VMX_EXIT_QUALIFICATION_GENREG_R15:
				return guest_context->r15;
			}
		};

		auto reg_val = get_gpr_value(qualification.general_purpose_register);

		// 2.5 Control Registers
		auto curr_effective_cr0 = vmx::get_effective_guest_cr0();
		auto curr_effective_cr4 = vmx::get_effective_guest_cr4();

		if (qualification.control_register == VMX_EXIT_QUALIFICATION_REGISTER_CR0)
		{
			cr0 new_cr0 = {0};
			new_cr0.flags = reg_val;

			// Attempting to set any reserved bits in CR0[31:0] is ignored
			new_cr0.reserved1 = new_cr0.reserved2 = new_cr0.reserved3 = 0;

			// In the Pentium 4, Intel Xeon, and P6 family processors, ET is hardcoded to 1
			new_cr0.extension_type = 1;

			// Attempting to set any reserved bits in CR0[63:32] results in a general-protection exception, #GP(0)
			if (new_cr0.reserved4 != 0)
			{
				vmx::inject_hw_exception(general_protection, 0);
				return;
			}

			// Setting the PG flag when the PE flag is clear causes a general-protection exception (#GP).
			if (new_cr0.paging_enable && !new_cr0.protection_enable)
			{
				vmx::inject_hw_exception(general_protection, 0);
				return;
			}

			// WP cannot be cleared as long as CR4.CET = 1
			if (!new_cr0.write_protect && curr_effective_cr4.control_flow_enforcement_enable)
			{
				vmx::inject_hw_exception(general_protection, 0);
				return;
			}

			// CR0.PG cannot be cleared in 64-bit mode
			if (!new_cr0.paging_enable)
			{
				vmx::inject_hw_exception(general_protection, 0);
				return;
			}

			// #GP(0) when setting the CD flag to 0 when the NW flag is set to 1
			if (!new_cr0.cache_disable && new_cr0.not_write_through)
			{
				vmx::inject_hw_exception(general_protection, 0);
				return;
			}

			//
			// Note: Skipped other checks unrelated when in 64-bit mode
			// & No need to update EPT structures if CR0.CD was set, since if CR0.CD=1, effective memory type is always UC.
			// (ref: 31.3.7.2 Memory Type Used for Translated Guest-Physical Addresses)
			//

			// Update the guest's CR0 shadow
			if (!vmx::vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW, new_cr0.flags))
			{
				LOG_ERROR("Failed to update guest CR0 read shadow");
				error::unrecoverable_host_error(vcpu);
			}

			// Account for A.7 VMX-Fixed Bits in CR0
			// TODO: Cache these
			auto cr0Fixed0 = __readmsr(IA32_VMX_CR0_FIXED0);
			auto cr0Fixed1 = __readmsr(IA32_VMX_CR0_FIXED1);

			new_cr0.flags |= cr0Fixed0;
			new_cr0.flags &= cr0Fixed1;

			// Update the guest's CR0
			if (!vmx::vmx_vmwrite(VMCS_GUEST_CR0, new_cr0.flags))
			{
				LOG_ERROR("Failed to update guest CR0");
				error::unrecoverable_host_error(vcpu);
			}

			advance_guest_rip();
			return;
		}
		else if (qualification.control_register == VMX_EXIT_QUALIFICATION_REGISTER_CR4)
		{
			cr4 new_cr4 = {0};
			new_cr4.flags = reg_val;

			// Attempting to set any reserved bits in CR4 results in a general-protection exception, #GP(0)
			if (new_cr4.reserved1 != 0 || new_cr4.reserved2 != 0)
			{
				vmx::inject_hw_exception(general_protection, 0);
				return;
			}

			// CR4.CET can be set only if CR0.WP is set
			if (new_cr4.control_flow_enforcement_enable && !curr_effective_cr0.write_protect)
			{
				vmx::inject_hw_exception(general_protection, 0);
				return;
			}

			// MOV to CR4 causes a general-protection exception (#GP) if it would change CR4.PCIDE from 0 to 1 when CR3[11:0] ≠ 000H.
			if (!curr_effective_cr4.pcid_enable && new_cr4.pcid_enable && (vmx::vmx_vmread(VMCS_GUEST_CR3) & 0xFFF) != 0)
			{
				vmx::inject_hw_exception(general_protection, 0);
				return;
			}

			// CR4.PAE and CR4.LA57 cannot be modified while either 4-level paging or 5-level paging is in use (#GP(0))
			//
			// & PAE must be set before entering IA-32e mode.
			if (new_cr4.physical_address_extension != curr_effective_cr4.physical_address_extension || new_cr4.linear_addresses_57_bit != curr_effective_cr4.linear_addresses_57_bit)
			{
				vmx::inject_hw_exception(general_protection, 0);
				return;
			}

			// TODO: What are the interactions between VMX and SMX if guest enables it here?
			// If the CPUID SMX feature flag is clear, attempting to set CR4.SMXE[Bit 14] results in a general protection exception.
			cpuid_eax_01 cpuid;
			__cpuid(reinterpret_cast<int*>(&cpuid), 1);
			if (!cpuid.cpuid_feature_information_ecx.safer_mode_extensions && new_cr4.smx_enable)
			{
				vmx::inject_hw_exception(general_protection, 0);
				return;
			}

			if (!vmx::vmx_vmwrite(VMCS_CTRL_CR4_READ_SHADOW, new_cr4.flags))
			{
				LOG_ERROR("Failed to update guest CR4 read shadow");
				error::unrecoverable_host_error(vcpu);
			}

			// Account for A.7 VMX-Fixed Bits in CR4
			// TODO: Cache these
			auto cr4Fixed0 = __readmsr(IA32_VMX_CR4_FIXED0);
			auto cr4Fixed1 = __readmsr(IA32_VMX_CR4_FIXED1);

			new_cr4.flags |= cr4Fixed0;
			new_cr4.flags &= cr4Fixed1;

			if (!vmx::vmx_vmwrite(VMCS_GUEST_CR4, new_cr4.flags))
			{
				LOG_ERROR("Failed to update guest CR4");
				error::unrecoverable_host_error(vcpu);
			}

			// TODO: What happens if guest changes CR4.VMXE ?

			advance_guest_rip();
			return;
		}
		else
		{
			LOG_ERROR("MOV to CR%u is not handled", qualification.control_register);
			error::unrecoverable_host_error(vcpu);
		}
	}

	static void handle_clts(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu, const vmx_exit_qualification_mov_cr qualification)
	{
		// Clear CR0.TS from both the shadow and the actual CR0
		auto current_cr0_shadow = vmx::vmx_vmread(VMCS_CTRL_CR0_READ_SHADOW);
		auto current_cr0 = vmx::vmx_vmread(VMCS_GUEST_CR0);

		current_cr0_shadow &= ~CR0_TASK_SWITCHED_FLAG;
		current_cr0 &= ~CR0_TASK_SWITCHED_FLAG;

		if (!vmx::vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW, current_cr0_shadow))
		{
			LOG_ERROR("Failed to update guest CR0 read shadow for CLTS");
			error::unrecoverable_host_error(vcpu);
		}

		if (!vmx::vmx_vmwrite(VMCS_GUEST_CR0, current_cr0))
		{
			LOG_ERROR("Failed to update guest CR0 for CLTS");
			error::unrecoverable_host_error(vcpu);
		}
	}

	static void handle_lmsw(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu, const vmx_exit_qualification_mov_cr qualification)
	{
		// Only the low-order 4 bits of the source operand (which contains the PE, MP, EM, and TS flags) are loaded into CR0
		constexpr uint64_t lmsw_mask = 0xF;

		auto new_bits = static_cast<uint64_t>(qualification.lmsw_source_data) & lmsw_mask;

		auto current_cr0_shadow = vmx::vmx_vmread(VMCS_CTRL_CR0_READ_SHADOW);
		auto current_cr0 = vmx::vmx_vmread(VMCS_GUEST_CR0);

		// PE (bit 0) can only be set, not cleared by LMSW
		if (current_cr0_shadow & CR0_PROTECTION_ENABLE_FLAG)
			new_bits |= CR0_PROTECTION_ENABLE_FLAG;

		auto new_cr0_shadow = (current_cr0_shadow & ~lmsw_mask) | new_bits;
		auto new_cr0 = (current_cr0 & ~lmsw_mask) | new_bits;

		if (!vmx::vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW, new_cr0_shadow))
		{
			LOG_ERROR("Failed to update guest CR0 read shadow for LMSW");
			error::unrecoverable_host_error(vcpu);
		}

		// Account for A.7 VMX-Fixed Bits in CR0
		auto cr0Fixed0 = __readmsr(IA32_VMX_CR0_FIXED0);
		auto cr0Fixed1 = __readmsr(IA32_VMX_CR0_FIXED1);

		new_cr0 |= cr0Fixed0;
		new_cr0 &= cr0Fixed1;

		if (!vmx::vmx_vmwrite(VMCS_GUEST_CR0, new_cr0))
		{
			LOG_ERROR("Failed to update guest CR0 for LMSW");
			error::unrecoverable_host_error(vcpu);
		}

		advance_guest_rip();
	}

	static void handle_cr_access(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		//
		// 27.1.3 Instructions That Cause VM Exits Conditionally
		//
		// MOV from CR3, CR8 and MOV to CR3, MOV to CR8 does not exit for our current implementation (since their respective exit controls are not set)
		// Others in this category are conditionally exiting, therefore we need to account for those.
		//
		// Note: CR3 exiting specifically depends on the CPU architecture. Some processors only support the 1-setting of CR3-load/store exiting.
		// TODO: Add warning for the CR3 thing on VMCS setup
		//

		vmx_exit_qualification_mov_cr qualification;
		qualification.flags = vmx::vmx_vmread(VMCS_EXIT_QUALIFICATION);

		switch (qualification.access_type)
		{
		case VMX_EXIT_QUALIFICATION_ACCESS_MOV_TO_CR:
			handle_mov_to_cr(guest_context, vcpu, qualification);
			break;
		case VMX_EXIT_QUALIFICATION_ACCESS_CLTS:
			handle_clts(guest_context, vcpu, qualification);
			break;
		case VMX_EXIT_QUALIFICATION_ACCESS_LMSW:
			handle_lmsw(guest_context, vcpu, qualification);
			break;
		default:  // MOV_FROM.. is left as default as they only exist for CR3 and CR8
			LOG_ERROR("Unknown control register access exit qualification: %llx", qualification.flags);
			error::unrecoverable_host_error(vcpu);
			break;
		}

		// TODO: Measure or what ?
		vcpu->stealth_data.tsc_data.instruction_overhead = 100;
	}

	static void handle_ept_violation(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		const auto& current_eptp = vcpu->in_normal_execution ? vcpu->eptp_normal_execution : vcpu->eptp_target_execution;
		auto& current_ept_pages = vcpu->in_normal_execution ? vcpu->ept_pages_normal : vcpu->ept_pages_target;

		vmx_exit_qualification_ept_violation qualification;
		qualification.flags = vmx::vmx_vmread(VMCS_EXIT_QUALIFICATION);

		// guest physical address that caused the ept-violation
		const auto physical_address = vmx::vmx_vmread(qualification.caused_by_translation ? VMCS_GUEST_PHYSICAL_ADDRESS : VMCS_EXIT_GUEST_LINEAR_ADDRESS);

		const auto hook = ept::get_hook_by_orig_pfn(current_ept_pages, physical_address >> 12);
		if (hook && hook->pte)
		{
			auto pte = hook->pte;
			if (qualification.execute_access)
			{
				pte->read_access = 0;
				pte->write_access = 0;
				pte->execute_access = 1;
				pte->page_frame_number = hook->hook_pfn;
			}
			else
			{
				pte->read_access = 1;
				pte->write_access = 1;
				pte->execute_access = 0;
				pte->page_frame_number = hook->orig_pfn;
			}

			vmx::invept(invept_single_context, current_eptp.flags);
			return;
		}

		if (qualification.execute_access)
		{
			if (vcpu->in_normal_execution)
			{
				// We're transitioning from normal execution to target execution, we need to switch the EPTP to the target EPTP
				vmx::change_eptp(vcpu->eptp_target_execution);
				vcpu->in_normal_execution = false;
				return;
			}
			else
			{
				// We're transitioning from target execution to normal execution, we need to switch the EPTP to the normal EPTP
				vmx::change_eptp(vcpu->eptp_normal_execution);
				vcpu->in_normal_execution = true;

				// Emit binary trace entry
				auto guest_rip = vmx::vmx_vmread(VMCS_GUEST_RIP);
				hv::trace::emit(vcpu->trace_buffer, ::trace::fmt_ept_target_transition, static_cast<uint16_t>(vcpu->core_id), guest_rip);

				// TODO: Log more info such as return address, per-function info for common APIs, etc.
				return;
			}
		}

		// Should never reach here
		LOG_ERROR("Unhandled EPT violation. Qualification: %llx, physical address: %llx", qualification.flags, physical_address);
		error::unrecoverable_host_error(vcpu);
	}

	void* handler(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		vcpu->guest_context = guest_context;
		stealth::on_vmexit(vcpu->stealth_data);
		stealth::tsc_data* return_data = nullptr;

		vmx_vmexit_reason reason;
		reason.flags = vmx::vmx_vmread(VMCS_EXIT_REASON);

		vmx::clear_vmentry_interrupt_info();

		// 28.8 VM-ENTRY FAILURES DURING OR AFTER LOADING GUEST STATE
		if (reason.vm_entry_failure)
		{
			handle_vm_entry_failure(guest_context, vcpu, reason);
			return nullptr;
		}

		switch (reason.basic_exit_reason)
		{
		case VMX_EXIT_REASON_EXECUTE_CPUID:
			handle_cpuid(guest_context, vcpu);
			return_data = &vcpu->stealth_data.tsc_data;	 // Hide the overhead
			break;
		case VMX_EXIT_REASON_EXECUTE_INVD:
			handle_invd(guest_context, vcpu);
			break;
		case VMX_EXIT_REASON_EXECUTE_RDMSR:
			handle_rdmsr(guest_context, vcpu);
			return_data = &vcpu->stealth_data.tsc_data;	 // Hide the overhead
			break;
		case VMX_EXIT_REASON_EXECUTE_WRMSR:
			handle_wrmsr(guest_context, vcpu);
			return_data = &vcpu->stealth_data.tsc_data;	 // Hide the overhead
			break;
		case VMX_EXIT_REASON_EXECUTE_XSETBV:
			handle_xsetbv(guest_context, vcpu);
			return_data = &vcpu->stealth_data.tsc_data;	 // Hide the overhead
			break;
		case VMX_EXIT_REASON_EXECUTE_GETSEC:
			handle_getsec(guest_context, vcpu);
			break;
		case VMX_EXIT_REASON_NMI_WINDOW:
			handle_nmi_window(guest_context, vcpu);
			break;
		case VMX_EXIT_REASON_EXCEPTION_OR_NMI:	// We don't exit for other exceptions, only NMIs
			handle_nmi(guest_context, vcpu);
			break;
		case VMX_EXIT_REASON_EXECUTE_VMCALL:
			if (!hypercall::handle_hypercall(guest_context, vcpu))
			{
				handle_vmx_instruction(guest_context, vcpu);  // If hypercall key is invalid, treat it as an invalid VMX instruction
				break;
			}
			advance_guest_rip();
			break;
		case VMX_EXIT_REASON_EPT_VIOLATION:
			handle_ept_violation(guest_context, vcpu);
			break;
		case VMX_EXIT_REASON_VMX_PREEMPTION_TIMER_EXPIRED:
			// Re-sync TSC
			vcpu->stealth_data.tsc_data.tsc_offset = 0;
			vmx::vmx_vmwrite(VMCS_CTRL_TSC_OFFSET, 0);
			// Disable the preemption timer
			vmx::disable_preemption_timer(vcpu->tsc_preemption_relation_bit);
			vcpu->preemption_timer_enabled = false;
			break;
		case VMX_EXIT_REASON_MOV_CR:
			handle_cr_access(guest_context, vcpu);
			return_data = &vcpu->stealth_data.tsc_data;	 // Hide the overhead
			break;
		case VMX_EXIT_REASON_EXECUTE_INVEPT:
		case VMX_EXIT_REASON_EXECUTE_INVVPID:
		case VMX_EXIT_REASON_EXECUTE_SEAMCALL:
		case VMX_EXIT_REASON_EXECUTE_TDCALL:
		case VMX_EXIT_REASON_EXECUTE_VMCLEAR:
		case VMX_EXIT_REASON_EXECUTE_VMLAUNCH:
		case VMX_EXIT_REASON_EXECUTE_VMPTRLD:
		case VMX_EXIT_REASON_EXECUTE_VMPTRST:
		case VMX_EXIT_REASON_EXECUTE_VMRESUME:
		case VMX_EXIT_REASON_EXECUTE_VMXOFF:
		case VMX_EXIT_REASON_EXECUTE_VMXON:
		case VMX_EXIT_REASON_EXECUTE_VMREAD:
		case VMX_EXIT_REASON_EXECUTE_VMWRITE:
			handle_vmx_instruction(guest_context, vcpu);
			break;
		default:
			LOG_ERROR("Unknown VMEXIT reason: %llx", reason.basic_exit_reason);
			error::unrecoverable_host_error(vcpu);
			break;
		}

		if (!vcpu->stealth_data.bench_completed)
			return_data = nullptr;	// Don't do any hiding until the benchmark is completed.

		if (return_data)
		{
			// Enable preemption timer if not enabled and the tsc offset
			if (!vcpu->preemption_timer_enabled)
			{
				vmx::enable_preemption_timer(vcpu->tsc_preemption_relation_bit, 10000000);
				vcpu->preemption_timer_enabled = true;
			}
		}

		return reinterpret_cast<void*>(return_data);
	}
}  // namespace hv::vmexit