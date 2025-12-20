#pragma once
#include "includes.h"
#include <stdarg.h>

namespace format
{
	/// @brief Formats a string
	/// @param format Format
	/// @param buffer Buffer to store the formatted string
	/// @param max_size Maximum size of the buffer
	/// @param args Arguments
	/// @return Number of characters written to the buffer
	int format(const char* format, char* buffer, size_t max_size, va_list args);
}  // namespace format