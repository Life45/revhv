#include "format.h"

namespace format
{
	// Helper to convert integers to strings (Hex and Decimal)
	static size_t int_to_str(char* buffer, size_t max_size, uint64_t value, int base, bool is_negative)
	{
		char temp[32];
		int i = 0;
		const char* digits = "0123456789abcdef";
		size_t written = 0;

		if (max_size == 0)
			return 0;

		// Handle negative sign for decimal
		if (is_negative && base == 10 && value > 0)
		{
			if (written < max_size - 1)
			{
				buffer[written++] = '-';
			}
		}

		// Process digits in reverse
		do
		{
			temp[i++] = digits[value % base];
			value /= base;
		} while (value > 0 && i < 32);

		// Reverse into the output buffer
		while (i > 0 && written < max_size - 1)
		{
			buffer[written++] = temp[--i];
		}

		return written;
	}

	int format(const char* format, char* buffer, size_t max_size, va_list args)
	{
		if (!buffer || max_size == 0)
			return 0;

		size_t pos = 0;
		const char* p = format;

		while (*p != '\0' && pos < max_size - 1)
		{
			if (*p != '%')
			{
				buffer[pos++] = *p++;
				continue;
			}

			p++;  // Skip '%'

			bool isLongLong = false;

			// Handle length modifiers (e.g., %ll)
			if (*p == 'l')
			{
				p++;
				if (*p == 'l')
				{
					isLongLong = true;
					p++;
				}
			}

			switch (*p)
			{
			case 's':
			{  // String
				const char* s = va_arg(args, const char*);
				if (!s)
					s = "(null)";
				while (*s && pos < max_size - 1)
				{
					buffer[pos++] = *s++;
				}
				break;
			}
			case 'd':
			case 'i':
			{  // Signed Decimal
				intptr_t val;
				if (isLongLong)
				{
					val = va_arg(args, intptr_t);
				}
				else
				{
					val = va_arg(args, int);
				}
				uint64_t uval = (val < 0) ? static_cast<uint64_t>(-val) : static_cast<uint64_t>(val);
				pos += int_to_str(buffer + pos, max_size - pos, uval, 10, val < 0);
				break;
			}
			case 'u':
			{  // Unsigned Decimal
				uint64_t val;
				if (isLongLong)
				{
					val = va_arg(args, uint64_t);
				}
				else
				{
					val = va_arg(args, uint32_t);
				}
				pos += int_to_str(buffer + pos, max_size - pos, val, 10, false);
				break;
			}
			case 'x':
			{  // Hexadecimal
				uint64_t val;
				if (isLongLong)
				{
					val = va_arg(args, uint64_t);
				}
				else
				{
					val = va_arg(args, uint32_t);
				}
				pos += int_to_str(buffer + pos, max_size - pos, val, 16, false);
				break;
			}
			case 'p':
			{  // Pointer
				void* ptr = va_arg(args, void*);
				if (pos < max_size - 3)
				{  // Room for 0x
					buffer[pos++] = '0';
					buffer[pos++] = 'x';
				}
				pos += int_to_str(buffer + pos, max_size - pos, reinterpret_cast<uint64_t>(ptr), 16, false);
				break;
			}
			case '%':
			{  // Literal %
				buffer[pos++] = '%';
				break;
			}
			default:
			{  // Unknown format, just print the char
				buffer[pos++] = *p;
				break;
			}
			}
			p++;
		}

		buffer[pos] = '\0';
		return static_cast<int>(pos);
	}
}  // namespace format