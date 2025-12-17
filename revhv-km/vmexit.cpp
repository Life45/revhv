#include "vmexit.h"
#include "vmx.h"
#include "serial.h"

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

	void handler(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		vcpu->guest_context = guest_context;

		vmx_vmexit_reason reason;
		reason.flags = vmx::vmx_vmread(VMCS_EXIT_REASON);

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
		default:
			LOG_ERROR("Unknown VMEXIT reason: %llx", reason.basic_exit_reason);
			// TODO: Fail appropriately
			__halt();
			break;
		}
	}
}  // namespace hv::vmexit