#include "kmodules.h"
#include "logger.hpp"
#include "utils.hpp"

void kmodule_context::refresh()
{
	auto new_modules = std::make_shared<module_list>();

	using fnNtQuerySystemInformation = decltype(&::NtQuerySystemInformation);
	fnNtQuerySystemInformation NtQuerySystemInformation = reinterpret_cast<fnNtQuerySystemInformation>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQuerySystemInformation"));

	ULONG buffer_size = 0;
	NTSTATUS status = NtQuerySystemInformation(SystemModuleInformation, nullptr, 0, &buffer_size);
	if (status != STATUS_INFO_LENGTH_MISMATCH)
	{
		logger::error("Failed to query system module information size, NTSTATUS: 0x{:X}", status);
		return;
	}

	// Some lazy padding in case new modules are loaded between the size query and the actual query
	std::vector<uint8_t> buffer(buffer_size + 0x200);
	status = NtQuerySystemInformation(SystemModuleInformation, buffer.data(), buffer_size, &buffer_size);
	if (!NT_SUCCESS(status))
	{
		logger::error("Failed to query system module information, NTSTATUS: 0x{:X}", status);
		return;
	}

	PRTL_PROCESS_MODULES modules = reinterpret_cast<PRTL_PROCESS_MODULES>(buffer.data());
	for (ULONG i = 0; i < modules->NumberOfModules; ++i)
	{
		const auto& mod = modules->Modules[i];
		std::string name = std::string((LPCSTR)PTR_ADD(mod.FullPathName, mod.OffsetToFileName));
		std::string full_path = std::string((LPCSTR)mod.FullPathName);
		if (full_path.starts_with("\\SystemRoot\\"))
		{
			std::wstring SystemRoot = USER_SHARED_DATA->NtSystemRoot;
			full_path.erase(0, 11);
			full_path = utils::wstring_to_string(SystemRoot) + full_path;
		}

		// remove extension
		name.erase(name.find_last_of('.'), std::string::npos);

		new_modules->push_back({reinterpret_cast<uint64_t>(mod.ImageBase), static_cast<size_t>(mod.ImageSize), std::move(name), std::move(full_path)});
	}

	// Atomically swap in the new module list and evict only stale parse cache entries
	std::scoped_lock lock(m_modules_mutex, m_parse_mutex);
	m_modules = std::move(new_modules);

	// Only discard cached PEs for modules that were unloaded or relocated
	std::erase_if(m_parsed_modules,
				  [this](const auto& entry)
				  {
					  for (const auto& mod : *m_modules)
					  {
						  if (mod.name == entry.first)
							  return reinterpret_cast<const uint8_t*>(mod.base) != entry.second.get_base() || mod.size != entry.second.get_size();
					  }
					  return true;	// module no longer present
				  });
}

std::shared_ptr<const kmodule_context::module_list> kmodule_context::snapshot()
{
	std::lock_guard lock(m_modules_mutex);
	return m_modules;
}

const pe* kmodule_context::ensure_parsed(const kmodule& mod)
{
	std::lock_guard lock(m_parse_mutex);

	auto [it, inserted] = m_parsed_modules.try_emplace(mod.name, reinterpret_cast<const uint8_t*>(mod.base), mod.size);
	if (inserted)
	{
		if (!it->second.parse())
			logger::warn("Failed to parse PE for module {}, addresses will resolve to module name + offset only", mod.name);
	}

	return it->second.is_valid() ? &it->second : nullptr;
}

std::string kmodule_context::resolve_address(uint64_t address)
{
	auto modules = snapshot();

	for (const auto& mod : *modules)
	{
		if (address < mod.base || address >= mod.base + mod.size)
			continue;

		uint64_t rva = address - mod.base;
		if (rva == 0)
			return mod.name;

		const pe* parsed = ensure_parsed(mod);

		// If parsing failed, we don't have any symbol information so return the module name + offset
		if (!parsed)
			return mod.name + "+" + utils::to_hexstr(rva);

		// Build a "mod:section+offset" suffix for the containing section, if any.
		const auto section_suffix = [&]() -> std::string
		{
			for (const auto& sect : parsed->get_sections())
			{
				if (rva >= sect.rva && rva < sect.rva + sect.size)
					return mod.name + ":" + sect.name + "+" + utils::to_hexstr(rva - sect.rva);
			}
			return {};
		};

		const auto append_section = [&](std::string result) -> std::string
		{
			auto s = section_suffix();
			if (!s.empty())
				result += " | " + s;
			return result;
		};

		auto symbol = parsed->get_closest_symbol(rva);

		// If no symbol found, return module name + offset (+ section if available)
		if (!symbol)
			return append_section(mod.name + "+" + utils::to_hexstr(rva));

		auto offset = rva - symbol->rva;

		// If the symbol is an exact match, return module name + symbol
		if (offset == 0)
			return append_section(mod.name + "!" + symbol->name);

		// Otherwise, return module name + symbol + offset
		return append_section(mod.name + "!" + symbol->name + "+" + utils::to_hexstr(offset));
	}

	// Not inside any module, return the raw address
	return utils::to_hexstr(address);
}

