#include "logging.h"
#include "serial.h"
#include <stdarg.h>
#include <ntstrsafe.h>

namespace logging
{
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

		// Prefix: [revhv] [LEVEL]
		if (!NT_SUCCESS(RtlStringCchPrintfA(buffer, bufferSize, "[revhv] [%s] ", level_str)))
		{
			return false;
		}

		// Append message
		size_t offset = 0;
		if (!NT_SUCCESS(RtlStringCchLengthA(buffer, bufferSize, &offset)))
		{
			return false;
		}

		if (!NT_SUCCESS(RtlStringCchVPrintfA(buffer + offset, bufferSize - offset, fmt, args)))
		{
			return false;
		}

		// Append newline
		(void)RtlStringCchCatA(buffer, bufferSize, "\n");

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

		serial::write_string(serial::SERIAL_COM_1, buffer);
	}

	void log_fmt_dbgprint(log_level level, const char* fmt, ...)
	{
		char buffer[512] = {0};
		va_list args;
		va_start(args, fmt);
		if (!log_fmt(buffer, sizeof(buffer), level, fmt, args))
		{
			DbgPrintEx(0, 0, "[revhv] [ERROR] Failed to format log message\n");
			va_end(args);
			return;
		}
		va_end(args);

		DbgPrintEx(0, 0, buffer);
	}
}  // namespace logging
