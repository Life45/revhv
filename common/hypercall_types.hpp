#pragma once
using uint8_t = unsigned char;
using uint16_t = unsigned short;
using uint32_t = unsigned int;
using uint64_t = unsigned long long;

namespace hv::hypercall
{
	constexpr uint64_t HYPERCALL_KEY = 0x5245564856;  // "REVHV" ASCII

	constexpr size_t MAX_VMEM_CHUNK_SIZE = 0x1000;

	// Structure for a virtual memory read/write hypercall requests (Since 5 arguments with the hypercall id don't fit in registers)
	struct vmem_request
	{
		uint64_t guest_buffer;
		uint64_t target_va;
		size_t size;
		uint64_t target_cr3;
	};

	enum hypercall_number : uint64_t
	{
		ping = 1,
		flush_standard_logs,
		read_vmem,
		write_vmem,
		hypercall_max
	};
}  // namespace hv::hypercall