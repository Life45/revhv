#include "memory.h"
#include "hv.h"
#include "pe.h"

extern "C" void* __ImageBase;

namespace hv::memory
{
	/// @brief Allocates a single page (4KB) of memory to be used as a page table for the host
	/// @return Pointer to the allocated memory virtual address
	static void* alloc_host_pt()
	{
		// TODO: Track these to free if hypervisor is stopped
		void* va = MmAllocateContiguousMemorySpecifyCache(0x1000, {.QuadPart = 0}, {.QuadPart = static_cast<LONGLONG>(~0ULL)}, {.QuadPart = 0}, MmCached);
		if (!va)
		{
			LOG_ERROR("Failed to allocate host page table");
			return nullptr;
		}

		memset(va, 0, 0x1000);

		return va;
	}

	/// @brief Tries to get the page table entry for a given virtual address for the system CR3
	/// @param virtual_address The virtual address to get the page table entry for
	/// @param system_cr3 The system CR3
	/// @param out_pt The output page table entry
	/// @param out_level The output page table level
	/// @return True if the page table entry was found, false otherwise
	static bool get_system_pte_for_va(const void* virtual_address, const cr3& system_cr3, pte_64& out_pt, page_table_level& out_level)
	{
		// TODO: Don't use MmGetVirtualForPhysical

		pml4_virtual_address va = {0};
		va.virtual_address = const_cast<void*>(virtual_address);

		// PML4
		pml4e_64* system_dtb = reinterpret_cast<pml4e_64*>(MmGetVirtualForPhysical(PHYSICAL_ADDRESS{.QuadPart = static_cast<LONGLONG>(system_cr3.address_of_page_directory << 12)}));
		if (!system_dtb)
		{
			LOG_ERROR("Failed to get virtual address for system directory table base");
			return false;
		}

		if (!system_dtb[va.pml4_idx].present)
		{
			LOG_ERROR("System PML4E %lu is not present", va.pml4_idx);
			return false;
		}

		// PDPT
		pdpte_64* system_pdpt = reinterpret_cast<pdpte_64*>(MmGetVirtualForPhysical(PHYSICAL_ADDRESS{.QuadPart = static_cast<LONGLONG>(system_dtb[va.pml4_idx].page_frame_number << 12)}));
		if (!system_pdpt || !system_pdpt[va.pdpt_idx].present)
		{
			LOG_ERROR("System PML4E %lu PDPT %lu is not present or null: %p", va.pml4_idx, va.pdpt_idx, system_pdpt);
			return false;
		}

		// 1 GB large page
		if (system_pdpt[va.pdpt_idx].large_page)
		{
			out_pt.flags = system_pdpt[va.pdpt_idx].flags;
			out_level = page_table_level::pdpte;
			return true;
		}

		// PD
		pde_64* system_pde = reinterpret_cast<pde_64*>(MmGetVirtualForPhysical(PHYSICAL_ADDRESS{.QuadPart = static_cast<LONGLONG>(system_pdpt[va.pdpt_idx].page_frame_number << 12)}));
		if (!system_pde || !system_pde[va.pd_idx].present)
		{
			LOG_ERROR("System PML4E %lu PDE %lu is not present or null: %p", va.pml4_idx, va.pd_idx, system_pde);
			return false;
		}

		// 2 MB large page
		if (system_pde[va.pd_idx].large_page)
		{
			out_pt.flags = system_pde[va.pd_idx].flags;
			out_level = page_table_level::pde;
			return true;
		}

		// PT
		pte_64* system_pte = reinterpret_cast<pte_64*>(MmGetVirtualForPhysical(PHYSICAL_ADDRESS{.QuadPart = static_cast<LONGLONG>(system_pde[va.pd_idx].page_frame_number << 12)}));
		if (!system_pte || !system_pte[va.pt_idx].present)
		{
			LOG_ERROR("System PML4E %lu PDE %lu PTE %lu is not present or null: %p", va.pml4_idx, va.pd_idx, va.pt_idx, system_pte);
			return false;
		}

		// 4KB page
		out_pt.flags = system_pte[va.pt_idx].flags;
		out_level = page_table_level::pte;
		return true;
	}

