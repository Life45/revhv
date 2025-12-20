#pragma once
#include "sync.hpp"

#define LOG_VERBOSE(fmt, ...) logging::log_fmt_print(logging::log_level_verbose, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) logging::log_fmt_print(logging::log_level_info, fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) logging::log_fmt_print(logging::log_level_warning, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) logging::log_fmt_print(logging::log_level_error, fmt, ##__VA_ARGS__)

#define LOG_VERBOSE_DBGPRINT(fmt, ...) logging::log_fmt_dbgprint(logging::log_level_verbose, fmt, ##__VA_ARGS__)
#define LOG_INFO_DBGPRINT(fmt, ...) logging::log_fmt_dbgprint(logging::log_level_info, fmt, ##__VA_ARGS__)
#define LOG_WARNING_DBGPRINT(fmt, ...) logging::log_fmt_dbgprint(logging::log_level_warning, fmt, ##__VA_ARGS__)
#define LOG_ERROR_DBGPRINT(fmt, ...) logging::log_fmt_dbgprint(logging::log_level_error, fmt, ##__VA_ARGS__)

namespace logging
{
	enum log_level
	{
		log_level_verbose,
		log_level_info,
		log_level_warning,
		log_level_error
	};

	/// @brief Formats and prints a message to the serial port
	/// @note All string manipulation APIs used by this function can be called at any IRQL only if the string resides in nonpaged memory
	/// @param level The log level
	/// @param fmt The format string
	/// @param ... The arguments to the format string
	void log_fmt_print(log_level level, const char* fmt, ...);

	/// @brief Formats and prints a message to the debugger
	/// @note DbgPrintEx uses IPIs, this shouldn't be called from vmx-root context
	/// @param level The log level
	/// @param fmt The format string
	/// @param ... The arguments to the format string
	void log_fmt_dbgprint(log_level level, const char* fmt, ...);
}  // namespace logging