#pragma once

#include <cstdint>
#include <format>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include <Windows.h>

namespace logger
{
	enum class level : std::uint8_t
	{
		trace,
		debug,
		info,
		warn,
		error,
		critical
	};

	namespace detail
	{
		inline constexpr std::string_view reset_color = "\x1b[0m";

		inline constexpr std::string_view level_name(const level log_level)
		{
			switch (log_level)
			{
			case level::trace:
				return "TRACE";
			case level::debug:
				return "DEBUG";
			case level::info:
				return "INFO";
			case level::warn:
				return "WARN";
			case level::error:
				return "ERROR";
			case level::critical:
				return "CRITICAL";
			default:
				return "UNKNOWN";
			}
		}

		inline constexpr std::string_view level_color(const level log_level)
		{
			switch (log_level)
			{
			case level::trace:
				return "\x1b[90m";
			case level::debug:
				return "\x1b[36m";
			case level::info:
				return "\x1b[96m";
			case level::warn:
				return "\x1b[33m";
			case level::error:
				return "\x1b[31m";
			case level::critical:
				return "\x1b[1;31m";
			default:
				return "\x1b[37m";
			}
		}

		inline void try_enable_virtual_terminal()
		{
			static std::once_flag once;
			std::call_once(once,
						   []
						   {
							   const HANDLE handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
							   if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
								   return;

							   DWORD mode = 0;
							   if (!::GetConsoleMode(handle, &mode))
								   return;

							   mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
							   ::SetConsoleMode(handle, mode);
						   });
		}

		inline std::string current_time()
		{
			SYSTEMTIME system_time{};
			::GetLocalTime(&system_time);

			return std::format("{:02}:{:02}:{:02}.{:03}", system_time.wHour, system_time.wMinute, system_time.wSecond, system_time.wMilliseconds);
		}
	}  // namespace detail

	template <typename... args_t>
	inline void log(const level log_level, const std::format_string<args_t...> format_text, args_t&&... args)
	{
		detail::try_enable_virtual_terminal();

		const std::string message = std::format(format_text, std::forward<args_t>(args)...);
		const std::string line = std::format("[{}] [revhv-um] [{}] {}", detail::current_time(), detail::level_name(log_level), message);

		static std::mutex output_lock;
		std::lock_guard guard(output_lock);

		std::ostream& stream = (log_level == level::error || log_level == level::critical) ? std::cerr : std::cout;
		stream << detail::level_color(log_level) << line << detail::reset_color << '\n';
		stream.flush();
	}

	template <typename... args_t>
	inline void trace(const std::format_string<args_t...> format_text, args_t&&... args)
	{
		log(level::trace, format_text, std::forward<args_t>(args)...);
	}

	template <typename... args_t>
	inline void debug(const std::format_string<args_t...> format_text, args_t&&... args)
	{
		log(level::debug, format_text, std::forward<args_t>(args)...);
	}

	template <typename... args_t>
	inline void info(const std::format_string<args_t...> format_text, args_t&&... args)
	{
		log(level::info, format_text, std::forward<args_t>(args)...);
	}

	template <typename... args_t>
	inline void warn(const std::format_string<args_t...> format_text, args_t&&... args)
	{
		log(level::warn, format_text, std::forward<args_t>(args)...);
	}

	template <typename... args_t>
	inline void error(const std::format_string<args_t...> format_text, args_t&&... args)
	{
		log(level::error, format_text, std::forward<args_t>(args)...);
	}

	template <typename... args_t>
	inline void critical(const std::format_string<args_t...> format_text, args_t&&... args)
	{
		log(level::critical, format_text, std::forward<args_t>(args)...);
	}
}  // namespace logger
