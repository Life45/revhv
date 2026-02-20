#include "hypercall.h"

namespace hv::hypercall
{
	static void handle_ping(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		LOG_INFO("Received ping hypercall from guest");
		guest_context->rax = 1;	 // Return 1
	}

	static void handle_flush_standard_logs(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// RDX is the pointer to the buffer in guest memory, and R8 is the maximum number of messages to flush
		auto guest_buffer = reinterpret_cast<logging::standard_log_message*>(guest_context->rdx);
		size_t max_messages = guest_context->r8;

		const size_t flushed = logging::flush_standard_logs(guest_buffer, max_messages, true);
		guest_context->rax = flushed;  // Return the number of messages flushed
	}

	bool handle_hypercall(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// Rax must be the hypercall key, otherwise it's not a valid hypercall and we should inject #UD
		if (guest_context->rax != HYPERCALL_KEY)
		{
			return false;
		}

		switch (guest_context->rcx)
		{
		case hypercall_number::ping:
			handle_ping(guest_context, vcpu);
			break;
		case hypercall_number::flush_standard_logs:
			handle_flush_standard_logs(guest_context, vcpu);
			break;
		default:
			LOG_WARNING("Received unknown hypercall number: %llu", guest_context->rcx);
			break;
		}

		return true;
	}
}  // namespace hv::hypercall