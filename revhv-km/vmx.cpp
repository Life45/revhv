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

	void exit_vmx_operation()
	{
		vmx_vmxoff();
	}

}  // namespace hv::vmx

// Inline wrappers
namespace hv::vmx
{
	inline bool vmx_vmxon(uint64_t vmxonPhys)
	{
		return __vmx_on(reinterpret_cast<uint64_t*>(vmxonPhys)) == 0;
	}

	inline void vmx_vmxoff()
	{
		__vmx_off();
	}

	inline bool vmx_vmclear(uint64_t vmcsPhys)
	{
		return __vmx_vmclear(reinterpret_cast<uint64_t*>(vmcsPhys)) == 0;
	}

	inline bool vmx_vmptrld(uint64_t vmcsPhys)
	{
		return __vmx_vmptrld(reinterpret_cast<uint64_t*>(vmcsPhys)) == 0;
	}

	inline bool vmx_vmwrite(uint64_t field, uint64_t value)
	{
		return __vmx_vmwrite(field, value) == 0;
	}
}  // namespace hv::vmx