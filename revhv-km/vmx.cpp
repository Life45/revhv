#include "vmx.h"

namespace hv::vmx
{
	bool check_vmx_support()
	{
		//
		// 25.6 Discovering Support for VMX
		//
		cpuid_eax_01 cpuid01 = {0};
		__cpuid(reinterpret_cast<int*>(&cpuid01), 0x01);

		if (!cpuid01.cpuid_feature_information_ecx.virtual_machine_extensions)
		{
			LOG_ERROR("VMX not supported by CPU");
			return false;
		}

		//
		// 25.7 Enabling and Entering VMX Operation
		//
		ia32_feature_control_register featureControl = {0};
		featureControl.flags = __readmsr(IA32_FEATURE_CONTROL);

		if (!featureControl.lock_bit)
		{
			LOG_ERROR("Lock bit is cleared, VMX cannot be enabled");
			return false;
		}

		if (!featureControl.enable_vmx_outside_smx)
		{
			LOG_ERROR("VMX outside SMX is not enabled");
			return false;
		}

		return true;
	}

	void enable_vmx()
	{
		//
		// 25.7 Enabling and Entering VMX Operation & 25.8 Restrictions on VMX Operation
		//

		// Disable interrupts
		_disable();

		cr4 cr4 = {0};
		cr4.flags = __readcr4();
		cr0 cr0 = {0};
		cr0.flags = __readcr0();

		auto cr0Fixed0 = __readmsr(IA32_VMX_CR0_FIXED0);
		auto cr0Fixed1 = __readmsr(IA32_VMX_CR0_FIXED1);
		auto cr4Fixed0 = __readmsr(IA32_VMX_CR4_FIXED0);
		auto cr4Fixed1 = __readmsr(IA32_VMX_CR4_FIXED1);

		cr4.vmx_enable = 1;

		// A.7 VMX-Fixed Bits in CR0
		cr0.flags |= cr0Fixed0;
		cr0.flags &= cr0Fixed1;

		// A.8 VMX-Fixed Bits in CR4
		cr4.flags |= cr4Fixed0;
		cr4.flags &= cr4Fixed1;

		__writecr4(cr4.flags);
		__writecr0(cr0.flags);

		// Enable interrupts
		_enable();
	}

	bool enter_vmx_operation(vcpu::vcpu* vcpu)
	{
		ia32_vmx_basic_register vmxBasic = {0};
		vmxBasic.flags = __readmsr(IA32_VMX_BASIC);

		//
		// 26.11.5 VMXON Region
		//
		vcpu->vmxon_region.revision_id = vmxBasic.vmcs_revision_id;
		vcpu->vmxon_region.must_be_zero = 0;

		// Execute VMXON
		PHYSICAL_ADDRESS vmxonPhys = MmGetPhysicalAddress(reinterpret_cast<void*>(&vcpu->vmxon_region));
		if (!vmx_vmxon(vmxonPhys.QuadPart))
		{
			LOG_ERROR("VMXON failed");
			return false;
		}

		return true;
	}

	bool launch_vm()
	{
		rflags rflags_out = {0};
		if (!vmx_vmlaunch_stub(&rflags_out.flags))
		{
			// 32.2 CONVENTIONS

			const bool CF = rflags_out.carry_flag;
			const bool PF = rflags_out.parity_flag;
			const bool AF = rflags_out.auxiliary_carry_flag;
			const bool ZF = rflags_out.zero_flag;
			const bool SF = rflags_out.sign_flag;
			const bool OF = rflags_out.overflow_flag;

			// VMfailInvalid
			if (CF && !PF && !AF && !ZF && !SF && !OF)
			{
				LOG_ERROR("VMLAUNCH failed: Current VMCS pointer is invalid");
				return false;
			}

			// VMfailValid
			if (!CF && !PF && !AF && ZF && !SF && !OF)
			{
				uint64_t error_code = vmx_vmread(VMCS_VM_INSTRUCTION_ERROR);

				switch (error_code)
				{
				case VMX_ERROR_VMENTRY_MOV_SS:
					LOG_ERROR("VMLAUNCH failed: VM entry with events blocked by MOV SS");
					return false;
				case VMX_ERROR_VMLAUCH_NON_CLEAR_VMCS:
					LOG_ERROR("VMLAUNCH failed: VMLAUNCH with non-clear VMCS");
					return false;
				case VMX_ERROR_VMENTRY_INVALID_CONTROL_FIELDS:
					LOG_ERROR("VMLAUNCH failed: VM entry with invalid control fields");
					return false;
				case VMX_ERROR_VMENTRY_INVALID_HOST_STATE:
					LOG_ERROR("VMLAUNCH failed: VM entry with invalid host state");
					return false;
				case VMX_ERROR_VMENTRY_INVALID_VMCS_EXECUTIVE_POINTER:
					LOG_ERROR("VMLAUNCH failed: VM entry with invalid executive VMCS pointer");
					return false;
				case VMX_ERROR_VMENTRY_NON_LAUNCHED_EXECUTIVE_VMCS:
					LOG_ERROR("VMLAUNCH failed: VM entry with non-launched executive VMCS");
					return false;
				case VMX_ERROR_VMENTRY_EXECUTIVE_VMCS_PTR:
					LOG_ERROR("VMLAUNCH failed: VM entry with executive VMCS pointer not VMXON pointer");
					return false;
				case VMX_ERROR_VMENTRY_INVALID_VM_EXECUTION_CONTROL:
					LOG_ERROR("VMLAUNCH failed: VM entry with invalid VM execution control");
					return false;
				default:
					LOG_ERROR("VMLAUNCH failed: Unknown error code %llx", error_code);
					return false;
				}
			}

			LOG_ERROR("VMLAUNCH failed: Invalid rflags combination to determine error: CF=%d, PF=%d, AF=%d, ZF=%d, SF=%d, OF=%d", CF, PF, AF, ZF, SF, OF);
			return false;
		}

		// We're now in VMX non-root mode with the VM successfully launched
		return true;
	}

	void exit_vmx_operation()
	{
		vmx_vmxoff();
	}

}  // namespace hv::vmx