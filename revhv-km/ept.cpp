#include "ept.h"
#include "hv.h"

namespace hv::ept
{
	static void set_all_permissions(ept_pages& ept_pages, bool read, bool write, bool execute)
	{
		ept_pml4e& pml4e = ept_pages.pml4e[0];
		for (size_t i = 0; i < ept_pd_count; i++)
		{
			ept_pdpte& pdpte = ept_pages.pdpte[i];

			for (size_t j = 0; j < 512; j++)
			{
				// 2MB large PDEs
				ept_pde_2mb& pde_2mb = ept_pages.pde_2mb[i][j];
				if (pde_2mb.large_page)
				{
					pde_2mb.read_access = read ? 1 : 0;
					pde_2mb.write_access = write ? 1 : 0;
					pde_2mb.execute_access = execute ? 1 : 0;
				}
			}
		}

		// Split PDEs
		for (size_t i = 0; i < ept_split_pte_count; i++)
		{
			ept_pte* pt = reinterpret_cast<ept_pte*>(&ept_pages.split_pte[i]);
			for (size_t j = 0; j < 512; j++)
			{
				ept_pte& pte = pt[j];
				pte.read_access = read ? 1 : 0;
				pte.write_access = write ? 1 : 0;
				pte.execute_access = execute ? 1 : 0;
			}
		}
	}

	bool split_pde(ept_pages& ept_pages, ept_pde_2mb* pde_2mb, ept_pte** out_pt = nullptr)
	{
		if (pde_2mb->large_page == 0)
		{
			// Already split
			return false;
		}

		if (ept_pages.split_pte_used >= ept_split_pte_count)
		{
			// No more split PTs available
			LOG_ERROR("No more free split PTs available");
			return false;
		}

		auto const pt_pfn = ept_pages.split_pte_pfns[ept_pages.split_pte_used];
		auto const pt = reinterpret_cast<ept_pte*>(&ept_pages.split_pte[ept_pages.split_pte_used]);
		++ept_pages.split_pte_used;

		for (size_t i = 0; i < 512; i++)
		{
			ept_pte& pte = pt[i];
			pte.flags = 0;

			// Inherit from PDE
			pte.read_access = pde_2mb->read_access;
			pte.write_access = pde_2mb->write_access;
			pte.execute_access = pde_2mb->execute_access;
			pte.memory_type = pde_2mb->memory_type;
			pte.ignore_pat = pde_2mb->ignore_pat;
			pte.accessed = pde_2mb->accessed;
			pte.dirty = pde_2mb->dirty;
			pte.user_mode_execute = pde_2mb->user_mode_execute;
			pte.verify_guest_paging = pde_2mb->verify_guest_paging;
			pte.paging_write_access = pde_2mb->paging_write_access;
			pte.supervisor_shadow_stack = pde_2mb->supervisor_shadow_stack;
			pte.suppress_ve = pde_2mb->suppress_ve;
			pte.page_frame_number = (pde_2mb->page_frame_number << 9) + i;
		}

		// Update PDE to point to the new PT
		ept_pde* pde = reinterpret_cast<ept_pde*>(pde_2mb);
		pde->flags = 0;
		pde->read_access = 1;
		pde->write_access = 1;
		pde->execute_access = 1;
		pde->user_mode_execute = 1;
		pde->page_frame_number = pt_pfn;

		if (out_pt)
		{
			*out_pt = pt;
		}
		return true;
	}

