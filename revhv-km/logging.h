#pragma once
#include "../common/logging_types.hpp"
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

	/// @brief A circular logger that stores formatted log messages synchronized with a spin lock.
	struct standard_logger
	{
		sync::reentrant_spin_lock spinlock;
		standard_log_message messages[max_standard_message_count];
		size_t cursor;
		// Lifetime count of all messages written
		size_t message_count;
		// Number of messages currently buffered in the ring and pending flush
		size_t pending_count;
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

	/// @brief Flushes the standard log messages into the provided buffer and returns the number of messages flushed.
	/// @param out_messages Pointer to an array of standard_log_message to flush the messages into
	/// @param max_messages The maximum number of messages to flush (size of the out_messages array)
	/// @param guest_memory Whether out_messages points to guest memory (in which case introspection functions will be used to copy memory safely)
	/// @return The number of messages flushed into the out_messages array
	size_t flush_standard_logs(standard_log_message* out_messages, size_t max_messages, bool guest_memory = true);

}  // namespace logging