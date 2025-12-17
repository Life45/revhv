#include "vcpu.h"
#include "vmx.h"
#include "vmcs.h"

namespace hv::vcpu
{
	bool virtualize(vcpu* vcpu)
	{
		if (!vmx::check_vmx_support())
		{
			LOG_ERROR("VMX is not supported by CPU");
			return false;
		}

		vmx::enable_vmx();

		if (!vmx::enter_vmx_operation(vcpu))
		{
			LOG_ERROR("Failed to enter VMX operation");
			return false;
		}

		gdt::initialize(vcpu->host_gdt, &vcpu->host_tss);
		gdt::initialize_tss(&vcpu->host_tss, vcpu->host_ist1_nmi, vcpu->host_ist2_df, vcpu->host_ist3_mc, ist_size);

		idt::initialize(vcpu->host_idt);

		if (!vmcs::load_vmcs(vcpu))
		{
			LOG_ERROR("Failed to load VMCS region");
			return false;
		}

		if (!vmcs::write_control_fields(vcpu))
		{
			LOG_ERROR("Failed to write control fields to VMCS");
			return false;
		}

		LOG_INFO("Launcing vCPU %lu...", vcpu->core_id);

		if (!vmx::launch_vm())
		{
			LOG_ERROR("Failed to launch VM");
			return false;
		}

		LOG_INFO("vCPU %lu launched successfully", vcpu->core_id);

		return true;
	}

}  // namespace hv::vcpu