#include "includes.h"
#include "hypercall.h"
#include "logger.hpp"
#include "utils.hpp"
#include "kmodules.h"
#include <iostream>

void flush_thread()
{
	std::ofstream log_file("hv_logs.txt", std::ios::out);
	if (!log_file.is_open())
	{
		logger::error("Failed to open hv_logs.txt for writing hypervisor logs.");
		return;
	}

	while (true)
	{
		std::vector<logging::standard_log_message> messages(100);
		if (hv::hypercall::flush_std_logs(messages))
		{
			for (const auto& msg : messages)
			{
				log_file << std::format("#{}: {}", msg.message_number, msg.text);
			}

			log_file.flush();
		}
		else
		{
			logger::error("Failed to flush standard logs from the hypervisor.");
			return;
		}

		// If the buffer was utilized fully, flush again immediately to avoid missing messages
		if (messages.size() < 100)
		{
			std::this_thread::sleep_for(5s);
		}
	}
}

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
		logger::error("Some cores failed to communicate with the hypervisor.");
		return -1;
	}

	std::cout << "Spawn a PowerShell terminal to monitor hv_logs.txt? [y/N]: ";
	std::string answer;
	std::getline(std::cin, answer);
	if (!answer.empty() && (answer[0] == 'y' || answer[0] == 'Y'))
	{
		std::string log_path = std::filesystem::absolute("hv_logs.txt").string();
		std::string cmd = "start powershell.exe -NoExit -Command \"Get-Content -Path '" + log_path + "' -Wait\"";
		system(cmd.c_str());
	}

	// Start the log flushing thread
	std::thread log_flush_thread(flush_thread);
	log_flush_thread.detach();

	return 0;
}