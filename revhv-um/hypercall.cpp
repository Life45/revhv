#include "hypercall.h"
#include <algorithm>
#include "utils.hpp"

namespace hv::hypercall
{
	bool ping_hv()
	{
		__try
		{
			return __vmcall(hypercall_number::ping) == 1;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			// #UD if the hypercall key is invalid or if VMX is not supported/enabled
			return false;
		}
	}

	bool flush_std_logs(std::vector<logging::standard_log_message>& messages)
	{
		const size_t max_messages = messages.size();
		auto buffer = messages.data();
		size_t flushed = 0;

		__try
		{
			flushed = __vmcall(hypercall_number::flush_standard_logs, reinterpret_cast<uint64_t>(buffer), max_messages);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}

		messages.resize(flushed);
		return true;
	}

	size_t read_vmemory(uint64_t target_va, void* out_buffer, size_t size, uint64_t target_cr3)
	{
		if (!out_buffer || size == 0)
		{
			return 0;
		}

		auto output = static_cast<uint8_t*>(out_buffer);
		size_t total_read = 0;

		while (total_read < size)
		{
			const size_t chunk_size = (std::min)(size - total_read, static_cast<size_t>(MAX_VMEM_CHUNK_SIZE));

			vmem_request request{};
			request.guest_buffer = reinterpret_cast<uint64_t>(output + total_read);
			request.target_va = target_va + total_read;
			request.size = chunk_size;
			request.target_cr3 = target_cr3;

			uint64_t bytes_read = 0;
			__try
			{
				bytes_read = __vmcall(hypercall_number::read_vmem, reinterpret_cast<uint64_t>(&request));
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				break;
			}

			if (bytes_read == 0)
			{
				break;
			}

			total_read += static_cast<size_t>(bytes_read);
			if (bytes_read < chunk_size)
			{
				break;
			}
		}

		return total_read;
	}

	size_t write_vmemory(uint64_t target_va, const void* in_buffer, size_t size, uint64_t target_cr3)
	{
		if (!in_buffer || size == 0)
		{
			return 0;
		}

		auto input = static_cast<const uint8_t*>(in_buffer);
		size_t total_written = 0;

		while (total_written < size)
		{
			const size_t chunk_size = (std::min)(size - total_written, static_cast<size_t>(MAX_VMEM_CHUNK_SIZE));

			vmem_request request{};
			request.guest_buffer = reinterpret_cast<uint64_t>(input + total_written);
			request.target_va = target_va + total_written;
			request.size = chunk_size;
			request.target_cr3 = target_cr3;

			uint64_t bytes_written = 0;
			__try
			{
				bytes_written = __vmcall(hypercall_number::write_vmem, reinterpret_cast<uint64_t>(&request));
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				break;
			}

			if (bytes_written == 0)
			{
				break;
			}

			total_written += static_cast<size_t>(bytes_written);
			if (bytes_written < chunk_size)
			{
				break;
			}
		}

		return total_written;
	}

	bool auto_trace_enable(uint64_t target_va, size_t target_size)
	{
		auto success = true;
		utils::for_each_cpu(
			[&](const uint32_t cpu)
			{
				__try
				{
					if (!__vmcall(hypercall_number::enable_auto_trace, target_va, target_size))
					{
						success = false;
					}
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					success = false;
				}
			});
		return success;
	}

	bool auto_trace_disable()
	{
		bool success = true;
		utils::for_each_cpu(
			[&](const uint32_t cpu)
			{
				__try
				{
					__vmcall(hypercall_number::disable_auto_trace);
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					success = false;
				}
			});
		return success;
	}

	uint64_t drain_trace_logs(uint32_t core_id, trace::entry* out, uint64_t max_entries)
	{
		__try
		{
			return __vmcall(hypercall_number::flush_trace_logs, static_cast<uint64_t>(core_id), reinterpret_cast<uint64_t>(out), max_entries);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return 0;
		}
	}
}  // namespace hv::hypercall