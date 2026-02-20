#pragma once
using uint8_t = unsigned char;
using uint16_t = unsigned short;
using uint32_t = unsigned int;
using uint64_t = unsigned long long;

namespace logging
{
	constexpr int max_standard_message_length = 256;
	constexpr int max_standard_message_count = 512;

	/// @brief A "standard" formatted log message
	struct standard_log_message
	{
		size_t message_number;
		char text[max_standard_message_length];
	};
}  // namespace logging