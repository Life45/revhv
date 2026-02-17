#pragma once
using uint8_t = unsigned char;
using uint16_t = unsigned short;
using uint32_t = unsigned int;
using uint64_t = unsigned long long;

namespace hv::hypercall
{
	constexpr uint64_t HYPERCALL_KEY = 0x5245564856;  // "REVHV" ASCII

	enum hypercall_number : uint64_t
	{
		ping = 1,
		hypercall_max
	};
}  // namespace hv::hypercall