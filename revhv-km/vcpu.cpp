#include "vcpu.h"
#include "vmx.h"
#include "vmcs.h"
#include "utils.hpp"

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
		// Capture current state for restoration in case of host double fault
		_sgdt(&vcpu->restore_context.gdtr);
		__sidt(&vcpu->restore_context.idtr);
		vcpu->restore_context.cr3 = __readcr3();

		vcpu->restore_context.cs = utils::segment::read_cs();
		vcpu->restore_context.ss = utils::segment::read_ss();
		vcpu->restore_context.ds = utils::segment::read_ds();
		vcpu->restore_context.es = utils::segment::read_es();
		vcpu->restore_context.fs = utils::segment::read_fs();
		vcpu->restore_context.gs = utils::segment::read_gs();
		vcpu->restore_context.tr = utils::segment::read_tr();
		vcpu->restore_context.ldtr = utils::segment::read_ldtr();

		vcpu->restore_context.fs_base = __readmsr(IA32_FS_BASE);
		vcpu->restore_context.gs_base = __readmsr(IA32_GS_BASE);
		vcpu->restore_context.kernel_gs_base = __readmsr(IA32_KERNEL_GS_BASE);

		vcpu->restore_context.efer = __readmsr(IA32_EFER);
		vcpu->restore_context.pat = __readmsr(IA32_PAT);

		vcpu->restore_context.sysenter_cs = __readmsr(IA32_SYSENTER_CS);
		vcpu->restore_context.sysenter_esp = __readmsr(IA32_SYSENTER_ESP);
		vcpu->restore_context.sysenter_eip = __readmsr(IA32_SYSENTER_EIP);

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