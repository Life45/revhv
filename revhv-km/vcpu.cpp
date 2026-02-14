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

		memory::read_mtrrs(vcpu->mtrr_state);

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

	void devirtualize(vcpu* vcpu)
	{
		vmx::vmx_vmxoff();

		// Restore CR3 and PAT
		__writecr3(vcpu->restore_context.cr3);
		__writemsr(IA32_PAT, vcpu->restore_context.pat);

		// Flush TLB
		__wbinvd();	 // Write back and invalidate cache
		auto cr4 = __readcr4();
		__writecr4(cr4 & ~(1ULL << 7));	 // Clear PGE flag
		__writecr4(cr4);				 // Set PGE flag again

		// Restore other MSRs
		__writemsr(IA32_EFER, vcpu->restore_context.efer);
		__writemsr(IA32_SYSENTER_CS, vcpu->restore_context.sysenter_cs);
		__writemsr(IA32_SYSENTER_ESP, vcpu->restore_context.sysenter_esp);
		__writemsr(IA32_SYSENTER_EIP, vcpu->restore_context.sysenter_eip);

		// Restore GDT and IDT
		_lgdt(&vcpu->restore_context.gdtr);
		__lidt(&vcpu->restore_context.idtr);

		// Restore segments
		utils::segment::write_ds(vcpu->restore_context.ds.flags);
		utils::segment::write_es(vcpu->restore_context.es.flags);
		utils::segment::write_fs(vcpu->restore_context.fs.flags);
		utils::segment::write_gs(vcpu->restore_context.gs.flags);

		// Restore MSR-based segment bases
		__writemsr(IA32_FS_BASE, vcpu->restore_context.fs_base);
		__writemsr(IA32_GS_BASE, vcpu->restore_context.gs_base);
		__writemsr(IA32_KERNEL_GS_BASE, vcpu->restore_context.kernel_gs_base);
	}

}  // namespace hv::vcpu