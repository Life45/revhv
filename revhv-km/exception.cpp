#include "exception.h"
#include "vcpu.h"
#include "vmcs.h"
#include "utils.hpp"
#include "vmx.h"

namespace hv::exception
{
	static void log_trap_frame(const trap_frame* trap_frame)
	{
		LOG_ERROR("Trap frame: \nRAX: %llx \nRCX: %llx \nRDX: %llx \nRBX: %llx \nRBP: %llx \nRSI: %llx \nRDI: %llx \nR8: %llx \nR9: %llx \nR10: %llx \nR11: %llx \nR12: %llx \nR13: %llx \nR14: %llx \nR15: %llx \nVector: %llx \nMachine frame: \nError code: %llx \nSS: %llx \nRSP: %llx \nRFLAGS: %llx \nCS: %llx \nRIP: %llx", trap_frame->rax, trap_frame->rcx, trap_frame->rdx, trap_frame->rbx, trap_frame->rbp, trap_frame->rsi, trap_frame->rdi, trap_frame->r8, trap_frame->r9, trap_frame->r10,
				  trap_frame->r11, trap_frame->r12, trap_frame->r13, trap_frame->r14, trap_frame->r15, trap_frame->vector, trap_frame->machine_frame.error_code, trap_frame->machine_frame.ss, trap_frame->machine_frame.rsp, trap_frame->machine_frame.rflags, trap_frame->machine_frame.cs, trap_frame->machine_frame.rip);
	}

	static void handle_generic(trap_frame* trap_frame, vcpu::vcpu* vcpu, bool expected_exception)
	{
		if (!expected_exception)
		{
			LOG_ERROR("Unhandled host exception with vector %llx occurred on vCPU %lu", trap_frame->vector, vcpu->core_id);
			log_trap_frame(trap_frame);
			// TODO: Fail appropriately
			__halt();
		}

		LOG_ERROR("An expected exception occurred on vCPU %lu, vector: %llx", vcpu->core_id, trap_frame->vector);
		LOG_ERROR("Generic exceptions cannot be handled by an exception handler. Add a specific handler for this exception vector.");
		log_trap_frame(trap_frame);
		// TODO: Fail appropriately
		__halt();
	}

	static void handle_gp(trap_frame* trap_frame, vcpu::vcpu* vcpu, bool expected_exception)
	{
		if (!expected_exception)
		{
			LOG_ERROR("Unhandled host general protection exception occurred on vCPU %lu", vcpu->core_id);
			log_trap_frame(trap_frame);
			// TODO: Fail appropriately
			__halt();
		}

		vcpu->exception_info.exception_occurred = true;
		vcpu->exception_info.exception_vector = trap_frame->vector;
		vcpu->exception_info.error_code = trap_frame->machine_frame.error_code;

		// Execution is resumed at RDI
		trap_frame->machine_frame.rip = trap_frame->rdi;
	}

	static void handle_ud(trap_frame* trap_frame, vcpu::vcpu* vcpu, bool expected_exception)
	{
		if (!expected_exception)
		{
			LOG_ERROR("Unhandled host invalid opcode exception occurred on vCPU %lu", vcpu->core_id);
			log_trap_frame(trap_frame);
			// TODO: Fail appropriately
			__halt();
		}

		vcpu->exception_info.exception_occurred = true;
		vcpu->exception_info.exception_vector = trap_frame->vector;
		vcpu->exception_info.error_code = trap_frame->machine_frame.error_code;

		// Execution is resumed at RDI
		trap_frame->machine_frame.rip = trap_frame->rdi;
	}

