#pragma once
#include "vcpu.h"

namespace hv::error
{
	/// @brief Raises a host crash (bugcheck) from the specified vCPU, sync all other vCPUs to acknowledge the crash
	/// @param vcpu Pointer to the vCPU structure
	void __declspec(noreturn) unrecoverable_host_error(vcpu::vcpu* vcpu);

	/// @brief Devirtualizes the vCPU and gives control back to the host OS IF an unrecoverable error has occurred, otherwise no op.
	/// @param vcpu Pointer to the vCPU structure
	void give_control_on_unrecoverable_error(vcpu::vcpu* vcpu);
}  // namespace hv::error