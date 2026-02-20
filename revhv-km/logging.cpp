#include "logging.h"
#include "serial.h"
#include "format.h"
#include <stdarg.h>
#include "sync.hpp"
#include "hv.h"
#include "introspection.h"

namespace logging
{
	// Lock for serial port access (reentrant so nested/recursive logging from the same core cannot deadlock)
	sync::reentrant_spin_lock serial_lock;

	static int format_to(char* buffer, size_t bufferSize, const char* fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		const int written = format::format(fmt, buffer, bufferSize, args);
		va_end(args);
		return written;
	}

	static bool log_fmt(char* buffer, size_t bufferSize, log_level level, const char* fmt, va_list args, size_t* out_size = nullptr)
	{
		const char* level_str;

		switch (level)
		{
		case log_level_verbose:
			level_str = "VERBOSE";
			break;
		case log_level_info:
			level_str = "INFO";
			break;
		case log_level_warning:
			level_str = "WARNING";
			break;
		case log_level_error:
			level_str = "ERROR";
			break;
		default:
			level_str = "UNKNOWN";
			break;
		}

		if (!buffer || bufferSize == 0)
		{
			return false;
		}

		// Prefix: [revhv] [LEVEL]
		const int prefix_len = format_to(buffer, bufferSize, "[revhv] [%s] ", level_str);
		if (prefix_len <= 0)
		{
			return false;
		}

		const size_t offset = static_cast<size_t>(prefix_len);
		if (offset >= bufferSize)
		{
			return false;
		}

		// Append message
		(void)format::format(fmt, buffer + offset, bufferSize - offset, args);

		// Append newline (best-effort, keep buffer NUL-terminated)
		size_t end = offset;
		while (end < bufferSize && buffer[end] != '\0')
		{
			++end;
		}

		if (end + 1 < bufferSize)
		{
			buffer[end++] = '\n';
			buffer[end] = '\0';
		}

		if (out_size)
		{
			*out_size = end;
		}

		return true;
	}

	static void write_to_standard_log(const char* message, size_t message_size)
	{
		sync::scoped_reentrant_spin_lock lock(hv::g_hv.logger.spinlock);

		auto& logger = hv::g_hv.logger;
		constexpr size_t max_message_length = max_standard_message_length;
		constexpr size_t max_message_count = max_standard_message_count;

		const size_t cursor = logger.cursor;
		standard_log_message& dst = logger.messages[cursor];

		dst.message_number = logger.message_count;
		++logger.message_count;

		if (!message || message_size == 0)
		{
			dst.text[0] = '\0';
		}
		else
		{
			const size_t copy_size = min(message_size, max_message_length - 1);
			utils::memcpy(dst.text, message, copy_size);
			dst.text[copy_size] = '\0';
		}

		logger.cursor = (cursor + 1) % max_message_count;
		logger.pending_count = min(logger.pending_count + 1, max_message_count);
	}

	void log_fmt_print(log_level level, const char* fmt, ...)
	{
		char buffer[max_standard_message_length] = {0};

		va_list args;
		va_start(args, fmt);
		size_t formatted_size = 0;
		if (!log_fmt(buffer, sizeof(buffer), level, fmt, args, &formatted_size))
		{
			constexpr char error_msg[] = "[revhv] [ERROR] Failed to format log message\n";
			utils::memcpy(buffer, error_msg, sizeof(error_msg));
			formatted_size = sizeof(error_msg) - 1;
		}
		va_end(args);

		if (hv::serial_output_enabled)
		{
			sync::scoped_reentrant_spin_lock lock(serial_lock);
			serial::write_string(serial::SERIAL_COM_1, buffer);
		}

		write_to_standard_log(buffer, formatted_size);
	}

	void log_fmt_dbgprint(log_level level, const char* fmt, ...)
	{
		char buffer[512] = {0};
		va_list args;
		va_start(args, fmt);
		if (!log_fmt(buffer, sizeof(buffer), level, fmt, args))
		{
			DbgPrintEx(0, 0, "%s", "[revhv] [ERROR] Failed to format log message\n");
			va_end(args);
			return;
		}
		va_end(args);

		DbgPrintEx(0, 0, "%s", buffer);
	}

	size_t flush_standard_logs(standard_log_message* out_messages, size_t max_messages, bool guest_memory)
	{
		// TODO: Think about what happens if a host NMI hits while inside this function
		if (!out_messages || max_messages == 0)
		{
			return 0;
		}

		sync::scoped_reentrant_spin_lock lock(hv::g_hv.logger.spinlock);

		auto& logger = hv::g_hv.logger;
		constexpr size_t max_message_count = max_standard_message_count;

		const size_t available = logger.pending_count;
		const size_t flush_count = min(available, max_messages);
		if (flush_count == 0)
		{
			return 0;
		}
		const size_t start_index = (logger.cursor + max_message_count - available) % max_message_count;

		// Determine contiguous segment sizes (the ring may wrap around)
		const size_t first_chunk = min(flush_count, max_message_count - start_index);
		const size_t second_chunk = flush_count - first_chunk;

		if (guest_memory)
		{
			// Batch into at most two write_guest_virtual calls instead of one per message
			hv::introspection::write_guest_virtual(&out_messages[0], &logger.messages[start_index], first_chunk * sizeof(standard_log_message));
			if (second_chunk > 0)
			{
				hv::introspection::write_guest_virtual(&out_messages[first_chunk], &logger.messages[0], second_chunk * sizeof(standard_log_message));
			}
		}
		else
		{
			utils::memcpy(&out_messages[0], &logger.messages[start_index], first_chunk * sizeof(standard_log_message));
			if (second_chunk > 0)
			{
				utils::memcpy(&out_messages[first_chunk], &logger.messages[0], second_chunk * sizeof(standard_log_message));
			}
		}

		logger.pending_count -= flush_count;

		return flush_count;
	}
}  // namespace logging
