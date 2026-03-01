#pragma once
#include "includes.h"
#include "pdb_parser.hpp"
#include <algorithm>
#include <functional>

/// @brief PE parser that can read from either live kernel memory (via hypercalls)
/// or a local memory buffer (e.g. a memory-mapped file from disk).
///
/// The read strategy is determined by the read_fn callback supplied at construction.
class pe
{
public:
	struct section
	{
		std::string name;
		uint64_t va;
		uint64_t rva;
		uint64_t size;
	};

	/// @brief Read callback: read `size` bytes from `source` into `dest`.
	/// Returns true on success. This abstracts hypercall reads vs. direct memcpy.
	using read_fn = std::function<bool(const void* source, void* dest, size_t size)>;

private:
	const uint8_t* m_base;
	size_t m_size;
	bool m_parsed = false;
	bool m_valid = false;
	bool m_owns_mapping = false;  // true when we memory-mapped a file ourselves
	void* m_mapping_base = nullptr;
	HANDLE m_mapping_handle = nullptr;
	std::vector<section> m_sections;
	std::vector<pdb::symbol_info> m_symbols;
	read_fn m_read;

public:
	/// @brief Construct a PE parser for live kernel memory (reads via hypercalls).
	pe(const uint8_t* base, size_t size);

	/// @brief Construct a PE parser backed by a local memory buffer with a custom read function.
	pe(const uint8_t* base, size_t size, read_fn read);

	/// @brief Open and memory-map a PE file from disk for offline parsing.
	/// @param file_path  Path to the PE file on disk
	/// @param image_base Optional virtual base address to use for VA computation (0 = use file offset)
	/// @return A pe instance backed by the mapped file, or std::nullopt on failure
	static std::optional<pe> from_file(const std::string& file_path, uint64_t image_base = 0);

	~pe();

	// Move-only (owns file mapping resources)
	pe(pe&& other) noexcept;
	pe& operator=(pe&& other) noexcept;
	pe(const pe&) = delete;
	pe& operator=(const pe&) = delete;

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