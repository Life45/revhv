#include "vmcs.h"
#include "vmx.h"
#include "utils.hpp"
#include "hv.h"
#include "vmexit.h"

namespace hv::vmcs
{

	static bool write_pin_based_vm_execution_controls()
	{
		//
		// 26.6.1 Pin-Based VM-Execution Controls
		//
		ia32_vmx_pinbased_ctls_register desiredPinBasedCtls = {0};

		desiredPinBasedCtls.external_interrupt_exiting = 0;
		desiredPinBasedCtls.nmi_exiting = 0;
		desiredPinBasedCtls.virtual_nmi = 0;
		desiredPinBasedCtls.activate_vmx_preemption_timer = 0;
		desiredPinBasedCtls.process_posted_interrupts = 0;

		if (!write_control_field(desiredPinBasedCtls.flags, VMCS_CTRL_PIN_BASED_VM_EXECUTION_CONTROLS, IA32_VMX_PINBASED_CTLS, IA32_VMX_TRUE_PINBASED_CTLS))
		{
			LOG_ERROR("Failed to write pin-based VM-execution controls to VMCS");
			return false;
		}

		return true;
	}

	static bool write_processor_based_vm_execution_controls()
	{
		//
		// 26.6.2 Processor-Based VM-Execution Controls
		//

		// Primary Processor-Based VM-Execution Controls
		// @note: Only 1-settings are explicitly set in this function, everything else is 0.

		ia32_vmx_procbased_ctls_register desiredProcBasedCtls = {0};

		desiredProcBasedCtls.use_tsc_offsetting = 1;
		desiredProcBasedCtls.use_msr_bitmaps = 1;
		desiredProcBasedCtls.activate_secondary_controls = 1;

		if (!write_control_field(desiredProcBasedCtls.flags, VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS, IA32_VMX_PROCBASED_CTLS, IA32_VMX_TRUE_PROCBASED_CTLS))
		{
			LOG_ERROR("Failed to write primary processor-based VM-execution controls to VMCS");
			return false;
		}

		ia32_vmx_procbased_ctls2_register desiredProcBasedCtls2 = {0};
		// TODO: Check if instruction is currently supported for "enable_x" where x is the instruction
		desiredProcBasedCtls2.enable_rdtscp = 1;
		desiredProcBasedCtls2.enable_vpid = 1;
		desiredProcBasedCtls2.enable_invpcid = 1;
		desiredProcBasedCtls2.conceal_vmx_from_pt = 1;
		desiredProcBasedCtls2.enable_xsaves = 1;
		desiredProcBasedCtls2.enable_user_wait_pause = 1;

		if (!write_control_field(desiredProcBasedCtls2.flags, VMCS_CTRL_SECONDARY_PROCESSOR_BASED_VM_EXECUTION_CONTROLS, IA32_VMX_PROCBASED_CTLS2, IA32_VMX_PROCBASED_CTLS2))
		{
			LOG_ERROR("Failed to write secondary processor-based VM-execution controls to VMCS");
			return false;
		}

		return true;
	}

