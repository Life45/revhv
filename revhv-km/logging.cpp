#include "logging.h"
#include "serial.h"
#include "format.h"
#include <stdarg.h>

namespace logging
{
	// Lock for serial port access
	sync::spin_lock serial_lock;

	static int format_to(char* buffer, size_t bufferSize, const char* fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		const int written = format::format(fmt, buffer, bufferSize, args);
		va_end(args);
		return written;
	}

	static bool log_fmt(char* buffer, size_t bufferSize, log_level level, const char* fmt, va_list args)
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

		return true;
	}

	void log_fmt_print(log_level level, const char* fmt, ...)
	{
		// TODO: Synchronize access, multi-core prints gibberish otherwise
		char buffer[512] = {0};

		va_list args;
		va_start(args, fmt);
		if (!log_fmt(buffer, sizeof(buffer), level, fmt, args))
		{
			serial::write_string(serial::SERIAL_COM_1, "[revhv] [ERROR] Failed to format log message\n");
			va_end(args);
			return;
		}
		va_end(args);

		sync::scoped_spin_lock lock(serial_lock);
		serial::write_string(serial::SERIAL_COM_1, buffer);
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
}  // namespace logging
