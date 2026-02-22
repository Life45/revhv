#pragma once
#include "includes.h"

namespace pdb
{
	struct symbol_info
	{
		std::string name;
		uint64_t rva;
	};

	/// @brief Parse the PDB file and extract symbol information.
	/// @param pdb_path The file path to the PDB file.
	/// @param symbols The vector to populate with symbol information.
	/// @return Success or fail
	bool load_symbols(const std::string& pdb_path, std::vector<symbol_info>& symbols);
}  // namespace pdb