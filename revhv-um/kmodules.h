#pragma once
#include "includes.h"
#include "pe.hpp"

class kmodule_context
{
public:
	struct kmodule
	{
		uint64_t base;
		size_t size;
		std::string name;
		std::string full_path;
	};

	using module_list = std::vector<kmodule>;

private:
	std::shared_ptr<const module_list> m_modules = std::make_shared<const module_list>();
	std::mutex m_modules_mutex;

	std::unordered_map<std::string, pe> m_parsed_modules;
	std::mutex m_parse_mutex;

	/// @brief Return a thread-safe snapshot of the current module list.
	std::shared_ptr<const module_list> snapshot();

	/// @brief Lazily parse and cache a module's PE.  Returns nullptr on parse failure.
	const pe* ensure_parsed(const kmodule& mod);

public:
	kmodule_context() = default;

	void refresh();
	std::string resolve_address(uint64_t address);
	std::optional<uint64_t> resolve_symbol(const std::string& symbol_str);
	std::optional<kmodule> get_module_by_name(const std::string& name);

	/// @brief Return a thread-safe snapshot of the full module list.
	std::shared_ptr<const module_list> get_modules();
};