#pragma once
#include "includes.h"
#include "utils.hpp"

namespace hv::memory
{
	// 256-511 are generally used in kernel space for Windows, so we use 255 for our PML4 index.
	constexpr uint64_t host_pml4_index = 255;

	// @note: Canonicalize call is done in case an implementation decides to use a higher PML4 index for whatever reason.
	constexpr uint64_t host_physical_base = utils::canonicalize(host_pml4_index << 39);

	// @note: 512 GBs of physical memory is mapped via 1GB large pages. In the unlikely event that more is needed, an additional PML4 entry can be added.
	struct host_page_tables
	{
		alignas(0x1000) pml4e_64 pml4e[512];

		alignas(0x1000) pdpte_1gb_64 pdpte[512];
	};

	/// @brief Maps the host page tables by copying the top half of system PML4, and the entire physical memory in a single PML4E via 1GB large pages.
	/// @return True if the host page tables were mapped successfully, false otherwise
	bool map_host_page_tables(host_page_tables& host_page_tables, const cr3& system_cr3);
}  // namespace hv::memory