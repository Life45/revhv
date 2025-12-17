#pragma once
#include "includes.h"
#include "vcpu.h"

namespace hv::vmexit
{
	/// @brief VMEXIT stub, entry point for VMEXITs
	/// @note: Defined in vmexit_stub.asm
	extern "C" void vmexit_stub();

	/// @brief Handles a VMEXIT
	/// @param guest_context Pointer to the guest context
	/// @param vcpu Pointer to the vcpu
	void handler(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu);
}  // namespace hv::vmexit