	void initialize_ept(ept_pages& ept_pages, const memory::mtrr_state& mtrr_state)
	{
		utils::memset(&ept_pages, 0, sizeof(ept_pages));

		// Harcoded check for the physical address width just to warn if our maximum can't cover the full addressable memory (some MMIO ranges can exceed it)
		cpuid_eax_80000008 cpuid = {0};
		__cpuid(reinterpret_cast<int*>(&cpuid), 0x80000008);
		const uint8_t physical_address_width = cpuid.eax.number_of_physical_address_bits;
		if (physical_address_width > 39)
		{
			LOG_WARNING("Processor supports physical address width of %i bits, but EPT implementation only supports up to 39 bits (512 GB)", physical_address_width);
		}

		// Initialize split PT PFNs
		for (size_t i = 0; i < ept_split_pte_count; i++)
		{
			auto pt = ept_pages.split_pte[i];
			ept_pages.split_pte_pfns[i] = MmGetPhysicalAddress(reinterpret_cast<void*>(pt)).QuadPart >> 12;
		}

		// Initialize PML4E (only one entry used, it covers 512 GB)
		ept_pml4e& pml4e = ept_pages.pml4e[0];
		pml4e.read_access = 1;
		pml4e.write_access = 1;
		pml4e.execute_access = 1;
		pml4e.page_frame_number = MmGetPhysicalAddress(&ept_pages.pdpte).QuadPart >> 12;

		// Initialize PDPTEs
		for (size_t i = 0; i < ept_pd_count; i++)
		{
			ept_pdpte& pdpte = ept_pages.pdpte[i];
			pdpte.read_access = 1;
			pdpte.write_access = 1;
			pdpte.execute_access = 1;
			pdpte.page_frame_number = MmGetPhysicalAddress(&ept_pages.pde_2mb[i]).QuadPart >> 12;

			// Initialize PDEs (2 MB pages, split for non-uniform MTRR memory types)
			for (size_t j = 0; j < 512; j++)
			{
				ept_pde_2mb& pde_2mb = ept_pages.pde_2mb[i][j];
				pde_2mb.read_access = 1;
				pde_2mb.write_access = 1;
				pde_2mb.execute_access = 1;
				pde_2mb.large_page = 1;
				pde_2mb.page_frame_number = (i << 9) + j;

				//
				// 30.3.7.2 Memory Type Used for Translated Guest-Physical Addresses
				//
				// Memory type here effectively replaces MTRRs for EPT mappings since we implicitly set pde.disable_pat=0, therefore
				// a combination of PAT and memory type that we set here will determine the actual memory type used.
				//

				// Get the memory type for the range and check if it's uniform
				bool is_uniform = false;
				const uint8_t memory_type = hv::memory::get_mtrr_range_memory_type(mtrr_state, pde_2mb.page_frame_number << 21, 0x200000, is_uniform);

				if (is_uniform)
				{
					pde_2mb.memory_type = memory_type;
					continue;
				}

				LOG_INFO("Non-uniform MTRR memory type detected for 2 MB page at PFN 0x%llx, splitting into 4 KB pages", pde_2mb.page_frame_number);

				// Non-uniform memory type, need to split the 2 MB page into 4 KB pages
				ept_pte* pt = nullptr;
				const bool split_success = split_pde(ept_pages, &pde_2mb, &pt);
				if (!split_success)
				{
					// Failed to split PDE, apply the most restrictive memory type (which get_mtrr_range_memory_type returned)
					pde_2mb.memory_type = memory_type;
					LOG_ERROR("Failed to split EPT PDE for non-uniform MTRR memory type, applying most restrictive memory type %u", memory_type);
					continue;
				}

				// Initialize the PT entries with correct memory types
				for (size_t k = 0; k < 512; k++)
				{
					ept_pte& pte = pt[k];

					// Get the memory type for this 4 KB page
					bool pte_is_uniform = false;
					const uint8_t pte_memory_type = hv::memory::get_mtrr_range_memory_type(mtrr_state, pte.page_frame_number << 12, 0x1000, pte_is_uniform);

					// It should always be uniform for a single 4 KB page
					if (!pte_is_uniform)
					{
						LOG_ERROR("4KB pages should always have uniform MTRR memory type, something is wrong!");
						// TODO: Better assertion handling
						return;
					}

					pte.memory_type = pte_memory_type;
				}
			}
		}
	}

