#include "exception.h"
#include "vcpu.h"

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
		// TODO: Fail appropriately
		__halt();
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
		// TODO: Handle NMIs properly
		LOG_ERROR("Host NMI occurred on vCPU %lu, vector: %llx", vcpu->core_id, trap_frame->vector);
		log_trap_frame(trap_frame);
		// TODO: Fail appropriately
		__halt();
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