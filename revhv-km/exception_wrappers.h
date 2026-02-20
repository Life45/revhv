#pragma once
#include "includes.h"

// All wrappers are defined in exception_wrappers.asm

namespace hv::exception_wrappers
{
	extern "C"
	{
		uint64_t rdmsr_wrapper(uint32_t msr);
		void wrmsr_wrapper(uint32_t msr, uint64_t value);
		void invd_wrapper();
		void xsetbv_wrapper(uint32_t index, uint64_t value);
		void memcpy_wrapper(void* dest, const void* src, size_t size);
	}
}  // namespace hv::exception_wrappers