#pragma once

#include "includes.h"
#include "kmodules.h"
#include "trace_poller.hpp"
#include "../common/trace_cfg_export.hpp"

#include <atomic>
#include <thread>

namespace commands
{
	class engine
	{
	public:
		enum class dump_kind : uint8_t
		{
			bytes,
			words,
			dwords,
			qwords,
			pointers
		};

		explicit engine(kmodule_context& modules);

		bool execute_line(const std::string& line);
		void print_general_help() const;

		/// @brief Stops the trace poller (if running). Call before exit.
		void stop_trace_poller();

	private:
		kmodule_context& m_modules;
		trace::poller m_trace_poller;

		// Snapshot thread for onload auto-trace: waits for first trace data, then polls until the target driver appears in the module list.
		std::thread m_onload_snapshot_thread;
		std::atomic_bool m_snapshot_triggered{false};

		// EPT transition config state, mirrored from per-vCPU KM state for local export
		trace::ept_transition_cfg m_generic_transition_cfg = trace::default_generic_cfg;
		std::string m_generic_transition_fmt;

		struct exact_cfg_entry
		{
			uint64_t addr;
			trace::ept_transition_cfg cfg;
			std::string fmt;
		};
		std::vector<exact_cfg_entry> m_exact_transition_cfgs;

		void print_help_for(const std::string& command_name) const;

		bool handle_dump(const std::vector<std::string>& args, dump_kind kind);
		bool handle_ln(const std::vector<std::string>& args);
		bool handle_lm(const std::vector<std::string>& args);
		bool handle_at(const std::vector<std::string>& args);
		bool handle_at_config(const std::vector<std::string>& args);
		bool handle_at_onload(const std::vector<std::string>& args);
		bool handle_trace_parse(const std::vector<std::string>& args);
		bool handle_apic_info(const std::vector<std::string>& args);
		bool handle_test_df(const std::vector<std::string>& args);

		/// @brief Serializes the current trace config to a trace_cfg.bin file.
		bool export_trace_config(const std::filesystem::path& path) const;

		/// @brief Returns true if a command that requires the hypervisor can proceed.
		/// Prints an error and returns false when the HV is absent.
		static bool require_hv(const std::string& command_name);

		bool parse_expression(const std::string& token, uint64_t& value) const;
		static bool parse_u64_token(const std::string& token, uint64_t& value);
		static std::vector<std::string> tokenize(const std::string& line);
	};
}  // namespace commands
