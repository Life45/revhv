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

		auto symbol = parsed->get_closest_symbol(rva);

		// If no symbol found, return module name + offset
		if (!symbol)
			return mod.name + "+" + utils::to_hexstr(rva);

		auto offset = rva - symbol->rva;

		// If the symbol is an exact match, return module name + symbol
		if (offset == 0)
			return mod.name + "!" + symbol->name;

		// Otherwise, return module name + symbol + offset
		return mod.name + "!" + symbol->name + "+" + utils::to_hexstr(offset);
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