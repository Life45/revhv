#pragma once

#include "includes.h"
#include "kmodules.h"
#include "trace_poller.hpp"

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

		void print_help_for(const std::string& command_name) const;

		bool handle_dump(const std::vector<std::string>& args, dump_kind kind);
		bool handle_ln(const std::vector<std::string>& args);
		bool handle_lm(const std::vector<std::string>& args);
		bool handle_at(const std::vector<std::string>& args);
		bool handle_trace_parse(const std::vector<std::string>& args);

		/// @brief Returns true if a command that requires the hypervisor can proceed.
		/// Prints an error and returns false when the HV is absent.
		static bool require_hv(const std::string& command_name);

		bool parse_expression(const std::string& token, uint64_t& value) const;
		static bool parse_u64_token(const std::string& token, uint64_t& value);
		static std::vector<std::string> tokenize(const std::string& line);
	};
}  // namespace commands
