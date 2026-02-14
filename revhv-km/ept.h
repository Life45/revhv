#pragma once
#include "includes.h"
#include "memory.h"

namespace hv::ept
{
	constexpr int ept_pd_count = 64;		  // Basically 64 GB of memory via 2 MB pages
	constexpr int ept_split_pte_count = 256;  // Number of PTEs used for splitting 2 MB pages into 4 KB pages

	struct ept_pages
	{
		alignas(0x1000) ept_pml4e pml4e[512];
		alignas(0x1000) ept_pdpte pdpte[512];
		union
		{
			alignas(0x1000) ept_pde_2mb pde_2mb[ept_pd_count][512];
			alignas(0x1000) ept_pde pde_split[ept_pd_count][512];
		};

		alignas(0x1000) ept_pte split_pte[ept_split_pte_count][512];  // Used for splitting 2 MB pages into 4 KB pages
		uint64_t split_pte_pfns[ept_split_pte_count];				  // PFNs for the PTs used for splitting, used to avoid va->pa translation later

		size_t split_pte_used = 0;
	};

	/// @brief Initializes the EPT structures
	/// @param ept_pages The EPT pages to initialize
	/// @param mtrr_state The MTRR state to use for memory type information
	void initialize_ept(ept_pages& ept_pages, const memory::mtrr_state& mtrr_state);

}  // namespace hv::ept