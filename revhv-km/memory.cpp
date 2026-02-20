#include "memory.h"
#include "hv.h"
#include "pe.h"
#include "utils.hpp"

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

		utils::memset(va, 0, 0x1000);

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

	void read_mtrrs(mtrr_state& state)
	{
		//
		//	13.11 Memory Type Range Registers (MTRRs)
		//

		utils::memset(&state, 0, sizeof(state));

		ia32_mtrr_capabilities_register cap{};
		cap.flags = __readmsr(IA32_MTRR_CAPABILITIES);
		state.variable_count = static_cast<uint8_t>(cap.variable_range_count);
		state.fixed_supported = cap.fixed_range_supported != 0;

		state.def_type.flags = __readmsr(IA32_MTRR_DEF_TYPE);

		if (!state.def_type.mtrr_enable)
		{
			LOG_INFO("MTRRs are not enabled on this processor");
			return;
		}

		// Fixed range MTRRs
		if (state.def_type.fixed_range_mtrr_enable && state.fixed_supported)
		{
			// 64K fixed range MTRR
			uint64_t fix64k_msr = __readmsr(IA32_MTRR_FIX64K_00000);
			for (size_t i = 0; i < 8; i++)
			{
				state.fixed[i] = (fix64k_msr >> (i * 8)) & 0xFF;
			}

			// 16K fixed range MTRRs
			constexpr uint32_t fix16k_msrs[] = {IA32_MTRR_FIX16K_80000, IA32_MTRR_FIX16K_A0000};
			for (size_t j = 0; j < 2; j++)
			{
				uint64_t fix16k_msr = __readmsr(fix16k_msrs[j]);
				for (size_t i = 0; i < 8; i++)
				{
					state.fixed[8 + j * 8 + i] = (fix16k_msr >> (i * 8)) & 0xFF;
				}
			}

			// 4K fixed range MTRRs
			constexpr uint32_t fix4k_msrs[] = {IA32_MTRR_FIX4K_C0000, IA32_MTRR_FIX4K_C8000, IA32_MTRR_FIX4K_D0000, IA32_MTRR_FIX4K_D8000, IA32_MTRR_FIX4K_E0000, IA32_MTRR_FIX4K_E8000, IA32_MTRR_FIX4K_F0000, IA32_MTRR_FIX4K_F8000};
			for (size_t j = 0; j < 8; j++)
			{
				uint64_t fix4k_msr = __readmsr(fix4k_msrs[j]);
				for (size_t i = 0; i < 8; i++)
				{
					state.fixed[24 + j * 8 + i] = (fix4k_msr >> (i * 8)) & 0xFF;
				}
			}
		}

		// Variable range MTRRs
		for (size_t i = 0; i < state.variable_count; i++)
		{
			state.variable[i].base.flags = __readmsr(IA32_MTRR_PHYSBASE0 + i * 2);
			state.variable[i].mask.flags = __readmsr(IA32_MTRR_PHYSMASK0 + i * 2);
		}
	}

	/// @brief Gets the MTRR memory type for a single physical address
	/// @param state Reference to an mtrr_state struct containing the MTRR state
	/// @param physical_address The physical address to get the memory type for
	/// @return The memory type for the given physical address
	static uint8_t get_mtrr_memory_type_single(const mtrr_state& state, uint64_t physical_address)
	{
		if (!state.def_type.mtrr_enable)
		{
			// If MTRRs are not enabled, the entire physical address space is treated as UC
			return MEMORY_TYPE_UNCACHEABLE;
		}

		// Check fixed range MTRRs first(if enabled), as they take precedence over variable range MTRRs
		if (physical_address < 0x100000 && state.fixed_supported && state.def_type.fixed_range_mtrr_enable)
		{
			if (physical_address < 0x80000)
				return state.fixed[physical_address / IA32_MTRR_FIX64K_SIZE];
			else if (physical_address < 0xC0000)
				return state.fixed[8 + (physical_address - IA32_MTRR_FIX16K_BASE) / IA32_MTRR_FIX16K_SIZE];
			else
				return state.fixed[24 + (physical_address - IA32_MTRR_FIX4K_BASE) / IA32_MTRR_FIX4K_SIZE];
		}

		// Variable range MTRRs
		uint8_t final_memory_type = MEMORY_TYPE_INVALID;
		for (size_t i = 0; i < state.variable_count; i++)
		{
			if (!state.variable[i].mask.valid)
				continue;

			const uint64_t range_mask = state.variable[i].mask.page_frame_number << 12;
			const uint64_t range_base = state.variable[i].base.page_frame_number << 12;

			if ((physical_address & range_mask) != (range_base & range_mask))
				continue;

			const uint8_t type = static_cast<uint8_t>(state.variable[i].base.type);

			if (final_memory_type == MEMORY_TYPE_INVALID)
			{
				final_memory_type = type;
				continue;
			}

			// Overlap precedence rules (13.11.4.1):
			//  - UC wins over everything.
			//  - WT + WB = WT.
			//  - All other overlaps are architecturally undefined; fall back to UC.
			if (type == MEMORY_TYPE_UNCACHEABLE || final_memory_type == MEMORY_TYPE_UNCACHEABLE)
				final_memory_type = MEMORY_TYPE_UNCACHEABLE;
			else if ((type == MEMORY_TYPE_WRITE_THROUGH && final_memory_type == MEMORY_TYPE_WRITE_BACK) || (type == MEMORY_TYPE_WRITE_BACK && final_memory_type == MEMORY_TYPE_WRITE_THROUGH))
				final_memory_type = MEMORY_TYPE_WRITE_THROUGH;
			else
				final_memory_type = MEMORY_TYPE_UNCACHEABLE;
		}

		// If no MTRR matched, return the default memory type
		return (final_memory_type != MEMORY_TYPE_INVALID) ? final_memory_type : static_cast<uint8_t>(state.def_type.default_memory_type);
	}

	uint8_t get_mtrr_range_memory_type(const mtrr_state& state, uint64_t physical_address, size_t target_size, bool& is_uniform)
	{
		// TODO: This function could probably be written a lot better

		is_uniform = true;

		const uint8_t first_type = get_mtrr_memory_type_single(state, physical_address);
		uint8_t result_type = first_type;

		// Walk each 4 KB page in the range, starting from the second page
		const uint64_t end_address = physical_address + target_size;
		for (uint64_t addr = (physical_address & ~0xFFFULL) + 0x1000; addr < end_address; addr += 0x1000)
		{
			const uint8_t type = get_mtrr_memory_type_single(state, addr);
			if (type == result_type)
				continue;

			is_uniform = false;

			// Overlap precedence rules (13.11.4.1):
			//  - UC wins over everything.
			//  - WT + WB = WT.
			//  - All other overlaps are architecturally undefined; fall back to UC.
			if (type == MEMORY_TYPE_UNCACHEABLE || result_type == MEMORY_TYPE_UNCACHEABLE)
				result_type = MEMORY_TYPE_UNCACHEABLE;
			else if ((type == MEMORY_TYPE_WRITE_THROUGH && result_type == MEMORY_TYPE_WRITE_BACK) || (type == MEMORY_TYPE_WRITE_BACK && result_type == MEMORY_TYPE_WRITE_THROUGH))
				result_type = MEMORY_TYPE_WRITE_THROUGH;
			else
				result_type = MEMORY_TYPE_UNCACHEABLE;
		}

		return result_type;
	}

	uint64_t gva_to_gpa(const cr3& guest_cr3, uint64_t gva, size_t* offset_to_next_page)
	{
		pml4_virtual_address va = {0};
		va.virtual_address = reinterpret_cast<void*>(gva);

		// PML4
		const pml4e_64* dtb = reinterpret_cast<pml4e_64*>(host_physical_base + (guest_cr3.address_of_page_directory << 12));
		const auto pml4e = dtb[va.pml4_idx];
		if (!pml4e.present)
		{
			return 0;
		}

		// PDPT
		const pdpte_64* pdpt = reinterpret_cast<pdpte_64*>(host_physical_base + (pml4e.page_frame_number << 12));
		const auto pdpte = pdpt[va.pdpt_idx];
		if (!pdpte.present)
		{
			return 0;
		}

		// 1 GB large page
		if (pdpte.large_page)
		{
			pdpte_1gb_64 pdpte_1gb;
			pdpte_1gb.flags = pdpte.flags;

			const auto offset = (va.pd_idx << 21) + (va.pt_idx << 12) + va.offset;

			if (offset_to_next_page)
				*offset_to_next_page = 0x40000000 - offset;

			return (pdpte_1gb.page_frame_number << 30) + offset;
		}

		// PD
		const pde_64* pd = reinterpret_cast<pde_64*>(host_physical_base + (pdpte.page_frame_number << 12));
		const auto pde = pd[va.pd_idx];
		if (!pde.present)
		{
			return 0;
		}

		// 2 MB large page
		if (pde.large_page)
		{
			pde_2mb_64 pde_2mb;
			pde_2mb.flags = pde.flags;

			auto const offset = (va.pt_idx << 12) + va.offset;

			if (offset_to_next_page)
				*offset_to_next_page = 0x200000 - offset;

			return (pde_2mb.page_frame_number << 21) + offset;
		}

		// PT
		const pte_64* pt = reinterpret_cast<pte_64*>(host_physical_base + (pde.page_frame_number << 12));
		const auto pte = pt[va.pt_idx];
		if (!pte.present)
		{
			return 0;
		}

		if (offset_to_next_page)
			*offset_to_next_page = 0x1000 - va.offset;

		return (pte.page_frame_number << 12) + va.offset;
	}

	uint64_t gva_to_hva(const cr3& guest_cr3, uint64_t gva, size_t* offset_to_next_page)
	{
		const auto gpa = gva_to_gpa(guest_cr3, gva, offset_to_next_page);
		if (gpa == 0)
		{
			return 0;
		}

		return host_physical_base + gpa;
	}

}  // namespace hv::memory