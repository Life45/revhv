#include "commands.h"

#include "hypercall.h"
#include "logger.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstring>
#include <limits>
#include <sstream>

namespace
{
	std::string to_lower(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	}

	std::string join_tokens(const std::vector<std::string>& tokens, const size_t start_index)
	{
		if (start_index >= tokens.size())
			return {};

		std::string result;
		for (size_t i = start_index; i < tokens.size(); ++i)
		{
			if (!result.empty())
				result += ' ';
			result += tokens[i];
		}
		return result;
	}

	std::string dump_kind_name(const commands::engine::dump_kind kind)
	{
		switch (kind)
		{
		case commands::engine::dump_kind::bytes:
			return "d/db";
		case commands::engine::dump_kind::words:
			return "dw";
		case commands::engine::dump_kind::dwords:
			return "dd";
		case commands::engine::dump_kind::qwords:
			return "dq";
		case commands::engine::dump_kind::pointers:
			return "dp";
		default:
			return "d";
		}
	}

	size_t unit_size_for(const commands::engine::dump_kind kind)
	{
		switch (kind)
		{
		case commands::engine::dump_kind::bytes:
			return 1;
		case commands::engine::dump_kind::words:
			return 2;
		case commands::engine::dump_kind::dwords:
			return 4;
		case commands::engine::dump_kind::qwords:
		case commands::engine::dump_kind::pointers:
			return 8;
		default:
			return 1;
		}
	}
}  // namespace

namespace commands
{
	engine::engine(kmodule_context& modules) : m_modules(modules)
	{
	}

	std::vector<std::string> engine::tokenize(const std::string& line)
	{
		std::istringstream stream(line);
		std::vector<std::string> tokens;
		for (std::string token; stream >> token;)
			tokens.push_back(std::move(token));
		return tokens;
	}

