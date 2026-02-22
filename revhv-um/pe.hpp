#pragma once
#include "includes.h"
#include "pdb_parser.hpp"
#include <algorithm>

/// @brief Simple PE parser that uses hypercalls for reading memory
class pe
{
	struct section
	{
		std::string name;
		uint64_t va;
		uint64_t rva;
		uint64_t size;
	};

private:
	const uint8_t* m_base;
	size_t m_size;
	bool m_parsed = false;
	bool m_valid = false;
	std::vector<section> m_sections;
	std::vector<pdb::symbol_info> m_symbols;

public:
	pe(const uint8_t* base, size_t size) : m_base(base), m_size(size) {}

	/// @brief Parse the PE file and extract section and symbol information.
	/// @return Success or fail
	bool parse();

	bool is_valid() const { return m_valid; }
	const uint8_t* get_base() const { return m_base; }
	size_t get_size() const { return m_size; }
	const std::vector<section>& get_sections() const { return m_sections; }
	const std::vector<pdb::symbol_info>& get_symbols() const { return m_symbols; }

	inline const pdb::symbol_info* get_closest_symbol(uint64_t rva) const
	{
		if (m_symbols.empty())
		{
			return nullptr;
		}

		// std::upper_bound finds the first element strictly GREATER than 'rva'.
		const auto it = std::upper_bound(m_symbols.begin(), m_symbols.end(), rva, [](uint64_t target_rva, const pdb::symbol_info& symbol) { return target_rva < symbol.rva; });

		// If 'it' points to the beginning, it means ALL symbols in the list
		// have an RVA > target_rva. Thus, returning anything would result in "symbol - rva".
		if (it == m_symbols.begin())
		{
			return nullptr;
		}

		// Stepping back one element guarantees we get the symbol where symbol.rva <= rva
		return &(*std::prev(it));
	}
};