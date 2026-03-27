#include "commands.h"

#include "hypercall.h"
#include "logger.hpp"
#include "trace_parser.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstring>
#include <fstream>
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

	trace::ept_transition_data_field parse_data_field_name(const std::string& name)
	{
		using F = trace::ept_transition_data_field;
		if (name == "rip")
			return F::guest_rip;
		if (name == "rsp")
			return F::guest_rsp;
		if (name == "rax")
			return F::guest_rax;
		if (name == "rbx")
			return F::guest_rbx;
		if (name == "rcx")
			return F::guest_rcx;
		if (name == "rdx")
			return F::guest_rdx;
		if (name == "rsi")
			return F::guest_rsi;
		if (name == "rdi")
			return F::guest_rdi;
		if (name == "rbp")
			return F::guest_rbp;
		if (name == "r8")
			return F::guest_r8;
		if (name == "r9")
			return F::guest_r9;
		if (name == "r10")
			return F::guest_r10;
		if (name == "r11")
			return F::guest_r11;
		if (name == "r12")
			return F::guest_r12;
		if (name == "r13")
			return F::guest_r13;
		if (name == "r14")
			return F::guest_r14;
		if (name == "r15")
			return F::guest_r15;
		if (name == "retaddr")
			return F::guest_retaddr;
		return F::none;
	}
}  // namespace

namespace commands
{
	engine::engine(kmodule_context& modules) : m_modules(modules)
	{
		// Push the default generic config to all vCPUs so the hypervisor
		// captures rip+retaddr out of the box without any explicit user command.
		if (hv::is_present())
		{
			hv::hypercall::at_config_request req{};
			req.cfg = trace::default_generic_cfg;
			hv::hypercall::config_ept_transition(hv::hypercall::at_cfg_scope::generic, req);
		}
	}

