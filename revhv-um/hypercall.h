#pragma once
#include "includes.h"
#include "../common/hypercall_types.hpp"

namespace hv::hypercall
{
	/// @brief Executes VMCALL instruction
	/// @param number hypercall number
	/// @param arg1 argument 1
	/// @param arg2 argument 2
	/// @param arg3 argument 3
	/// @param key hypercall key (should be HYPERCALL_KEY for a valid hypercall)
	/// @return result of the hypercall
	extern "C" uint64_t __fastcall __vmcall(uint64_t number, uint64_t arg1 = 0, uint64_t arg2 = 0, uint64_t arg3 = 0, uint64_t key = HYPERCALL_KEY);

	/// @brief Pings the hv from the current core
	/// @return True if the hv responded correctly, false otherwise
	bool ping_hv();
}  // namespace hv::hypercall