	static bool write_vmexit_controls()
	{
		//
		// 26.7.1 Primary VM-Exit Controls
		//

		ia32_vmx_exit_ctls_register desiredExitCtls = {0};
		desiredExitCtls.save_debug_controls = 1;
		desiredExitCtls.host_address_space_size = 1;
		desiredExitCtls.save_ia32_pat = 1;
		desiredExitCtls.load_ia32_pat = 1;
		desiredExitCtls.save_ia32_efer = 1;
		desiredExitCtls.load_ia32_efer = 1;
		desiredExitCtls.save_ia32_perf_global_ctl = 1;
		desiredExitCtls.load_ia32_perf_global_ctrl = 1;
		desiredExitCtls.conceal_vmx_from_pt = 1;

		if (!write_control_field(desiredExitCtls.flags, VMCS_CTRL_PRIMARY_VMEXIT_CONTROLS, IA32_VMX_EXIT_CTLS, IA32_VMX_TRUE_EXIT_CTLS))
		{
			LOG_ERROR("Failed to write primary VM-exit controls to VMCS");
			return false;
		}

		//
		// 26.7.2 VM-Exit Controls for MSRs
		//

		// TODO: Any other MSRs to save/load ? Such as IA32_APERF, etc.
		if (!vmx::vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_STORE_COUNT, 0))
		{
			LOG_ERROR("Failed to write VM-exit MSR store count to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_STORE_ADDRESS, 0ull))
		{
			LOG_ERROR("Failed to write VM-exit MSR store value 0 to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_LOAD_COUNT, 0))
		{
			LOG_ERROR("Failed to write VM-exit MSR load count to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_CTRL_VMEXIT_MSR_LOAD_ADDRESS, 0ull))
		{
			LOG_ERROR("Failed to write VM-exit MSR load address to VMCS");
			return false;
		}

		return true;
	}

	static bool write_vmentry_controls()
	{
		//
		// 26.8.1 VM-Entry Controls
		//

		ia32_vmx_entry_ctls_register desiredEntryCtls = {0};
		desiredEntryCtls.load_debug_controls = 1;
		desiredEntryCtls.ia32e_mode_guest = 1;
		desiredEntryCtls.load_ia32_efer = 1;
		desiredEntryCtls.load_ia32_pat = 1;
		desiredEntryCtls.load_ia32_perf_global_ctrl = 1;
		desiredEntryCtls.conceal_vmx_from_pt = 1;

		if (!write_control_field(desiredEntryCtls.flags, VMCS_CTRL_VMENTRY_CONTROLS, IA32_VMX_ENTRY_CTLS, IA32_VMX_TRUE_ENTRY_CTLS))
		{
			LOG_ERROR("Failed to write VM-entry controls to VMCS");
			return false;
		}

		//
		// 26.8.2 VM-Entry Controls for MSRs
		//
		if (!vmx::vmx_vmwrite(VMCS_CTRL_VMENTRY_MSR_LOAD_COUNT, 0))
		{
			LOG_ERROR("Failed to write VM-entry MSR load count to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_CTRL_VMENTRY_MSR_LOAD_ADDRESS, 0ull))
		{
			LOG_ERROR("Failed to write VM-entry MSR load address to VMCS");
			return false;
		}

		return true;
	}

	static bool write_other_control_fields(vcpu::vcpu* vcpu)
	{
		//
		// 26.6.3 Exception Bitmap & 27.2 Page faults
		//

		// Don't exit for any exceptions
		uint64_t exceptionBitmap = 0;
		utils::set_bit(exceptionBitmap, page_fault);

		if (!vmx::vmx_vmwrite(VMCS_CTRL_EXCEPTION_BITMAP, exceptionBitmap))
		{
			LOG_ERROR("Failed to write exception bitmap to VMCS");
			return false;
		}

		// Don't exit for any page faults
		if (!vmx::vmx_vmwrite(VMCS_CTRL_PAGEFAULT_ERROR_CODE_MASK, 0ull))
		{
			LOG_ERROR("Failed to write page fault error code mask to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_CTRL_PAGEFAULT_ERROR_CODE_MATCH, MAXULONG_PTR))
		{
			LOG_ERROR("Failed to write page fault error code match to VMCS");
			return false;
		}

		//
		// 26.6.5 Time-Stamp Counter Offset and Multiplier
		//
		if (!vmx::vmx_vmwrite(VMCS_CTRL_TSC_OFFSET, 0ull))
		{
			LOG_ERROR("Failed to write TSC offset to VMCS");
			return false;
		}

		//
		// 26.6.6 Guest/Host Masks and Read Shadows for CR0 and CR4
		//

		// Set up CR4 guest/host masks to only exit in case a reserved bit or VMX enable flag is written to
		uint64_t cr4GuestHostMask = __readmsr(IA32_VMX_CR4_FIXED0) | ~__readmsr(IA32_VMX_CR4_FIXED1) | CR4_VMX_ENABLE_FLAG;
		// Set up CR0 guest/host masks to only exit in case a reserved bit
		uint64_t cr0GuestHostMask = __readmsr(IA32_VMX_CR0_FIXED0) | ~__readmsr(IA32_VMX_CR0_FIXED1);

		if (!vmx::vmx_vmwrite(VMCS_CTRL_CR4_GUEST_HOST_MASK, cr4GuestHostMask))
		{
			LOG_ERROR("Failed to write CR4 mask to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_CTRL_CR0_GUEST_HOST_MASK, cr0GuestHostMask))
		{
			LOG_ERROR("Failed to write CR0 mask to VMCS");
			return false;
		}

		// Omit VMX enable flag from CR4 read shadow
		if (!vmx::vmx_vmwrite(VMCS_CTRL_CR4_READ_SHADOW, __readcr4() & ~CR4_VMX_ENABLE_FLAG))
		{
			LOG_ERROR("Failed to write CR4 read shadow to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW, __readcr0()))
		{
			LOG_ERROR("Failed to write CR0 read shadow to VMCS");
			return false;
		}

		//
		// 26.6.7 CR3-Target Controls
		//

		// Do not exit for system CR3
		if (!vmx::vmx_vmwrite(VMCS_CTRL_CR3_TARGET_COUNT, 1))
		{
			LOG_ERROR("Failed to write CR3 target count to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_CTRL_CR3_TARGET_VALUE_0, g_hv.system_cr3.flags))
		{
			LOG_ERROR("Failed to write CR3 target value 0 to VMCS");
			return false;
		}

		//
		// 26.6.9 MSR-Bitmap Address
		//

		// Do not exit for any MSRs
		if (!vmx::vmx_vmwrite(VMCS_CTRL_MSR_BITMAP_ADDRESS, static_cast<uint64_t>(MmGetPhysicalAddress(&vcpu->msr_bitmap).QuadPart)))
		{
			LOG_ERROR("Failed to write MSR bitmap address to VMCS");
			return false;
		}

		//
		// 26.6.12 VPID
		//

		// Use 1 for guest VPID
		// TODO: Is there any benefit in using multiple VPIDs for a single guest ?
		if (!vmx::vmx_vmwrite(VMCS_CTRL_VIRTUAL_PROCESSOR_IDENTIFIER, 1))
		{
			LOG_ERROR("Failed to write VPID to VMCS");
			return false;
		}

		//
		// 26.6.20 XSS-Exiting Bitmap
		//

		// Do not exit for any XSS instructions
		if (!vmx::vmx_vmwrite(VMCS_CTRL_XSS_EXITING_BITMAP, 0ull))
		{
			LOG_ERROR("Failed to write XSS exiting bitmap to VMCS");
			return false;
		}

		//
		// 26.8.3 VM-Entry Controls for Event Injection
		//

		if (!vmx::vmx_vmwrite(VMCS_CTRL_VMENTRY_INTERRUPTION_INFORMATION_FIELD, 0))
		{
			LOG_ERROR("Failed to write VM-entry interruption information field to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_CTRL_VMENTRY_EXCEPTION_ERROR_CODE, 0))
		{
			LOG_ERROR("Failed to write VM-entry exception error code to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_CTRL_VMENTRY_INSTRUCTION_LENGTH, 0))
		{
			LOG_ERROR("Failed to write VM-entry instruction length to VMCS");
			return false;
		}

		return true;
	}

	static bool write_guest_state_fields(vcpu::vcpu* vcpu)
	{
		//
		// 26.4.1 Guest Register State
		//
		if (!vmx::vmx_vmwrite(VMCS_GUEST_CR0, __readcr0()))
		{
			LOG_ERROR("Failed to write guest CR0 to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_CR3, g_hv.system_cr3.flags))
		{
			LOG_ERROR("Failed to write guest CR3 to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_CR4, __readcr4()))
		{
			LOG_ERROR("Failed to write guest CR4 to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_DR7, __readdr(7)))
		{
			LOG_ERROR("Failed to write guest DR7 to VMCS");
			return false;
		}

		// RSP, RIP are set up at vmlaunch
		if (!vmx::vmx_vmwrite(VMCS_GUEST_RSP, 0ull))
		{
			LOG_ERROR("Failed to write guest RSP to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_RIP, 0ull))
		{
			LOG_ERROR("Failed to write guest RIP to VMCS");
			return false;
		}

		if (!vmx::vmx_vmwrite(VMCS_GUEST_RFLAGS, __readeflags()))
		{
			LOG_ERROR("Failed to write guest RFLAGS to VMCS");
			return false;
		}

		// Selectors for CS, SS, DS, ES, FS, GS, LDTR, and TR
		if (!vmx::vmx_vmwrite(VMCS_GUEST_CS_SELECTOR, utils::segment::read_cs().flags))
		{
			LOG_ERROR("Failed to write guest CS selector to VMCS");
			return false;
		}

		if (!vmx::vmx_vmwrite(VMCS_GUEST_SS_SELECTOR, utils::segment::read_ss().flags))
		{
			LOG_ERROR("Failed to write guest SS selector to VMCS");
			return false;
		}

		if (!vmx::vmx_vmwrite(VMCS_GUEST_DS_SELECTOR, utils::segment::read_ds().flags))
		{
			LOG_ERROR("Failed to write guest DS selector to VMCS");
			return false;
		}

		if (!vmx::vmx_vmwrite(VMCS_GUEST_ES_SELECTOR, utils::segment::read_es().flags))
		{
			LOG_ERROR("Failed to write guest ES selector to VMCS");
			return false;
		}

		if (!vmx::vmx_vmwrite(VMCS_GUEST_FS_SELECTOR, utils::segment::read_fs().flags))
		{
			LOG_ERROR("Failed to write guest FS selector to VMCS");
			return false;
		}

		if (!vmx::vmx_vmwrite(VMCS_GUEST_GS_SELECTOR, utils::segment::read_gs().flags))
		{
			LOG_ERROR("Failed to write guest GS selector to VMCS");
			return false;
		}

		if (!vmx::vmx_vmwrite(VMCS_GUEST_LDTR_SELECTOR, utils::segment::read_ldtr().flags))
		{
			LOG_ERROR("Failed to write guest LDTR selector to VMCS");
			return false;
		}

		if (!vmx::vmx_vmwrite(VMCS_GUEST_TR_SELECTOR, utils::segment::read_tr().flags))
		{
			LOG_ERROR("Failed to write guest TR selector to VMCS");
			return false;
		}

		segment_descriptor_register_64 gdt = {0};
		_sgdt(&gdt);
		segment_descriptor_register_64 idt = {0};
		__sidt(&idt);

		// Base addresses for CS, SS, DS, ES, FS, GS, LDTR, and TR
		if (!vmx::vmx_vmwrite(VMCS_GUEST_CS_BASE, utils::segment::base_address(gdt, utils::segment::read_cs())))
		{
			LOG_ERROR("Failed to write guest CS base address to VMCS");
			return false;
		}

		if (!vmx::vmx_vmwrite(VMCS_GUEST_SS_BASE, utils::segment::base_address(gdt, utils::segment::read_ss())))
		{
			LOG_ERROR("Failed to write guest SS base address to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_DS_BASE, utils::segment::base_address(gdt, utils::segment::read_ds())))
		{
			LOG_ERROR("Failed to write guest DS base address to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_ES_BASE, utils::segment::base_address(gdt, utils::segment::read_es())))
		{
			LOG_ERROR("Failed to write guest ES base address to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_FS_BASE, __readmsr(IA32_FS_BASE)))
		{
			LOG_ERROR("Failed to write guest FS base address to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_GS_BASE, __readmsr(IA32_GS_BASE)))
		{
			LOG_ERROR("Failed to write guest GS base address to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_LDTR_BASE, utils::segment::base_address(gdt, utils::segment::read_ldtr())))
		{
			LOG_ERROR("Failed to write guest LDTR base address to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_TR_BASE, utils::segment::base_address(gdt, utils::segment::read_tr())))
		{
			LOG_ERROR("Failed to write guest TR base address to VMCS");
			return false;
		}

		// Limits for CS, SS, DS, ES, FS, GS, LDTR, and TR
		if (!vmx::vmx_vmwrite(VMCS_GUEST_CS_LIMIT, utils::segment::limit(gdt, utils::segment::read_cs())))
		{
			LOG_ERROR("Failed to write guest CS limit to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_SS_LIMIT, utils::segment::limit(gdt, utils::segment::read_ss())))
		{
			LOG_ERROR("Failed to write guest SS limit to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_DS_LIMIT, utils::segment::limit(gdt, utils::segment::read_ds())))
		{
			LOG_ERROR("Failed to write guest DS limit to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_ES_LIMIT, utils::segment::limit(gdt, utils::segment::read_es())))
		{
			LOG_ERROR("Failed to write guest ES limit to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_FS_LIMIT, utils::segment::limit(gdt, utils::segment::read_fs())))
		{
			LOG_ERROR("Failed to write guest FS limit to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_GS_LIMIT, utils::segment::limit(gdt, utils::segment::read_gs())))
		{
			LOG_ERROR("Failed to write guest GS limit to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, utils::segment::limit(gdt, utils::segment::read_ldtr())))
		{
			LOG_ERROR("Failed to write guest LDTR limit to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_TR_LIMIT, utils::segment::limit(gdt, utils::segment::read_tr())))
		{
			LOG_ERROR("Failed to write guest TR limit to VMCS");
			return false;
		}

		// Access rights for CS, SS, DS, ES, FS, GS, LDTR, and TR
		if (!vmx::vmx_vmwrite(VMCS_GUEST_CS_ACCESS_RIGHTS, utils::segment::access_rights(gdt, utils::segment::read_cs()).flags))
		{
			LOG_ERROR("Failed to write guest CS access rights to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_SS_ACCESS_RIGHTS, utils::segment::access_rights(gdt, utils::segment::read_ss()).flags))
		{
			LOG_ERROR("Failed to write guest SS access rights to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_DS_ACCESS_RIGHTS, utils::segment::access_rights(gdt, utils::segment::read_ds()).flags))
		{
			LOG_ERROR("Failed to write guest DS access rights to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_ES_ACCESS_RIGHTS, utils::segment::access_rights(gdt, utils::segment::read_es()).flags))
		{
			LOG_ERROR("Failed to write guest ES access rights to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_FS_ACCESS_RIGHTS, utils::segment::access_rights(gdt, utils::segment::read_fs()).flags))
		{
			LOG_ERROR("Failed to write guest FS access rights to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_GS_ACCESS_RIGHTS, utils::segment::access_rights(gdt, utils::segment::read_gs()).flags))
		{
			LOG_ERROR("Failed to write guest GS access rights to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_LDTR_ACCESS_RIGHTS, utils::segment::access_rights(gdt, utils::segment::read_ldtr()).flags))
		{
			LOG_ERROR("Failed to write guest LDTR access rights to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_TR_ACCESS_RIGHTS, utils::segment::access_rights(gdt, utils::segment::read_tr()).flags))
		{
			LOG_ERROR("Failed to write guest TR access rights to VMCS");
			return false;
		}

		// GDTR, IDTR base and limit
		if (!vmx::vmx_vmwrite(VMCS_GUEST_GDTR_BASE, gdt.base_address))
		{
			LOG_ERROR("Failed to write guest GDTR base to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_GDTR_LIMIT, gdt.limit))
		{
			LOG_ERROR("Failed to write guest GDTR limit to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_IDTR_BASE, idt.base_address))
		{
			LOG_ERROR("Failed to write guest IDTR base to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_IDTR_LIMIT, idt.limit))
		{
			LOG_ERROR("Failed to write guest IDTR limit to VMCS");
			return false;
		}

		// MSRs
		if (!vmx::vmx_vmwrite(VMCS_GUEST_DEBUGCTL, __readmsr(IA32_DEBUGCTL)))
		{
			LOG_ERROR("Failed to write guest DEBUGCTL to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_SYSENTER_CS, __readmsr(IA32_SYSENTER_CS)))
		{
			LOG_ERROR("Failed to write guest SYSENTER_CS to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP)))
		{
			LOG_ERROR("Failed to write guest SYSENTER_ESP to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP)))
		{
			LOG_ERROR("Failed to write guest SYSENTER_EIP to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_PERF_GLOBAL_CTRL, __readmsr(IA32_PERF_GLOBAL_CTRL)))
		{
			LOG_ERROR("Failed to write guest PERF_GLOBAL_CTRL to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_PAT, __readmsr(IA32_PAT)))
		{
			LOG_ERROR("Failed to write guest PAT to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_EFER, __readmsr(IA32_EFER)))
		{
			LOG_ERROR("Failed to write guest EFER to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_GUEST_SMBASE, __readmsr(IA32_SMBASE)))
		{
			LOG_ERROR("Failed to write guest SMBASE to VMCS");
			return false;
		}

		// 26.4.2 Guest Non-Register State
		if (!vmx::vmx_vmwrite(VMCS_GUEST_ACTIVITY_STATE, vmx_active))
		{
			LOG_ERROR("Failed to write guest activity state to VMCS");
			return false;
		}

		if (!vmx::vmx_vmwrite(VMCS_GUEST_INTERRUPTIBILITY_STATE, vmx_interruptibility_state{0}.flags))
		{
			LOG_ERROR("Failed to write guest interruptibility state to VMCS");
			return false;
		}

		if (!vmx::vmx_vmwrite(VMCS_GUEST_PENDING_DEBUG_EXCEPTIONS, vmx_pending_debug_exceptions{0}.flags))
		{
			LOG_ERROR("Failed to write guest pending debug exceptions to VMCS");
			return false;
		}

		if (!vmx::vmx_vmwrite(VMCS_GUEST_VMCS_LINK_POINTER, MAXULONG_PTR))
		{
			LOG_ERROR("Failed to write guest VMCS link pointer to VMCS");
			return false;
		}
		return true;
	}

	static bool write_host_state_fields(vcpu::vcpu* vcpu)
	{
		//
		// 26.5.1 Host Register State
		//

		// CR0, CR3, CR4
		if (!vmx::vmx_vmwrite(VMCS_HOST_CR0, __readcr0()))
		{
			LOG_ERROR("Failed to write host CR0 to VMCS");
			return false;
		}
		cr3 host_cr3 = {0};
		host_cr3.address_of_page_directory = MmGetPhysicalAddress(&g_hv.host_page_tables.pml4e).QuadPart >> 12;
		if (!vmx::vmx_vmwrite(VMCS_HOST_CR3, host_cr3.flags))
		{
			LOG_ERROR("Failed to write host CR3 to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_HOST_CR4, __readcr4()))
		{
			LOG_ERROR("Failed to write host CR4 to VMCS");
			return false;
		}

		// 16 byte aligned RSP
		uint64_t host_rsp = (reinterpret_cast<uint64_t>(vcpu->host_stack) + vcpu::host_stack_size) & ~0xFULL;  // 16B align down

		// RSP, RIP
		if (!vmx::vmx_vmwrite(VMCS_HOST_RSP, host_rsp))
		{
			LOG_ERROR("Failed to write host RSP to VMCS");
			return false;
		}

		if (!vmx::vmx_vmwrite(VMCS_HOST_RIP, reinterpret_cast<uint64_t>(vmexit::vmexit_stub)))
		{
			LOG_ERROR("Failed to write host RIP to VMCS");
			return false;
		}

		// Segment selectors for CS, SS, DS, ES, FS, GS, and TR
		if (!vmx::vmx_vmwrite(VMCS_HOST_CS_SELECTOR, gdt::host_cs.flags))
		{
			LOG_ERROR("Failed to write host CS selector to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_HOST_SS_SELECTOR, gdt::host_ss.flags))
		{
			LOG_ERROR("Failed to write host SS selector to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_HOST_DS_SELECTOR, 0))
		{
			LOG_ERROR("Failed to write host DS selector to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_HOST_ES_SELECTOR, 0))
		{
			LOG_ERROR("Failed to write host ES selector to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_HOST_FS_SELECTOR, 0))
		{
			LOG_ERROR("Failed to write host FS selector to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_HOST_GS_SELECTOR, 0))
		{
			LOG_ERROR("Failed to write host GS selector to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_HOST_TR_SELECTOR, gdt::host_tr.flags))
		{
			LOG_ERROR("Failed to write host TR selector to VMCS");
			return false;
		}

		// Base for FS, GS, TR, GDTR, and IDTR (FS used for VCPU)
		if (!vmx::vmx_vmwrite(VMCS_HOST_FS_BASE, reinterpret_cast<uint64_t>(vcpu)))
		{
			LOG_ERROR("Failed to write host FS base to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_HOST_GS_BASE, 0))
		{
			LOG_ERROR("Failed to write host GS base to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_HOST_TR_BASE, reinterpret_cast<uint64_t>(&vcpu->host_tss)))
		{
			LOG_ERROR("Failed to write host TR base to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_HOST_GDTR_BASE, reinterpret_cast<uint64_t>(vcpu->host_gdt)))
		{
			LOG_ERROR("Failed to write host GDTR base to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_HOST_IDTR_BASE, reinterpret_cast<uint64_t>(vcpu->host_idt)))
		{
			LOG_ERROR("Failed to write host IDTR base to VMCS");
			return false;
		}

		// MSRs

		if (!vmx::vmx_vmwrite(VMCS_HOST_SYSENTER_CS, 0))
		{
			LOG_ERROR("Failed to write host SYSENTER_CS to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_HOST_SYSENTER_ESP, 0))
		{
			LOG_ERROR("Failed to write host SYSENTER_ESP to VMCS");
			return false;
		}
		if (!vmx::vmx_vmwrite(VMCS_HOST_SYSENTER_EIP, 0))
		{
			LOG_ERROR("Failed to write host SYSENTER_EIP to VMCS");
			return false;
		}

		if (!vmx::vmx_vmwrite(VMCS_HOST_PERF_GLOBAL_CTRL, 0))
		{
			LOG_ERROR("Failed to write host PERF_GLOBAL_CTRL to VMCS");
			return false;
		}

		ia32_efer_register efer = {0};
		efer.ia32e_mode_enable = 1;
		efer.ia32e_mode_active = 1;
		efer.syscall_enable = 1;
		efer.execute_disable_bit_enable = 1;
		if (!vmx::vmx_vmwrite(VMCS_HOST_EFER, efer.flags))
		{
			LOG_ERROR("Failed to write host EFER to VMCS");
			return false;
		}

		// Configure PAT according to 13.12.4 Programming the PAT, Memory Type Setting of PAT Entries Following a Power-up or Reset.
		// It shouldn't really matter whether we configure a "default after reset" PAT or copy the OS-configured PAT, since we should only care about regular system memory.
		// Keep in mind that this is not bulletproof, and it's best to avoid accessing potentially problematic ranges or pages.
		// See map_host_page_tables @ memory.cpp for additional implications.
		ia32_pat_register pat = {0};
		pat.pa0 = MEMORY_TYPE_WRITE_BACK;
		pat.pa1 = MEMORY_TYPE_WRITE_THROUGH;
		pat.pa1 = MEMORY_TYPE_WRITE_THROUGH;
		pat.pa2 = MEMORY_TYPE_UNCACHEABLE_MINUS;
		pat.pa3 = MEMORY_TYPE_UNCACHEABLE;
		pat.pa4 = MEMORY_TYPE_WRITE_BACK;
		pat.pa5 = MEMORY_TYPE_WRITE_THROUGH;
		pat.pa6 = MEMORY_TYPE_UNCACHEABLE_MINUS;
		pat.pa7 = MEMORY_TYPE_UNCACHEABLE;
		if (!vmx::vmx_vmwrite(VMCS_HOST_PAT, pat.flags))
		{
			LOG_ERROR("Failed to write host PAT to VMCS");
			return false;
		}

		// No PMCs
		ia32_perf_global_ctrl_register perf_global_ctrl = {0};
		if (!vmx::vmx_vmwrite(VMCS_HOST_PERF_GLOBAL_CTRL, perf_global_ctrl.flags))
		{
			LOG_ERROR("Failed to write host PERF_GLOBAL_CTRL to VMCS");
			return false;
		}

		return true;
	}

	bool write_control_field(uint64_t value, const uint64_t controlField, const uint64_t capMsr, const uint64_t trueCapMsr)
	{
		// A.3-A.5 Control Fields
		// @note: This function could be a lot shorter, however it's written this way to ensure easier understanding of the code's purpose.

		ia32_vmx_basic_register vmxBasic = {0};
		vmxBasic.flags = __readmsr(IA32_VMX_BASIC);

		if (vmxBasic.vmx_controls)
		{
			// In case true controls are supported, trueCapMsr reports all bits including the default1 or default0 reserved bits
			ia32_vmx_true_ctls_register trueCtls = {0};
			trueCtls.flags = __readmsr(trueCapMsr);

			value |= trueCtls.allowed_0_settings;
			value &= trueCtls.allowed_1_settings;
		}
		else
		{
			// In case true controls are not supported, capMsr reports all bits excluding the default1 reserved bits
			ia32_vmx_true_ctls_register capCtls = {0};
			capCtls.flags = __readmsr(capMsr);

			value |= capCtls.allowed_0_settings;
			value &= capCtls.allowed_1_settings;

			// default1 or default0 reserved bits are always set to 1 or 0 respectively, so we don't need to set them again here
		}

		if (!vmx::vmx_vmwrite(controlField, value))
		{
			return false;
		}

		return true;
	}
	bool load_vmcs(vcpu::vcpu* vcpu)
	{
		//
		// 26.1 Overview & 26.2 Format of the VMCS Region
		//

		ia32_vmx_basic_register vmxBasic = {0};
		vmxBasic.flags = __readmsr(IA32_VMX_BASIC);

		vcpu->vmcs_region.revision_id = vmxBasic.vmcs_revision_id;

		PHYSICAL_ADDRESS vmcsPhys = MmGetPhysicalAddress(reinterpret_cast<void*>(&vcpu->vmcs_region));
		if (!vmx::vmx_vmclear(vmcsPhys.QuadPart))
		{
			LOG_ERROR("Failed to clear VMCS region");
			return false;
		}

		if (!vmx::vmx_vmptrld(vmcsPhys.QuadPart))
		{
			LOG_ERROR("Failed to load VMCS region");
			return false;
		}

		return true;
	}

	bool write_control_fields(vcpu::vcpu* vcpu)
	{
		if (!write_pin_based_vm_execution_controls())
		{
			LOG_ERROR("Failed to write pin-based VM-execution controls to VMCS");
			return false;
		}

		if (!write_processor_based_vm_execution_controls())
		{
			LOG_ERROR("Failed to write processor-based VM-execution controls to VMCS");
			return false;
		}

		if (!write_vmexit_controls())
		{
			LOG_ERROR("Failed to write VM-exit controls to VMCS");
			return false;
		}

		if (!write_vmentry_controls())
		{
			LOG_ERROR("Failed to write VM-entry controls to VMCS");
			return false;
		}

		if (!write_other_control_fields(vcpu))
		{
			LOG_ERROR("Failed to write other control fields to VMCS");
			return false;
		}

		if (!write_guest_state_fields(vcpu))
		{
			LOG_ERROR("Failed to write guest state fields to VMCS");
			return false;
		}

		if (!write_host_state_fields(vcpu))
		{
			LOG_ERROR("Failed to write host state fields to VMCS");
			return false;
		}

		return true;
	}
}  // namespace hv::vmcs