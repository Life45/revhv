#include "includes.h"
#include "hypercall.h"
#include "logger.hpp"
#include "utils.hpp"

int main()
{
	logger::info("revhv-um started");

	bool fail = false;
	utils::for_each_cpu(
		[&fail](size_t i)
		{
			if (hv::hypercall::ping_hv())
			{
				logger::info("HV alive on core {}", i);
			}
			else
			{
				logger::error("Failed to receive response from hypervisor on CPU core {}", i);
				fail = true;
			}
		});

	if (fail)
	{
		logger::error("Some CPU cores failed to communicate with the hypervisor.");
		return -1;
	}

	while (true)
	{
		std::vector<logging::standard_log_message> messages(100);
		if (hv::hypercall::flush_std_logs(messages))
		{
			logger::info("Flushed {} standard log messages from the hypervisor:", messages.size());
			for (const auto& msg : messages)
			{
				logger::info("Log #{}: {}", msg.message_number, msg.text);
			}
		}
		else
		{
			logger::error("Failed to flush standard logs from the hypervisor.");
			return -1;
		}

		Sleep(5000);
	}

	return 0;
}