	void update_ept_memory_types(ept_pages& ept_pages, const memory::mtrr_state& mtrr_state)
	{
		for (size_t i = 0; i < ept_pd_count; i++)
		{
			ept_pdpte& pdpte = ept_pages.pdpte[i];
			pdpte.read_access = 1;
			pdpte.write_access = 1;
			pdpte.execute_access = 1;
			pdpte.page_frame_number = MmGetPhysicalAddress(&ept_pages.pde_2mb[i]).QuadPart >> 12;

			// Update PDEs
			for (size_t j = 0; j < 512; j++)
			{
				ept_pde_2mb& pde_2mb = ept_pages.pde_2mb[i][j];
				if (!pde_2mb.large_page)
				{
					continue;
				}

				// Get the memory type for the range and check if it's uniform
				bool is_uniform = false;
				const uint8_t memory_type = hv::memory::get_mtrr_range_memory_type(mtrr_state, pde_2mb.page_frame_number << 21, 0x200000, is_uniform);

				if (is_uniform)
				{
					pde_2mb.memory_type = memory_type;
					continue;
				}

				LOG_INFO("Non-uniform MTRR memory type detected for 2 MB page at PFN 0x%llx, splitting into 4 KB pages", pde_2mb.page_frame_number);

				ept_pte* pt = nullptr;
				const bool split_success = split_pde(ept_pages, &pde_2mb, &pt);
				if (!split_success)
				{
					// Failed to split PDE, apply the most restrictive memory type (which get_mtrr_range_memory_type returned)
					pde_2mb.memory_type = memory_type;
					LOG_ERROR("Failed to split EPT PDE for non-uniform MTRR memory type, applying most restrictive memory type %u", memory_type);
					continue;
				}
			}

			// Update split PT entries
			for (size_t split_index = 0; split_index < ept_pages.split_pte_used; split_index++)
			{
				ept_pte* pt = reinterpret_cast<ept_pte*>(&ept_pages.split_pte[split_index]);

				for (size_t k = 0; k < 512; k++)
				{
					ept_pte& pte = pt[k];

					// Get the memory type for this 4 KB page
					bool pte_is_uniform = false;
					const uint8_t pte_memory_type = hv::memory::get_mtrr_range_memory_type(mtrr_state, pte.page_frame_number << 12, 0x1000, pte_is_uniform);

					// It should always be uniform for a single 4 KB page
					if (!pte_is_uniform)
					{
						LOG_ERROR("4KB pages should always have uniform MTRR memory type, something is wrong!");
						// TODO: Better assertion handling
						return;
					}

					pte.memory_type = pte_memory_type;
				}
			}
		}
	}

	ept_pte* get_ept_pte(ept_pages& ept_pages, uint64_t gpa, bool force_split)
	{
		const memory::pml4_virtual_address pa = {reinterpret_cast<void*>(gpa)};

		if (pa.pml4_idx != 0)
			return nullptr;

		if (pa.pdpt_idx >= ept_pd_count)
			return nullptr;

		auto& pde_2mb = ept_pages.pde_2mb[pa.pdpt_idx][pa.pd_idx];

		if (pde_2mb.large_page)
		{
			if (!force_split)
				return nullptr;

			if (!split_pde(ept_pages, &pde_2mb))
			{
				LOG_ERROR("Failed to split EPT PDE for GPA 0x%llx", gpa);
				return nullptr;
			}
		}

		const auto pt = reinterpret_cast<ept_pte*>(memory::host_physical_base + (ept_pages.pde_split[pa.pdpt_idx][pa.pd_idx].page_frame_number << 12));

		return &pt[pa.pt_idx];
	}

	bool add_hook(ept_pages& ept_pages, uint64_t orig_pfn, uint64_t hook_pfn)
	{
		if (ept_pages.hook_count >= ept_max_hook_count)
		{
			LOG_ERROR("Maximum EPT hook count reached, cannot add more hooks");
			return false;
		}

		auto pte = get_ept_pte(ept_pages, orig_pfn << 12, true);
		if (!pte)
		{
			LOG_ERROR("Failed to get EPT PTE for original PFN 0x%llx, cannot add hook", orig_pfn);
			return false;
		}

		auto& hook = ept_pages.hooks[ept_pages.hook_count++];
		hook.orig_pfn = static_cast<uint32_t>(orig_pfn);
		hook.hook_pfn = static_cast<uint32_t>(hook_pfn);
		hook.pte = pte;

		pte->execute_access = 0;
		return true;
	}