	/// @brief Maps a given virtual address range to the host page tables, copying the flags from system CR3
	/// @note This function can over-map. For example, if the host uses a 1 GB large page and the virtual address range is less, this function will map the entire 1 GB large page.
	/// @param virtual_address The virtual address range start
	/// @param size The size of the virtual address range
	/// @param system_cr3 The system CR3
	/// @param host_page_tables The host page tables
	/// @return True if the virtual address range was mapped successfully, false otherwise
	static bool map_va_range_to_host(void* virtual_address, size_t size, const cr3& system_cr3, host_page_tables& host_page_tables)
	{
		// TODO: Don't use MmGetVirtualForPhysical, also keep a cache of PFNs

		// Start at the virtual address range start
		// No alignment is done here, as get_system_pte_for_va works with indexes only. Final alignment is done in the loop below.
		void* current_address = virtual_address;
		// End at the virtual address range end
		const void* end_address = reinterpret_cast<const void*>(reinterpret_cast<uint64_t>(virtual_address) + size);
		while (current_address < end_address)
		{
			// Get the page table entry for the current address
			pte_64 pt = {0};
			page_table_level level = page_table_level::none;
			if (!get_system_pte_for_va(current_address, system_cr3, pt, level))
			{
				return false;
			}

			pml4_virtual_address pml4_va = {0};
			pml4_va.virtual_address = current_address;

			// TODO: Assert pml4_idx < 512 and level != none && level != pml4e

			// Get the host PML4E for the current PML4 index
			pml4e_64* host_pml4e = &host_page_tables.pml4e[pml4_va.pml4_idx];

			// 1 GB large page
			if (level == page_table_level::pdpte)
			{
				pdpte_1gb_64 new_pdpte = {0};
				new_pdpte.flags = pt.flags;

				// Allocate PT(for PDPTEs) for this PML4 index if it doesn't exist
				if (!host_pml4e->present)
				{
					// Allocate a new host PDPT
					void* host_pt = alloc_host_pt();
					if (!host_pt)
					{
						LOG_ERROR("Failed to allocate host PDPT for PML4 index %lu", pml4_va.pml4_idx);
						return false;
					}

					host_pml4e->present = 1;
					host_pml4e->write = 1;
					host_pml4e->page_frame_number = MmGetPhysicalAddress(host_pt).QuadPart >> 12;
				}

				// Get the host PDPT for the current PML4 index
				pdpte_1gb_64* host_pdpt = reinterpret_cast<pdpte_1gb_64*>(MmGetVirtualForPhysical(PHYSICAL_ADDRESS{.QuadPart = static_cast<LONGLONG>(host_pml4e->page_frame_number << 12)}));
				if (!host_pdpt)
				{
					LOG_ERROR("Failed to get virtual address for host PDPT for PML4 index %lu", pml4_va.pml4_idx);
					return false;
				}

				// Map this large page to the host
				host_pdpt[pml4_va.pdpt_idx].flags = new_pdpte.flags;

				// Align current address down to 1GB large page boundary, then advance by 1GB
				current_address = reinterpret_cast<void*>(reinterpret_cast<uint64_t>(current_address) & ~0xFFFFFFFFULL);
				current_address = reinterpret_cast<void*>(reinterpret_cast<uint64_t>(current_address) + 0x100000000);
			}
			// 2 MB large page
			else if (level == page_table_level::pde)
			{
				pde_2mb_64 new_pde = {0};
				new_pde.flags = pt.flags;

				// Allocate PT(for PDPTEs) for this PML4 index if it doesn't exist
				if (!host_pml4e->present)
				{
					void* host_pt = alloc_host_pt();
					if (!host_pt)
					{
						LOG_ERROR("Failed to allocate host PDPT for PML4 index %lu", pml4_va.pml4_idx);
						return false;
					}

					host_pml4e->present = 1;
					host_pml4e->write = 1;
					host_pml4e->page_frame_number = MmGetPhysicalAddress(host_pt).QuadPart >> 12;
				}

				// Get the host PDPT for the current PML4 index
				pdpte_64* host_pdpt = reinterpret_cast<pdpte_64*>(MmGetVirtualForPhysical(PHYSICAL_ADDRESS{.QuadPart = static_cast<LONGLONG>(host_pml4e->page_frame_number << 12)}));
				if (!host_pdpt)
				{
					LOG_ERROR("Failed to get virtual address for host PDPT for PML4 index %lu", pml4_va.pml4_idx);
					return false;
				}

				// Allocate PT(for PDEs) for this PDPTE index if it doesn't exist
				if (!host_pdpt[pml4_va.pdpt_idx].present)
				{
					void* host_pt = alloc_host_pt();
					if (!host_pt)
					{
						LOG_ERROR("Failed to allocate host PD for PDPTE index %lu", pml4_va.pdpt_idx);
						return false;
					}

					host_pdpt[pml4_va.pdpt_idx].present = 1;
					host_pdpt[pml4_va.pdpt_idx].write = 1;
					host_pdpt[pml4_va.pdpt_idx].page_frame_number = MmGetPhysicalAddress(host_pt).QuadPart >> 12;
				}

				// Get the host PD for the current PDPTE index
				pde_2mb_64* host_pd = reinterpret_cast<pde_2mb_64*>(MmGetVirtualForPhysical(PHYSICAL_ADDRESS{.QuadPart = static_cast<LONGLONG>(host_pdpt[pml4_va.pdpt_idx].page_frame_number << 12)}));
				if (!host_pd)
				{
					LOG_ERROR("Failed to get virtual address for host PD for PDPTE index %lu", pml4_va.pdpt_idx);
					return false;
				}

				// Map this 2MB page to the host
				host_pd[pml4_va.pd_idx].flags = new_pde.flags;

				// Align current address down to 2MB page boundary, then advance by 2MB
				current_address = reinterpret_cast<void*>(reinterpret_cast<uint64_t>(current_address) & ~0x1FFFFFULL);
				current_address = reinterpret_cast<void*>(reinterpret_cast<uint64_t>(current_address) + 0x200000);
			}
			// 4KB page
			else if (level == page_table_level::pte)
			{
				pte_64 new_pte = {0};
				new_pte.flags = pt.flags;

				// Allocate PT(for PDPTEs) for this PML4 index if it doesn't exist
				if (!host_pml4e->present)
				{
					// Allocate a new host PDPT
					void* host_pt = alloc_host_pt();
					if (!host_pt)
					{
						LOG_ERROR("Failed to allocate host PDPT for PML4 index %lu", pml4_va.pml4_idx);
						return false;
					}

					host_pml4e->present = 1;
					host_pml4e->write = 1;
					host_pml4e->page_frame_number = MmGetPhysicalAddress(host_pt).QuadPart >> 12;
				}

				// Get the host PDPT for the current PML4 index
				pdpte_64* host_pdpt = reinterpret_cast<pdpte_64*>(MmGetVirtualForPhysical(PHYSICAL_ADDRESS{.QuadPart = static_cast<LONGLONG>(host_pml4e->page_frame_number << 12)}));
				if (!host_pdpt)
				{
					LOG_ERROR("Failed to get virtual address for host PDPT for PML4 index %lu", pml4_va.pml4_idx);
					return false;
				}

				// Allocate PT(for PDEs) for this PDPTE index if it doesn't exist
				if (!host_pdpt[pml4_va.pdpt_idx].present)
				{
					void* host_pt = alloc_host_pt();
					if (!host_pt)
					{
						LOG_ERROR("Failed to allocate host PD for PDPTE index %lu", pml4_va.pdpt_idx);
						return false;
					}

					host_pdpt[pml4_va.pdpt_idx].present = 1;
					host_pdpt[pml4_va.pdpt_idx].write = 1;
					host_pdpt[pml4_va.pdpt_idx].page_frame_number = MmGetPhysicalAddress(host_pt).QuadPart >> 12;
				}

				// Get the host PD for the current PDPTE index
				pde_64* host_pd = reinterpret_cast<pde_64*>(MmGetVirtualForPhysical(PHYSICAL_ADDRESS{.QuadPart = static_cast<LONGLONG>(host_pdpt[pml4_va.pdpt_idx].page_frame_number << 12)}));
				if (!host_pd)
				{
					LOG_ERROR("Failed to get virtual address for host PD for PDPTE index %lu", pml4_va.pdpt_idx);
					return false;
				}

				// Allocate PT(for PTEs) for this PDE index if it doesn't exist
				if (!host_pd[pml4_va.pd_idx].present)
				{
					void* host_pt = alloc_host_pt();
					if (!host_pt)
					{
						LOG_ERROR("Failed to allocate host PTE for PDE index %lu", pml4_va.pd_idx);
						return false;
					}

					host_pd[pml4_va.pd_idx].present = 1;
					host_pd[pml4_va.pd_idx].write = 1;
					host_pd[pml4_va.pd_idx].page_frame_number = MmGetPhysicalAddress(host_pt).QuadPart >> 12;
				}

				// Get the host PT for the current PDE index
				pte_64* host_pte = reinterpret_cast<pte_64*>(MmGetVirtualForPhysical(PHYSICAL_ADDRESS{.QuadPart = static_cast<LONGLONG>(host_pd[pml4_va.pd_idx].page_frame_number << 12)}));
				if (!host_pte)
				{
					LOG_ERROR("Failed to get virtual address for host PTE for PDE index %lu", pml4_va.pd_idx);
					return false;
				}

				// Map this 4KB page to the host
				host_pte[pml4_va.pt_idx].flags = new_pte.flags;

				// Align current address down to 4KB page boundary, then advance by 4KB
				current_address = reinterpret_cast<void*>(reinterpret_cast<uint64_t>(current_address) & ~0xFFFULL);
				current_address = reinterpret_cast<void*>(reinterpret_cast<uint64_t>(current_address) + 0x1000);
			}
			else
			{
				// Should never end up here
				LOG_ERROR("Invalid page table level %lu for virtual address %p", static_cast<uint64_t>(level), current_address);
				return false;
			}
		}
		LOG_INFO("Mapped virtual address range %p to %p", virtual_address, current_address);
		return true;
	}

