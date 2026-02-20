#include "hypercall.h"

namespace hv::hypercall
{
	bool ping_hv()
	{
		__try
		{
			return __vmcall(hypercall_number::ping, 0, 0, 0) == 1;
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
}  // namespace hv::hypercall