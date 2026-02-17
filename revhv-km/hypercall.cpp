#include "hypercall.h"

namespace hv::hypercall
{
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
			LOG_INFO("Received ping hypercall from guest");
			guest_context->rax = 1;	 // Return 1
			break;
		default:
			LOG_WARNING("Received unknown hypercall number: %llu", guest_context->rcx);
			break;
		}

		return true;
	}
}  // namespace hv::hypercall