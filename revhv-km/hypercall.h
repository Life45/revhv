#pragma once
#include "includes.h"
#include "../common/hypercall_types.hpp"
#include "vcpu.h"

namespace hv::hypercall
{
	/// @brief Handles a hypercall from the guest
	/// @param guest_context Guest context at the time of the hypercall
	/// @param vcpu Pointer to the vCPU structure
	/// @return True if the hypercall key matches, false otherwise(caller should inject UD in that case)
	bool handle_hypercall(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu);

}  // namespace hv::hypercall