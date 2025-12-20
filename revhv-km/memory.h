#pragma once
#include "includes.h"
#include "utils.hpp"

namespace hv::memory
{

	constexpr ULONG pool_tag = 'REVH';

	// 256-511 are generally used in kernel space for Windows, so we use 255 for our PML4 index.
	constexpr uint64_t host_pml4_index = 255;

	// @note: Canonicalize call is done in case an implementation decides to use a higher PML4 index for whatever reason.
	constexpr uint64_t host_physical_base = utils::canonicalize(host_pml4_index << 39);

	/// @brief Enum representing the level of the page table
	enum class page_table_level
	{
		none = 0,
		pte = 1,
		pde,
		pdpte,
		pml4e,
	};

	// @note: 512 GBs of physical memory is mapped at PML4E[host_pml4_index] via 1GB large pages. In the unlikely event that more is needed, an additional PML4 entry can be added.
	// @note: Rest of the page table entries are dynamically allocated as needed. See allocate_map_nx_pool and map_host_page_tables functions.
	struct host_page_tables
	{
		alignas(0x1000) pml4e_64 pml4e[512];

		alignas(0x1000) pdpte_1gb_64 pdpte[512];
	};

	/// @brief Union representing a virtual address in 4-level paging hierarchy
	union pml4_virtual_address
	{
		void* virtual_address;	// The virtual address
		struct
		{
			uint64_t offset : 12;		  // Offset into the page
			uint64_t pt_idx : 9;		  // Page table index
			uint64_t pd_idx : 9;		  // Page directory index
			uint64_t pdpt_idx : 9;		  // Page directory pointer table index
			uint64_t pml4_idx : 9;		  // PML4 table index
			uint64_t sign_ext_bits : 16;  // Sign extension bits
		};
	};

	/// @brief Maps the host page tables by copying the top half of system PML4, and the entire physical memory in a single PML4E via 1GB large pages.
	/// @return True if the host page tables were mapped successfully, false otherwise
	bool map_host_page_tables(host_page_tables& host_page_tables, const cr3& system_cr3);

	/// @brief Allocates an NX pool and maps it to the host page tables
	/// @note Do not use this function after vmlaunch
	/// @param size Size of the memory to allocate
	/// @return True if the memory was allocated and mapped successfully, false otherwise
	void* allocate_map_nx_pool(size_t size, const cr3& system_cr3);
}  // namespace hv::memory