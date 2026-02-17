#pragma once
#include "includes.h"
#include <stdexcept>
#include <string>

namespace utils
{
	template <typename Func>
	inline void for_each_cpu(Func func)
	{
		SYSTEM_INFO info = {};
		GetSystemInfo(&info);

		for (uint32_t i = 0; i < info.dwNumberOfProcessors; ++i)
		{
			auto const prev_affinity = SetThreadAffinityMask(GetCurrentThread(), 1ull << i);
			func(i);
			SetThreadAffinityMask(GetCurrentThread(), prev_affinity);
		}
	}
}  // namespace utils