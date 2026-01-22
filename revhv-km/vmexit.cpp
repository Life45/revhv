#include "vmexit.h"
#include "vmx.h"
#include "vmcs.h"
#include "serial.h"
#include "utils.hpp"
#include "exception_wrappers.h"
#include "error.h"

namespace hv::vmexit
{
	static void advance_guest_rip()
	{
		uint64_t rip = vmx::vmx_vmread(VMCS_GUEST_RIP);
		size_t instruction_length = vmx::vmx_vmread(VMCS_VMEXIT_INSTRUCTION_LENGTH);
		if (!vmx::vmx_vmwrite(VMCS_GUEST_RIP, rip + instruction_length))
		{
			LOG_ERROR("Failed to advance guest RIP");
			// TODO: Fail appropriately
			__halt();
		}
	}

	/// @brief Injects a host hardware exception to the guest, and clears host exception information
	/// @param vcpu VCPU
	static void inject_host_hw_exception_to_guest(vcpu::vcpu* vcpu)
	{
		LOG_INFO("Injecting host hardware exception to guest: vector: %llx, error code: %llx", vcpu->exception_info.exception_vector, vcpu->exception_info.error_code);
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

		// TODO: Fail appropriately
		__halt();
	}

	static void handle_cpuid(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// cpuid_eax_10_ecx_02 is used as a generic struct in this case, independent of the actual CPUID function
		cpuid_eax_10_ecx_02 cpuid = {0};

		__cpuidex(reinterpret_cast<int*>(&cpuid), vcpu->guest_context->eax, vcpu->guest_context->ecx);

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

		uint64_t msr_result = exception_wrappers::rdmsr_wrapper(msr);

		// Check if an exception has occurred
		if (vcpu->exception_info.exception_occurred)
		{
			inject_host_hw_exception_to_guest(vcpu);
			return;
		}

		// Store the result in the guest context
		vcpu->guest_context->eax = msr_result >> 32;
		vcpu->guest_context->edx = msr_result & 0xFFFFFFFF;

		advance_guest_rip();
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

		exception_wrappers::wrmsr_wrapper(msr, value);

		// Check if an exception has occurred
		if (vcpu->exception_info.exception_occurred)
		{
			inject_host_hw_exception_to_guest(vcpu);
			return;
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
		// Still inject #GP(0)
		vmx::inject_hw_exception(general_protection, 0);
		return;
	}

	static void handle_xsetbv(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// TODO: Check if the guest is attempting to disable SSE, which might be needed by our host (depending on if MSVC emits SSE instructions)

		uint32_t index = guest_context->ecx;
		uint64_t value = (static_cast<uint64_t>(guest_context->edx) << 32) | guest_context->eax;

		exception_wrappers::xsetbv_wrapper(index, value);

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

	void handler(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		vcpu->guest_context = guest_context;

		vmx_vmexit_reason reason;
		reason.flags = vmx::vmx_vmread(VMCS_EXIT_REASON);

		vmx::clear_vmentry_interrupt_info();

		// 28.8 VM-ENTRY FAILURES DURING OR AFTER LOADING GUEST STATE
		if (reason.vm_entry_failure)
		{
			handle_vm_entry_failure(guest_context, vcpu, reason);
			return;
		}

		switch (reason.basic_exit_reason)
		{
		case VMX_EXIT_REASON_EXECUTE_CPUID:
			handle_cpuid(guest_context, vcpu);
			break;
		case VMX_EXIT_REASON_EXECUTE_INVD:
			handle_invd(guest_context, vcpu);
			break;
		case VMX_EXIT_REASON_EXECUTE_RDMSR:
			handle_rdmsr(guest_context, vcpu);
			break;
		case VMX_EXIT_REASON_EXECUTE_WRMSR:
			handle_wrmsr(guest_context, vcpu);
			break;
		case VMX_EXIT_REASON_EXECUTE_XSETBV:
			handle_xsetbv(guest_context, vcpu);
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
		case VMX_EXIT_REASON_EXECUTE_INVEPT:
		case VMX_EXIT_REASON_EXECUTE_INVVPID:
		case VMX_EXIT_REASON_EXECUTE_SEAMCALL:
		case VMX_EXIT_REASON_EXECUTE_TDCALL:
		case VMX_EXIT_REASON_EXECUTE_VMCALL:
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
			// TODO: Fail appropriately
			__halt();
			break;
		}
	}
}  // namespace hv::vmexit