	ept_hook* get_hook_by_orig_pfn(ept_pages& ept_pages, uint64_t orig_pfn)
	{
		for (size_t i = 0; i < ept_pages.hook_count; i++)
		{
			if (ept_pages.hooks[i].orig_pfn == orig_pfn)
			{
				return &ept_pages.hooks[i];
			}
		}
		return nullptr;
	}

	void deactivate_hooks(ept_pages& ept_pages)
	{
		for (size_t i = 0; i < ept_pages.hook_count; i++)
		{
			auto& hook = ept_pages.hooks[i];
			if (!hook.pte)
				continue;

			// Restore original PFN with full RWX permissions
			hook.pte->read_access = 1;
			hook.pte->write_access = 1;
			hook.pte->execute_access = 1;
			hook.pte->page_frame_number = hook.orig_pfn;

			// Mark hook as inactive
			hook.pte = nullptr;
		}
	}

	void reactivate_hooks(ept_pages& ept_pages)
	{
		for (size_t i = 0; i < ept_pages.hook_count; i++)
		{
			auto& hook = ept_pages.hooks[i];
			if (hook.pte)
				continue;  // Already active

			// Re-lookup the PTE for this hook's original PFN
			auto pte = get_ept_pte(ept_pages, static_cast<uint64_t>(hook.orig_pfn) << 12);
			if (!pte)
			{
				LOG_ERROR("Failed to re-lookup PTE for hook orig_pfn 0x%x during reactivation", hook.orig_pfn);
				continue;
			}

			// Re-apply hook: original PFN stays, but mark execute as forbidden so
			// the EPT violation handler can swap to hook_pfn on execute access
			pte->page_frame_number = hook.orig_pfn;
			pte->execute_access = 0;
			hook.pte = pte;
		}
	}

	bool enable_auto_trace(ept_pages& normal_ept, ept_pages& target_ept, uint64_t target_va, size_t target_size)
	{
		//
		// We set target pages as NX for the normal EPT, and set all pages as NX except the target pages for the target EPT. We force split PDEs as we go.
		//

		// Page align the target VA
		const uint64_t page_aligned_va = target_va & ~0xFFFULL;
		const uint64_t page_aligned_end = (target_va + target_size + 0xFFFULL) & ~0xFFFULL;
		const size_t aligned_size = page_aligned_end - page_aligned_va;

		LOG_INFO("Enabling auto trace for target VA 0x%llx (aligned 0x%llx), size 0x%llx (aligned 0x%llx)", target_va, page_aligned_va, target_size, aligned_size);

		// Set all EPT pages to non-executable first in the target range, we will set the target pages back to executable in the target EPT
		set_all_permissions(target_ept, true, true, false);

		// Iterate physical pages backing the target VA range
		for (size_t offset = 0; offset < aligned_size; offset += 0x1000)
		{
			const uint64_t current_va = page_aligned_va + offset;

			// Get GPA for the current VA
			auto gpa = memory::gva_to_gpa(hv::g_hv.system_cr3, current_va, nullptr);

			if (!gpa)
			{
				LOG_ERROR("Failed to translate target VA 0x%llx to GPA, it might be paged out.", current_va);
				continue;
			}

			auto pte_normal = get_ept_pte(normal_ept, gpa, true);
			auto pte_target = get_ept_pte(target_ept, gpa, true);
			if (!pte_normal || !pte_target)
			{
				LOG_ERROR("Failed to get EPT PTE for GPA 0x%llx. This shouldn't happen, bailing out. Are there enough free split pages ?", gpa);
				set_all_permissions(normal_ept, true, true, true);
				set_all_permissions(target_ept, true, true, true);
				return false;
			}

			pte_normal->execute_access = 0;
			pte_target->execute_access = 1;
		}

		return true;
	}

	void restore_auto_trace(ept_pages& normal_ept, ept_pages& target_ept)
	{
		set_all_permissions(normal_ept, true, true, true);
		set_all_permissions(target_ept, true, true, true);
	}
}  // namespace hv::ept