	bool engine::parse_u64_token(const std::string& token, uint64_t& value)
	{
		std::string sanitized;
		sanitized.reserve(token.size());
		for (const unsigned char c : token)
		{
			if (c == '`' || c == '\'' || c == '_')
				continue;
			sanitized.push_back(static_cast<char>(c));
		}

		if (sanitized.empty())
			return false;

		int base = 16;
		if (sanitized.size() > 2 && sanitized[0] == '0' && (sanitized[1] == 'x' || sanitized[1] == 'X'))
		{
			base = 16;
			sanitized.erase(0, 2);
		}
		else if (!sanitized.empty() && (sanitized.back() == 'h' || sanitized.back() == 'H'))
		{
			base = 16;
			sanitized.pop_back();
		}
		else if (std::any_of(sanitized.begin(), sanitized.end(), [](const unsigned char c) { return (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }))
		{
			base = 16;
		}

		if (sanitized.empty())
			return false;

		const char* begin = sanitized.data();
		const char* end = begin + sanitized.size();
		const auto parse_attempt = [&](const int parse_base) -> bool
		{
			uint64_t parsed = 0;
			auto [ptr, ec] = std::from_chars(begin, end, parsed, parse_base);
			if (ec == std::errc{} && ptr == end)
			{
				value = parsed;
				return true;
			}
			return false;
		};

		return parse_attempt(base);
	}

	bool engine::parse_expression(const std::string& token, uint64_t& value) const
	{
		if (parse_u64_token(token, value))
			return true;

		auto resolved = m_modules.resolve_symbol(token);
		if (!resolved)
			return false;

		value = *resolved;
		return true;
	}

	void engine::print_general_help() const
	{
		std::cout << "Available commands:\n"
				  << "  help, ?                Show general help\n"
				  << "  d/db, dw, dd, dq, dp   Memory dumps (bytes/words/dwords/qwords/pointers)\n"
				  << "  ln                      Resolve an address/symbol\n"
				  << "  lm                      List loaded kernel modules\n"
				  << "  q, quit, exit           Exit revhv-um\n"
				  << "\n"
				  << "Integer parsing: all numeric inputs are treated as hexadecimal.\n"
				  << "Accepted hex forms: 0x1234, 1234, 1234h, ffff`f800`1234`5678.\n"
				  << "\n"
				  << "Unrecognized input is auto-tried as a symbol/address expression.\n"
				  << "Use '<command> help' for command-specific usage.\n";
	}

	void engine::print_help_for(const std::string& command_name) const
	{
		const std::string cmd = to_lower(command_name);

		if (cmd == "d" || cmd == "db" || cmd == "dw" || cmd == "dd" || cmd == "dq" || cmd == "dp")
		{
			std::cout << "Usage: " << cmd << " <address|symbol> [count] [target_cr3]\n"
					  << "  address|symbol : Hex address or symbol expression\n"
					  << "  count          : Number of elements to dump (default varies by command)\n"
					  << "  target_cr3     : Optional CR3 for target address space (default: 0 = system CR3)\n"
					  << "  Note           : All integer values are hexadecimal\n"
					  << "Examples:\n"
					  << "  db nt!MmCopyMemory 40\n"
					  << "  dd fffff80312340000 20\n"
					  << "  dp ntoskrnl!KeBugCheckEx+20 8\n"
					  << "  dq nt:PAGE+123\n";
			return;
		}

		if (cmd == "ln")
		{
			std::cout << "Usage: ln <address|symbol>\n"
					  << "  Resolves input to address and nearest symbolic form.\n"
					  << "  Symbol forms supported by resolver:\n"
					  << "    module                    (e.g. nt, ntoskrnl, ntoskrnl.sys)\n"
					  << "    module+offset             (e.g. nt+1000, ntoskrnl+0x1000)\n"
					  << "    module!symbol[+offset]    (e.g. nt!MmCopyMemory+100)\n"
					  << "    module:section[+offset]   (e.g. nt:PAGE+0x123, nt:.text+40)\n"
					  << "  Note: all integer values are hexadecimal.\n"
					  << "Examples:\n"
					  << "  ln nt!MmCopyMemory+0x100\n"
					  << "  ln nt:PAGE+0x123\n"
					  << "  ln 0xfffff80312345678\n";
			return;
		}

		if (cmd == "lm")
		{
			std::cout << "Usage: lm [filter]\n"
					  << "  Lists loaded kernel modules, optionally filtered by module name or path substring.\n"
					  << "  Note: address output is hexadecimal.\n"
					  << "Examples:\n"
					  << "  lm\n"
					  << "  lm nt\n";
			return;
		}

		if (cmd == "help" || cmd == "?")
		{
			print_general_help();
			return;
		}

		std::cout << "No command-specific help for '" << command_name << "'.\n";
	}

	bool engine::handle_dump(const std::vector<std::string>& args, const dump_kind kind)
	{
		if (args.size() >= 2 && to_lower(args[1]) == "help")
		{
			print_help_for(args[0]);
			return true;
		}

		if (args.size() < 2)
		{
			logger::warn("Missing address/symbol for {}", dump_kind_name(kind));
			print_help_for(args[0]);
			return true;
		}

		m_modules.refresh();

		uint64_t start_address = 0;
		if (!parse_expression(args[1], start_address))
		{
			logger::warn("Invalid address/symbol: '{}'", args[1]);
			print_help_for(args[0]);
			return true;
		}

		uint64_t count = 0;
		switch (kind)
		{
		case dump_kind::bytes:
			count = 0x40;
			break;
		case dump_kind::words:
			count = 0x20;
			break;
		case dump_kind::dwords:
			count = 0x10;
			break;
		case dump_kind::qwords:
		case dump_kind::pointers:
			count = 0x8;
			break;
		default:
			count = 0x40;
			break;
		}

		if (args.size() >= 3 && !parse_u64_token(args[2], count))
		{
			logger::warn("Invalid count '{}' (hex expected)", args[2]);
			print_help_for(args[0]);
			return true;
		}

		if (count == 0)
		{
			logger::warn("Count must be greater than 0");
			print_help_for(args[0]);
			return true;
		}

		uint64_t target_cr3 = 0;
		if (args.size() >= 4 && !parse_u64_token(args[3], target_cr3))
		{
			logger::warn("Invalid CR3 '{}' (hex expected)", args[3]);
			print_help_for(args[0]);
			return true;
		}

		if (args.size() > 4)
		{
			logger::warn("Too many arguments for {}", dump_kind_name(kind));
			print_help_for(args[0]);
			return true;
		}

		const size_t unit_size = unit_size_for(kind);
		if (count > (std::numeric_limits<size_t>::max)() / unit_size)
		{
			logger::error("Requested dump size is too large");
			return true;
		}

		const size_t byte_count = static_cast<size_t>(count) * unit_size;
		std::vector<uint8_t> buffer(byte_count);

		const size_t bytes_read = hv::hypercall::read_vmemory(start_address, buffer.data(), byte_count, target_cr3);
		if (bytes_read == 0)
		{
			logger::error("Failed to read memory at 0x{:x}", start_address);
			return true;
		}

		if (kind == dump_kind::pointers)
		{
			const size_t pointer_size = sizeof(uint64_t);
			const size_t pointer_count = bytes_read / pointer_size;
			for (size_t i = 0; i < pointer_count; ++i)
			{
				uint64_t value = 0;
				std::memcpy(&value, buffer.data() + i * pointer_size, pointer_size);

				const uint64_t current_address = start_address + static_cast<uint64_t>(i * pointer_size);
				std::cout << std::format("{:016x}  {:016x}  {}\n", current_address, value, m_modules.resolve_address(value));
			}

			if (pointer_count == 0)
				logger::warn("No full pointers were read");

			if (bytes_read < byte_count)
				logger::warn("Read only {} out of {} requested bytes", bytes_read, byte_count);

			return true;
		}

		constexpr size_t bytes_per_line = 16;
		for (size_t offset = 0; offset < bytes_read; offset += bytes_per_line)
		{
			const size_t line_bytes = (std::min)(bytes_per_line, bytes_read - offset);
			const uint64_t line_address = start_address + static_cast<uint64_t>(offset);

			std::cout << std::format("{:016x}  ", line_address);

			for (size_t cell_offset = 0; cell_offset < bytes_per_line; cell_offset += unit_size)
			{
				if (cell_offset + unit_size <= line_bytes)
				{
					uint64_t value = 0;
					std::memcpy(&value, buffer.data() + offset + cell_offset, unit_size);
					std::cout << std::format("{:0{}x} ", value, static_cast<int>(unit_size * 2));
				}
				else
				{
					std::cout << std::string(unit_size * 2, ' ') << ' ';
				}
			}

			if (kind == dump_kind::bytes)
			{
				std::cout << " |";
				for (size_t i = 0; i < line_bytes; ++i)
				{
					const unsigned char c = buffer[offset + i];
					std::cout << (std::isprint(c) ? static_cast<char>(c) : '.');
				}
				std::cout << '|';
			}

			std::cout << '\n';
		}

		if (bytes_read < byte_count)
			logger::warn("Read only {} out of {} requested bytes", bytes_read, byte_count);

		return true;
	}

	bool engine::handle_ln(const std::vector<std::string>& args)
	{
		if (args.size() >= 2 && to_lower(args[1]) == "help")
		{
			print_help_for("ln");
			return true;
		}

		if (args.size() != 2)
		{
			logger::warn("'ln' expects exactly one argument");
			print_help_for("ln");
			return true;
		}

		m_modules.refresh();

		uint64_t resolved_address = 0;
		if (!parse_expression(args[1], resolved_address))
		{
			logger::warn("Failed to resolve expression: '{}'", args[1]);
			print_help_for("ln");
			return true;
		}

		std::cout << std::format("{:016x}  {}\n", resolved_address, m_modules.resolve_address(resolved_address));
		return true;
	}

	bool engine::handle_lm(const std::vector<std::string>& args)
	{
		if (args.size() >= 2 && to_lower(args[1]) == "help")
		{
			print_help_for("lm");
			return true;
		}

		m_modules.refresh();

		std::string filter;
		if (args.size() > 1)
			filter = to_lower(join_tokens(args, 1));

		auto modules = m_modules.get_modules();
		size_t printed = 0;
		for (const auto& mod : *modules)
		{
			const std::string lower_name = to_lower(mod.name);
			const std::string lower_path = to_lower(mod.full_path);
			if (!filter.empty() && lower_name.find(filter) == std::string::npos && lower_path.find(filter) == std::string::npos)
				continue;

			const uint64_t start = mod.base;
			const uint64_t end = mod.base + static_cast<uint64_t>(mod.size);
			std::cout << std::format("{:016x} {:016x} {:<24} {}\n", start, end, mod.name, mod.full_path);
			++printed;
		}

		if (printed == 0)
			logger::warn("No modules matched the provided filter");

		return true;
	}

	bool engine::execute_line(const std::string& line)
	{
		try
		{
			auto tokens = tokenize(line);
			if (tokens.empty())
				return true;

			const std::string cmd = to_lower(tokens[0]);

			if (cmd == "help" || cmd == "?")
			{
				if (tokens.size() == 1)
					print_general_help();
				else
					print_help_for(tokens[1]);
				return true;
			}

			if (cmd == "q" || cmd == "quit" || cmd == "exit")
				return false;

			if (cmd == "d" || cmd == "db" || cmd == "hex")
				return handle_dump(tokens, dump_kind::bytes);

			if (cmd == "dw")
				return handle_dump(tokens, dump_kind::words);

			if (cmd == "dd")
				return handle_dump(tokens, dump_kind::dwords);

			if (cmd == "dq")
				return handle_dump(tokens, dump_kind::qwords);

			if (cmd == "dp")
				return handle_dump(tokens, dump_kind::pointers);

			if (cmd == "ln")
				return handle_ln(tokens);

			if (cmd == "lm")
				return handle_lm(tokens);

			m_modules.refresh();
			const std::string expression = join_tokens(tokens, 0);
			uint64_t resolved_address = 0;
			if (parse_expression(expression, resolved_address))
			{
				std::cout << std::format("{:016x}  {}\n", resolved_address, m_modules.resolve_address(resolved_address));
				return true;
			}

			logger::warn("Unknown command/expression: '{}'", expression);
			print_general_help();
			return true;
		}
		catch (const std::exception& ex)
		{
			logger::error("Command processing error: {}", ex.what());
			return true;
		}
		catch (...)
		{
			logger::error("Command processing failed due to an unknown error");
			return true;
		}
	}
}  // namespace commands