	static void handle_df(trap_frame* trap_frame, vcpu::vcpu* vcpu, bool expected_exception)
	{
		LOG_ERROR("Host double fault occurred on vCPU %lu, vector: %llx", vcpu->core_id, trap_frame->vector);
		log_trap_frame(trap_frame);

		// Restore context and bugcheck
		vmx::vmx_vmxoff();

		LOG_INFO("Restoring CR3");

		// Restore CR3 and PAT
		__writecr3(vcpu->restore_context.cr3);
		__writemsr(IA32_PAT, vcpu->restore_context.pat);

		// Flush TLB
		__wbinvd();	 // Write back and invalidate cache
		auto cr4 = __readcr4();
		__writecr4(cr4 & ~(1ULL << 7));	 // Clear PGE flag
		__writecr4(cr4);				 // Set PGE flag again

		LOG_INFO("Restoring MSRs");

		// Restore other MSRs
		__writemsr(IA32_EFER, vcpu->restore_context.efer);
		__writemsr(IA32_SYSENTER_CS, vcpu->restore_context.sysenter_cs);
		__writemsr(IA32_SYSENTER_ESP, vcpu->restore_context.sysenter_esp);
		__writemsr(IA32_SYSENTER_EIP, vcpu->restore_context.sysenter_eip);

		LOG_INFO("Restoring GDT and IDT");
		// Restore GDT and IDT
		_lgdt(&vcpu->restore_context.gdtr);
		__lidt(&vcpu->restore_context.idtr);

		LOG_INFO("Restoring segments");
		// Restore segments
		LOG_INFO("Restoring DS");
		utils::segment::write_ds(vcpu->restore_context.ds.flags);
		LOG_INFO("Restoring ES");
		utils::segment::write_es(vcpu->restore_context.es.flags);
		LOG_INFO("Restoring FS");
		utils::segment::write_fs(vcpu->restore_context.fs.flags);
		LOG_INFO("Restoring GS");
		utils::segment::write_gs(vcpu->restore_context.gs.flags);

		// Restore MSR-based segment bases
		__writemsr(IA32_FS_BASE, vcpu->restore_context.fs_base);
		__writemsr(IA32_GS_BASE, vcpu->restore_context.gs_base);
		__writemsr(IA32_KERNEL_GS_BASE, vcpu->restore_context.kernel_gs_base);

		LOG_INFO("Bugchecking");
		KeBugCheckEx(UNEXPECTED_KERNEL_MODE_TRAP, EXCEPTION_DOUBLE_FAULT, 0xDEADDEAD, reinterpret_cast<ULONG_PTR>(trap_frame), vcpu->core_id);
	}

	static void handle_mc(trap_frame* trap_frame, vcpu::vcpu* vcpu, bool expected_exception)
	{
		// TODO: More information about machine check
		LOG_ERROR("Host machine check occurred on vCPU %lu, vector: %llx", vcpu->core_id, trap_frame->vector);
		log_trap_frame(trap_frame);
		// TODO: Fail appropriately
		__halt();
	}

	static void handle_nmi(trap_frame* trap_frame, vcpu::vcpu* vcpu, bool expected_exception)
	{
		LOG_ERROR("Host NMI occurred on vCPU %lu, vector: %llx", vcpu->core_id, trap_frame->vector);

		// Enqueue the NMI to be injected into the guest when NMIs are unblocked
		vcpu->queued_nmi_count++;

		// Enable NMI window exiting
		vmcs::enable_nmi_window_exiting();
	}

	void handle_exception(trap_frame* trap_frame, vcpu::vcpu* vcpu)
	{
		LOG_ERROR("Exception occurred on vCPU %lu, vector: %llx", vcpu->core_id, trap_frame->vector);

		// It's an expected exception if RSI is set to the RIP of the instruction that caused the exception
		bool expected_exception = trap_frame->rsi == trap_frame->machine_frame.rip;

		switch (trap_frame->vector)
		{
		case double_fault:
			handle_df(trap_frame, vcpu, expected_exception);
			break;
		case general_protection:
			handle_gp(trap_frame, vcpu, expected_exception);
			break;
		case invalid_opcode:
			handle_ud(trap_frame, vcpu, expected_exception);
			break;
		case machine_check:
			handle_mc(trap_frame, vcpu, expected_exception);
			break;
		case nmi:
			handle_nmi(trap_frame, vcpu, expected_exception);
			break;
		case divide_error:
		case debug:
		case breakpoint:
		case overflow:
		case bound_range_exceeded:
		case device_not_available:
		case invalid_tss:
		case segment_not_present:
		case stack_segment_fault:
		case page_fault:
		case x87_floating_point_error:
		case alignment_check:
		case simd_floating_point_error:
		case virtualization_exception:
		case control_protection:
			handle_generic(trap_frame, vcpu, expected_exception);
			break;
		default:
			LOG_ERROR("Unknown exception vector: %llx", trap_frame->vector);
			log_trap_frame(trap_frame);
			// TODO: Fail appropriately
			__halt();
			break;
		}
	}
}  // namespace hv::exception