	std::vector<std::string> engine::tokenize(const std::string& line)
	{
		std::vector<std::string> tokens;
		size_t i = 0;
		while (i < line.size())
		{
			while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
				++i;
			if (i >= line.size())
				break;

			if (line[i] == '"')
			{
				++i;
				std::string tok;
				while (i < line.size() && line[i] != '"')
					tok += line[i++];
				if (i < line.size())
					++i;
				tokens.push_back(std::move(tok));
			}
			else
			{
				const size_t start = i;
				while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i])))
					++i;
				tokens.emplace_back(line.substr(start, i - start));
			}
		}
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

	bool engine::require_hv(const std::string& command_name)
	{
		if (hv::is_present())
			return true;
		logger::error("'{}' requires the hypervisor, which is not present", command_name);
		return false;
	}

	void engine::print_general_help() const
	{
		std::cout << "Available commands:\n"
				  << "  help, ?                Show general help\n"
				  << "  d/db, dw, dd, dq, dp   Memory dumps (bytes/words/dwords/qwords/pointers) [HV]\n"
				  << "  ln                      Resolve an address/symbol\n"
				  << "  lm                      List loaded kernel modules\n"
				  << "  at                      Auto-trace: enable/disable/config [HV]\n"
				  << "  apic                    Retrieve APIC info [HV]\n"
				  << "  df                      Test host double fault [HV]\n"
				  << "  trace parse             Parse and format trace log files (offline)\n"
				  << "  q, quit, exit           Exit revhv-um\n"
				  << "\n"
				  << "Commands marked [HV] require the hypervisor to be active.\n"
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
					  << "       lm export <filename>\n"
					  << "  Lists loaded kernel modules, optionally filtered by module name or path substring.\n"
					  << "  'lm export' saves the current module list to a binary file for offline processing.\n"
					  << "  Note: address output is hexadecimal.\n"
					  << "Examples:\n"
					  << "  lm\n"
					  << "  lm nt\n"
					  << "  lm export modules.bin\n";
			return;
		}

		if (cmd == "at")
		{
			std::cout << "Usage:\n"
					  << "  at enable <address|symbol> <size> [output_dir]\n"
					  << "  at disable\n"
					  << "  at config generic <f0> [f1] [f2] [f3] [f4] [f5]\n"
					  << "  at config exact <address|symbol> <f0> [f1] [f2] [f3] [f4] [f5]\n"
					  << "  at config fmt generic \"<format>\"\n"
					  << "  at config fmt exact <address|symbol> \"<format>\"\n"
					  << "  at config fmt clear generic\n"
					  << "  at config fmt clear exact <address|symbol>\n"
					  << "  at config export <path>\n"
					  << "\n"
					  << "  enable     : Activate auto-trace for the given address range. [HV]\n"
					  << "  disable    : Stop tracing and flush remaining entries. [HV]\n"
					  << "  config     : Set which guest values are captured per trace entry. [HV]\n"
					  << "               generic applies to all transitions with no exact match.\n"
					  << "               exact installs a per-RIP override.\n"
					  << "  config fmt : Set a custom display format for the trace parser.\n"
					  << "               Use {field} for symbol-resolved or {field:x} for hex.\n"
					  << "               Omit to use the default field=value display.\n"
					  << "  config fmt clear: Remove a previously set display format.\n"
					  << "  config export: Save current config to a binary file (no HV required).\n"
					  << "\n"
					  << "  size       : Hex size of traced range (must be > 0).\n"
					  << "  output_dir : Directory for trace_core_N.bin, modules.bin, trace_cfg.bin\n"
					  << "               (default: current directory).\n"
					  << "\n"
					  << "  Available data fields (f0..f5):\n"
					  << "    rip      Guest RIP at the transition point\n"
					  << "    retaddr  64-bit value at [guest_rsp] (return address)\n"
					  << "    rsp rax rbx rcx rdx rsi rdi rbp\n"
					  << "    r8  r9  r10 r11 r12 r13 r14 r15\n"
					  << "    none     Slot unused (default)\n"
					  << "\n"
					  << "  Note: all integer values are hexadecimal.\n"
					  << "Examples:\n"
					  << "  at enable nt!NtCreateFile 20\n"
					  << "  at enable fffff80312345678 100 C:\\traces\n"
					  << "  at disable\n"
					  << "  at config generic rip retaddr\n"
					  << "  at config exact nt!NtOpenFile rip retaddr rcx rdx r8 r9\n"
					  << "  at config fmt exact nt!ExFreePool \"{retaddr} -> {rip}(pool = {rcx:x})\"\n"
					  << "  at config fmt generic \"{rip} {retaddr}\"\n"
					  << "  at config fmt clear exact nt!ExFreePool\n"
					  << "  at config export trace_cfg.bin\n";
			return;
		}

		if (cmd == "trace")
		{
			std::cout << "Usage:\n"
					  << "  trace parse <modules.bin> <trace_dir> [output_file]\n"
					  << "\n"
					  << "  Parses per-core binary trace logs, resolves symbols from module PEs on disk,\n"
					  << "  merges all entries ordered by timestamp, and writes a formatted log file.\n"
					  << "  Blocks the command prompt until processing is complete.\n"
					  << "\n"
					  << "  modules.bin : Module list exported by 'lm export' or auto-trace.\n"
					  << "  trace_dir   : Directory containing trace_core_N.bin files.\n"
					  << "  output_file : Optional output path (default: <trace_dir>/trace_formatted.log).\n"
					  << "\n"
					  << "  Does NOT require the hypervisor.\n"
					  << "Examples:\n"
					  << "  trace parse modules.bin ./traces\n"
					  << "  trace parse modules.bin ./traces combined.log\n";
			return;
		}

		if (cmd == "apic")
		{
			std::cout << "Usage: apic\n"
					  << "  Retrieves and prints APIC information from the hypervisor.\n";
			return;
		}

		if (cmd == "df")
		{
			std::cout << "Usage: df\n"
					  << "  Tests a host double fault inside the hypervisor.\n"
					  << "  WARNING: This will bugcheck/crash the system.\n";
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

		if (!require_hv(args[0]))
			return true;

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

		// Handle "lm export <filename>"
		if (args.size() >= 2 && to_lower(args[1]) == "export")
		{
			if (args.size() != 3)
			{
				logger::warn("'lm export' expects exactly one argument: <filename>");
				print_help_for("lm");
				return true;
			}

			if (m_modules.export_modules(args[2]))
				std::cout << std::format("Modules exported to {}\n", args[2]);
			else
				logger::error("Failed to export modules to '{}'", args[2]);

			return true;
		}

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

	bool engine::export_trace_config(const std::filesystem::path& path) const
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file.is_open())
			return false;

		trace_cfg_export::file_header hdr{};
		hdr.magic = trace_cfg_export::file_magic;
		hdr.version = trace_cfg_export::file_version;
		hdr.data_field_count = trace::max_data_fields;
		hdr.exact_entry_count = static_cast<uint32_t>(m_exact_transition_cfgs.size());
		hdr._pad = 0;
		hdr.generic_cfg = m_generic_transition_cfg;
		std::memset(hdr.generic_format, 0, sizeof(hdr.generic_format));
		if (!m_generic_transition_fmt.empty())
		{
			const size_t len = std::min(m_generic_transition_fmt.size(), trace_cfg_export::max_format_length - 1);
			std::memcpy(hdr.generic_format, m_generic_transition_fmt.data(), len);
		}

		file.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

		for (const auto& e : m_exact_transition_cfgs)
		{
			trace_cfg_export::exact_entry entry{};
			entry.addr = e.addr;
			entry.cfg = e.cfg;
			std::memset(entry.format, 0, sizeof(entry.format));
			if (!e.fmt.empty())
			{
				const size_t len = std::min(e.fmt.size(), trace_cfg_export::max_format_length - 1);
				std::memcpy(entry.format, e.fmt.data(), len);
			}
			file.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
		}

		return file.good();
	}

	bool engine::handle_at_config(const std::vector<std::string>& args)
	{
		if (args.size() < 3)
		{
			print_help_for("at");
			return true;
		}

		const std::string scope_str = to_lower(args[2]);

		if (scope_str == "export")
		{
			if (args.size() != 4)
			{
				logger::warn("'at config export' expects exactly one argument: <path>");
				print_help_for("at");
				return true;
			}
			const std::filesystem::path path = args[3];
			if (export_trace_config(path))
				logger::info("Trace config saved to {}", path.string());
			else
				logger::error("Failed to save trace config to '{}'", path.string());
			return true;
		}

		if (scope_str == "fmt")
		{
			if (args.size() < 4)
			{
				print_help_for("at");
				return true;
			}

			const std::string fmt_target = to_lower(args[3]);

			if (fmt_target == "clear")
			{
				if (args.size() < 5)
				{
					logger::warn("'at config fmt clear' expects 'generic' or 'exact <addr>'");
					return true;
				}
				const std::string clear_scope = to_lower(args[4]);
				if (clear_scope == "generic")
				{
					m_generic_transition_fmt.clear();
					std::cout << "Generic display format cleared\n";
				}
				else if (clear_scope == "exact")
				{
					if (args.size() < 6)
					{
						logger::warn("'at config fmt clear exact' requires an address argument");
						return true;
					}
					uint64_t addr = 0;
					if (!parse_expression(args[5], addr))
					{
						logger::warn("Failed to resolve address expression: '{}'", args[5]);
						return true;
					}
					auto it = std::find_if(m_exact_transition_cfgs.begin(), m_exact_transition_cfgs.end(), [&](const exact_cfg_entry& e) { return e.addr == addr; });
					if (it != m_exact_transition_cfgs.end())
					{
						it->fmt.clear();
						std::cout << std::format("Display format cleared for 0x{:x}\n", addr);
					}
					else
					{
						logger::warn("No exact config exists for 0x{:x}", addr);
					}
				}
				else
				{
					logger::warn("Unknown clear target '{}' (expected 'generic' or 'exact')", args[4]);
				}
				return true;
			}

			if (fmt_target == "generic")
			{
				if (args.size() != 5)
				{
					logger::warn("'at config fmt generic' expects exactly one argument: \"<format>\"");
					return true;
				}
				m_generic_transition_fmt = args[4];
				std::cout << std::format("Generic display format set: {}\n", m_generic_transition_fmt);
				return true;
			}

			if (fmt_target == "exact")
			{
				if (args.size() != 6)
				{
					logger::warn("'at config fmt exact' expects: <address|symbol> \"<format>\"");
					return true;
				}
				uint64_t addr = 0;
				if (!parse_expression(args[4], addr))
				{
					logger::warn("Failed to resolve address expression: '{}'", args[4]);
					return true;
				}
				auto it = std::find_if(m_exact_transition_cfgs.begin(), m_exact_transition_cfgs.end(), [&](const exact_cfg_entry& e) { return e.addr == addr; });
				if (it != m_exact_transition_cfgs.end())
				{
					it->fmt = args[5];
					std::cout << std::format("Display format set for 0x{:x}: {}\n", addr, it->fmt);
				}
				else
				{
					logger::warn("No exact config exists for 0x{:x}. Set data fields first with 'at config exact'.", addr);
				}
				return true;
			}

			logger::warn("Unknown fmt target '{}' (expected 'generic', 'exact', or 'clear')", args[3]);
			return true;
		}

		if (!require_hv("at config"))
			return true;

		hv::hypercall::at_cfg_scope scope{};
		hv::hypercall::at_config_request req{};
		size_t field_start = 0;

		if (scope_str == "generic")
		{
			scope = hv::hypercall::at_cfg_scope::generic;
			field_start = 3;
		}
		else if (scope_str == "exact")
		{
			if (args.size() < 4)
			{
				logger::warn("'at config exact' requires an address argument");
				return true;
			}

			uint64_t addr = 0;
			if (!parse_expression(args[3], addr))
			{
				logger::warn("Failed to resolve address expression: '{}'", args[3]);
				return true;
			}

			req.exact_addr = addr;
			scope = hv::hypercall::at_cfg_scope::exact_addr;
			field_start = 4;
		}
		else
		{
			logger::warn("Unknown at config scope '{}' (expected 'generic' or 'exact')", args[2]);
			return true;
		}

		// Parse the data-field names into the config map.
		for (size_t i = 0; i < trace::max_data_fields && (field_start + i) < args.size(); ++i)
			req.cfg.data_map[i] = parse_data_field_name(to_lower(args[field_start + i]));

		if (!hv::hypercall::config_ept_transition(scope, req))
		{
			logger::error("Failed to apply EPT transition config on one or more cores");
			return true;
		}

		// Mirror the new config into the local tracking state so it can be exported.
		if (scope == hv::hypercall::at_cfg_scope::generic)
		{
			m_generic_transition_cfg = req.cfg;
		}
		else
		{
			auto it = std::find_if(m_exact_transition_cfgs.begin(), m_exact_transition_cfgs.end(), [&](const exact_cfg_entry& e) { return e.addr == req.exact_addr; });
			if (it != m_exact_transition_cfgs.end())
				it->cfg = req.cfg;
			else
				m_exact_transition_cfgs.push_back({req.exact_addr, req.cfg});
		}

		std::cout << "EPT transition config updated\n";
		return true;
	}

	bool engine::handle_at(const std::vector<std::string>& args)
	{
		if (args.size() >= 2 && to_lower(args[1]) == "help")
		{
			print_help_for("at");
			return true;
		}

		if (!require_hv("at"))
			return true;

		if (args.size() < 2)
		{
			logger::warn("Missing subcommand for 'at' (expected 'enable' or 'disable')");
			print_help_for("at");
			return true;
		}

		const std::string subcommand = to_lower(args[1]);
		if (subcommand == "config")
			return handle_at_config(args);

		if (subcommand == "disable")
		{
			if (args.size() != 2)
			{
				logger::warn("'at disable' does not accept additional arguments");
				print_help_for("at");
				return true;
			}

			// Stop the trace poller before disabling auto-trace so remaining entries are flushed
			m_trace_poller.stop();

			if (!hv::hypercall::auto_trace_disable())
				logger::error("Failed to disable auto-trace");
			else
				std::cout << "Auto-trace disabled\n";

			return true;
		}

		if (subcommand != "enable")
		{
			logger::warn("Unknown at subcommand '{}'", args[1]);
			print_help_for("at");
			return true;
		}

		if (args.size() < 4 || args.size() > 5)
		{
			logger::warn("'at enable' expects: <address|symbol> <size> [output_dir]");
			print_help_for("at");
			return true;
		}

		m_modules.refresh();

		uint64_t resolved_address = 0;
		if (!parse_expression(args[2], resolved_address))
		{
			logger::warn("Failed to resolve expression: '{}'", args[2]);
			return true;
		}

		uint64_t size = 0;
		if (!parse_u64_token(args[3], size))
		{
			logger::warn("Invalid size '{}' (hex expected)", args[3]);
			return true;
		}

		if (size == 0)
		{
			logger::warn("Auto-trace size must be greater than 0");
			return true;
		}

		std::filesystem::path output_dir = args.size() == 5 ? std::filesystem::path(args[4]) : std::filesystem::path(".");

		if (!hv::hypercall::auto_trace_enable(resolved_address, size))
		{
			logger::error("Failed to enable auto-trace at 0x{:x} (size: 0x{:x})", resolved_address, size);
			return true;
		}

		m_trace_poller.start(output_dir);

		m_modules.refresh();
		const auto modules_path = m_trace_poller.get_output_dir() / "modules.bin";
		if (m_modules.export_modules(modules_path.string()))
			logger::info("Module snapshot saved to {}", modules_path.string());
		else
			logger::warn("Failed to save module snapshot");

		const auto cfg_path = m_trace_poller.get_output_dir() / "trace_cfg.bin";
		if (export_trace_config(cfg_path))
			logger::info("Trace config saved to {}", cfg_path.string());
		else
			logger::warn("Failed to save trace config");

		std::cout << std::format("Auto-trace enabled at 0x{:x} (size: 0x{:x}), output: {}\n", resolved_address, size, m_trace_poller.get_output_dir().string());
		return true;
	}

	void engine::stop_trace_poller()
	{
		m_trace_poller.stop();
	}

	bool engine::handle_trace_parse(const std::vector<std::string>& args)
	{
		if (args.size() >= 2 && to_lower(args[1]) == "help")
		{
			print_help_for("trace");
			return true;
		}

		if (args.size() < 2 || to_lower(args[1]) != "parse")
		{
			logger::warn("Unknown trace subcommand (expected 'trace parse')");
			print_help_for("trace");
			return true;
		}

		if (args.size() < 4 || args.size() > 5)
		{
			logger::warn("'trace parse' expects: <modules.bin> <trace_dir> [output_file]");
			print_help_for("trace");
			return true;
		}

		const std::string& modules_path = args[2];
		const std::string& trace_dir = args[3];
		std::string output_path;
		if (args.size() == 5)
			output_path = args[4];
		else
			output_path = (std::filesystem::path(trace_dir) / "trace_formatted.log").string();

		std::cout << "Parsing trace logs (this may take a while for large datasets)...\n" << std::flush;

		trace::parser parser;
		if (!parser.run(modules_path, trace_dir, output_path))
		{
			logger::error("Trace parsing failed");
			return true;
		}

		std::cout << std::format("Trace parsing complete: {}\n", output_path);
		return true;
	}

	bool engine::handle_apic_info(const std::vector<std::string>& args)
	{
		if (args.size() >= 2 && to_lower(args[1]) == "help")
		{
			print_help_for(args[0]);
			return true;
		}

		if (!require_hv(args[0]))
			return true;

		uint64_t lapic_mmio_phys_base = 0;
		bool x2apic = false;

		if (hv::hypercall::retrieve_apic_info(lapic_mmio_phys_base, x2apic))
		{
			std::cout << "APIC Information:\n" << std::format("  x2APIC enabled             : {}\n", x2apic ? "Yes" : "No") << std::format("  LAPIC MMIO physical base   : 0x{:016x}\n", lapic_mmio_phys_base);
		}
		else
		{
			logger::error("Failed to retrieve APIC info from the hypervisor.");
		}

		return true;
	}

	bool engine::handle_test_df(const std::vector<std::string>& args)
	{
		if (args.size() >= 2 && to_lower(args[1]) == "help")
		{
			print_help_for(args[0]);
			return true;
		}

		if (!require_hv(args[0]))
			return true;

		std::cout << "WARNING: This command will intentionally cause a host double fault (CRASH the system).\n"
				  << "Are you sure you want to proceed? (y/N): ";

		std::string response;
		std::getline(std::cin, response);

		response = to_lower(response);
		if (response == "y" || response == "yes")
		{
			std::cout << "Initiating host double fault...\n";
			hv::hypercall::test_host_double_fault();
			std::cout << "If you see this, the system did not crash (unexpected outcome).\n";
		}
		else
		{
			std::cout << "Aborted.\n";
		}

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

			if (cmd == "at")
				return handle_at(tokens);

			if (cmd == "trace")
				return handle_trace_parse(tokens);

			if (cmd == "apic")
				return handle_apic_info(tokens);

			if (cmd == "df")
				return handle_test_df(tokens);

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