	bool map_host_page_tables(host_page_tables& host_page_tables, const cr3& system_cr3)
	{
		// Check CPU support for 1GB large pages
		cpuid_eax_80000001 cpuid80000001 = {0};
		__cpuid(reinterpret_cast<int*>(&cpuid80000001), 0x80000001);
		if (!cpuid80000001.edx.pages_1gb_available)
		{
			LOG_ERROR("CPU does not support 1GB large pages");
			return false;
		}

		//
		// Below code has the following implications/side effects:
		// - PAT, PCD, PWT flags are implicitly set to 0, which sets each 1 GB large page to be write-back cached if IA32_PAT is configured as it is
		// following power-up or reset (See 13.12.3 Selecting a Memory Type from the PAT), or as how Windows generally configures it.
		// - With MTRRs, any 1 GB large page that translates to physical memory where multiple different MTRR-defined memory types exist
		// will result in undefined behavior. Intel SDM suggests that PCD and PWT flags in the page-table entry
		// should be set for the most conservative memory type for that range, however doing this for all ranges would
		// kill performance even if this host table is used for fairly limited purposes (see 13.11.9 Large Page Size Considerations).
		// Any check or fix for this is currently not done in the code below, therefore it is best to avoid accessing such possible ranges (eg. MMIO, etc.)
		//

		// Map 512 GBs of physical memory via 1GB large pages at PML4E[host_pml4_index]
		auto host_pml4e = &host_page_tables.pml4e[host_pml4_index];
		host_pml4e->flags = 0;
		host_pml4e->present = 1;
		host_pml4e->write = 1;
		host_pml4e->page_frame_number = MmGetPhysicalAddress(&host_page_tables.pdpte).QuadPart >> 12;
		for (size_t i = 0; i < 512; i++)
		{
			auto host_pdpte = &host_page_tables.pdpte[i];
			host_pdpte->flags = 0;
			host_pdpte->present = 1;
			host_pdpte->write = 1;
			host_pdpte->large_page = 1;
			host_pdpte->page_frame_number = i;
		}

		// Map the host image to the host page tables
		size_t hv_size = pe::get_image_size(reinterpret_cast<const uint8_t*>(&__ImageBase));
		if (!map_va_range_to_host(reinterpret_cast<void*>(&__ImageBase), hv_size, system_cr3, host_page_tables))
		{
			LOG_ERROR("Failed to map host image to host page tables");
			return false;
		}

		return true;
	}

	/// @brief Allocates an NX pool and maps it to the host page tables
	/// @note Do not use this function after vmlaunch
	/// @param size Size of the memory to allocate
	/// @return Pointer to the allocated memory
	void* allocate_map_nx_pool(size_t size, const cr3& system_cr3)
	{
		auto memory = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, pool_tag);
		if (!memory)
		{
			LOG_ERROR("Failed to allocate NX pool of size %lu", size);
			return nullptr;
		}

		// Map the NX pool to the host page tables
		if (!map_va_range_to_host(memory, size, system_cr3, g_hv.host_page_tables))
		{
			LOG_ERROR("Failed to map NX pool to host page tables");
			ExFreePool(memory);
			return nullptr;
		}

		return memory;
	}
}  // namespace hv::memory