std::optional<kmodule_context::kmodule> kmodule_context::get_module_by_name(const std::string& name)
{
	auto modules = snapshot();
	for (const auto& mod : *modules)
	{
		if (mod.name == name)
			return mod;
	}
	return std::nullopt;
}

std::shared_ptr<const kmodule_context::module_list> kmodule_context::get_modules()
{
	return snapshot();
}

std::optional<uint64_t> kmodule_context::resolve_symbol(const std::string& symbol_str)
{
	if (symbol_str.empty())
		return std::nullopt;

	// --- Split into module part, separator, and remainder ---
	// Separators: '!' (symbol), ':' (section), '+' (direct offset from module base).
	// The first occurrence determines the split.
	std::string module_part;
	char separator = '\0';
	std::string rest;

	const auto sep_pos = symbol_str.find_first_of("!:+");
	if (sep_pos == std::string::npos)
	{
		module_part = symbol_str;
	}
	else
	{
		module_part = symbol_str.substr(0, sep_pos);
		separator = symbol_str[sep_pos];
		rest = symbol_str.substr(sep_pos + 1);
	}

	// --- Strip known file extensions from the module part ---
	// e.g. "ntoskrnl.sys" -> "ntoskrnl"
	static constexpr std::string_view known_exts[] = {".sys", ".dll", ".exe"};
	for (const auto& ext : known_exts)
	{
		if (module_part.size() > ext.size())
		{
			std::string suffix = module_part.substr(module_part.size() - ext.size());
			std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
			if (suffix == ext)
			{
				module_part.erase(module_part.size() - ext.size());
				break;
			}
		}
	}

	// --- Apply well-known module aliases ---
	if (module_part == "nt")
		module_part = "ntoskrnl";

	// --- Look up the module ---
	auto mod_opt = get_module_by_name(module_part);
	if (!mod_opt)
		return std::nullopt;

	const auto& mod = *mod_opt;

	// No separator: return the module base address.
	if (separator == '\0')
		return mod.base;

	// Helper: parse an offset string that is always treated as hexadecimal,
	// with or without a leading "0x"/"0X" prefix.
	const auto parse_hex_offset = [](const std::string& s, uint64_t& out) -> bool
	{
		if (s.empty())
			return false;
		try
		{
			out = std::stoull(s, nullptr, 16);
			return true;
		}
		catch (...)
		{
			return false;
		}
	};

	// --- '+' separator: module base + direct hex offset ---
	// e.g. "wdfilter+0x100f", "ntoskrnl.sys+0x12"
	if (separator == '+')
	{
		uint64_t offset = 0;
		if (!parse_hex_offset(rest, offset))
			return std::nullopt;
		return mod.base + offset;
	}

	// For '!' and ':' the remainder may end with an optional "+<offset>".
	std::string name_part;
	uint64_t trailing_offset = 0;

	const auto plus_pos = rest.find('+');
	if (plus_pos != std::string::npos)
	{
		name_part = rest.substr(0, plus_pos);
		if (!parse_hex_offset(rest.substr(plus_pos + 1), trailing_offset))
			return std::nullopt;
	}
	else
	{
		name_part = rest;
	}

	// --- ':' separator: section lookup ---
	// e.g. "nt:.text", "nt:.text+0x1000"
	if (separator == ':')
	{
		const pe* parsed = ensure_parsed(mod);
		if (!parsed)
			return std::nullopt;

		// Normalise by stripping a leading '.' for comparison.
		const auto strip_dot = [](const std::string& s) -> std::string
		{
			return (!s.empty() && s[0] == '.') ? s.substr(1) : s;
		};

		const std::string cmp = strip_dot(name_part);
		for (const auto& sect : parsed->get_sections())
		{
			if (strip_dot(sect.name) == cmp)
				return mod.base + sect.rva + trailing_offset;
		}
		return std::nullopt;
	}

	// --- '!' separator: symbol lookup ---
	// e.g. "ntoskrnl!MmCopyMemory", "ntoskrnl!MmCopyMemory+0x3f"
	if (separator == '!')
	{
		const pe* parsed = ensure_parsed(mod);
		if (!parsed)
			return std::nullopt;

		for (const auto& sym : parsed->get_symbols())
		{
			if (sym.name == name_part)
				return mod.base + sym.rva + trailing_offset;
		}
		return std::nullopt;
	}

	return std::nullopt;
}