#include "includes.h"
#include "hypercall.h"
#include "logger.hpp"
#include "utils.hpp"
#include "kmodules.h"
#include "commands.h"
#include <atomic>
#include <iostream>

//
// Global hypervisor presence flag
//
namespace hv
{
	/// @brief True when the hypervisor responded on every logical core during startup.
	/// Commands that issue hypercalls must check this before executing.
	static std::atomic_bool g_hv_present{false};

	bool is_present()
	{
		return g_hv_present.load(std::memory_order_relaxed);
	}
}  // namespace hv

void flush_thread(std::atomic_bool& stop_requested)
{
	std::ofstream log_file("hv_logs.txt", std::ios::out);
	if (!log_file.is_open())
	{
		logger::error("Failed to open hv_logs.txt for writing hypervisor logs.");
		return;
	}

	while (!stop_requested.load(std::memory_order_relaxed))
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
			std::this_thread::sleep_for(1s);
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

	bool all_cores_ok = true;
	utils::for_each_cpu(
		[&all_cores_ok](size_t i)
		{
			if (hv::hypercall::ping_hv())
			{
				logger::info("HV alive on core {}", i);
			}
			else
			{
				logger::error("Failed to receive response from hypervisor on CPU core {}", i);
				all_cores_ok = false;
			}
		});

	hv::g_hv_present.store(all_cores_ok, std::memory_order_relaxed);

	if (!all_cores_ok)
	{
		logger::warn("Hypervisor not detected on all cores. "
					 "Commands that require the hypervisor (memory dumps, auto-trace, etc.) are disabled. "
					 "Offline commands (ln, lm, trace parse, etc.) remain available.");
	}
	else
	{
		std::cout << "Spawn a PowerShell terminal to monitor hv_logs.txt? [y/N]: ";
		std::string answer;
		std::getline(std::cin, answer);
		if (!answer.empty() && (answer[0] == 'y' || answer[0] == 'Y'))
		{
			std::string log_path = std::filesystem::absolute("hv_logs.txt").string();
			std::string cmd = "start powershell.exe -NoExit -Command \"Get-Content -Path '" + log_path + "' -Wait\"";
			system(cmd.c_str());
		}
	}

	kmodule_context kmodules;
	kmodules.refresh();

	commands::engine cmd_engine(kmodules);
	cmd_engine.print_general_help();

	// Only start the HV log flush thread when the hypervisor is present
	std::atomic_bool stop_flush_thread = false;
	std::thread log_flush_thread;
	if (hv::is_present())
		log_flush_thread = std::thread(flush_thread, std::ref(stop_flush_thread));

	while (true)
	{
		std::cout << "revhv> " << std::flush;

		std::string line;
		if (!std::getline(std::cin, line))
			break;

		if (!cmd_engine.execute_line(line))
			break;
	}

	stop_flush_thread.store(true, std::memory_order_relaxed);
	if (log_flush_thread.joinable())
		log_flush_thread.join();

	// Ensure the trace poller is stopped on exit (may already be stopped by 'at disable')
	cmd_engine.stop_trace_poller();

	return 0;
}