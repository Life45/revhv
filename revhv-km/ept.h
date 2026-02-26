#pragma once
#include "includes.h"
#include "memory.h"

namespace hv::ept
{
	constexpr int ept_pd_count = 64;		  // Basically 64 GB of memory via 2 MB pages
	constexpr int ept_split_pte_count = 256;  // Number of PTEs used for splitting 2 MB pages into 4 KB pages
	constexpr int ept_max_hook_count = 32;	  // Maximum number of EPT hooks

	struct ept_hook
	{
		uint32_t orig_pfn;
		uint32_t hook_pfn;
		ept_pte* pte;  // Pointer to the PTE that maps orig_pfn. nullptr if the hook is not currently active.
	};

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

		ept_hook hooks[ept_max_hook_count];
		size_t hook_count = 0;
	};

	/// @brief Splits a 2 MB page into 4 KB pages
	/// @param ept_pages The EPT pages to use for splitting
	/// @param pde_2mb The 2 MB page to split
	/// @param out_pt The output pointer to the new PT
	/// @return True if the split was successful, false otherwise. Returns false for already split PDEs too.
	bool split_pde(ept_pages& ept_pages, ept_pde_2mb* pde_2mb, ept_pte** out_pt);

	/// @brief Initializes the EPT structures
	/// @param ept_pages The EPT pages to initialize
	/// @param mtrr_state The MTRR state to use for memory type information
	void initialize_ept(ept_pages& ept_pages, const memory::mtrr_state& mtrr_state);

	/// @brief Updates the memory types of EPT entries based on the current MTRR state. Should be called after initialization and after any changes to MTRRs.
	/// @param ept_pages The EPT pages to update
	/// @param mtrr_state The MTRR state to use for memory type information
	void update_ept_memory_types(ept_pages& ept_pages, const memory::mtrr_state& mtrr_state);

	/// @brief Gets the EPT PTE for a given GPA, optionally splitting 2 MB pages if necessary. Returns nullptr if the entry is not present or if the split fails.
	/// @param ept_pages The EPT pages to search
	ept_pte* get_ept_pte(ept_pages& ept_pages, uint64_t gpa, bool force_split = false);

	/// @brief Adds an EPT hook by remapping the original PFN to the hook PFN. This will split 2 MB pages if necessary. Does not invept, caller should do it.
	/// @param ept_pages The EPT pages to modify
	bool add_hook(ept_pages& ept_pages, uint64_t orig_pfn, uint64_t hook_pfn);

	/// @brief Gets an EPT hook by the original PFN. Returns nullptr if not found.
	/// @param ept_pages The EPT pages to search
	ept_hook* get_hook_by_orig_pfn(ept_pages& ept_pages, uint64_t orig_pfn);

	/// @brief Sets up and enables auto tracing for given guest VA range. Does not invept, caller should do it after this.
	/// @param normal_ept The EPT used for normal execution
	/// @param target_ept The EPT used for when target is executing
	/// @param target_va The guest virtual address
	/// @param target_size The size of the guest virtual address range
	/// @return True if auto tracing was successfully enabled, false otherwise
	bool enable_auto_trace(ept_pages& normal_ept, ept_pages& target_ept, uint64_t target_va, size_t target_size);

	/// @brief Restores permissions changed by auto trace setup. Caller should switch to normal EPTP before or after this and invept to apply the changes.
	/// @param normal_ept The EPT used for normal execution
	/// @param target_ept The EPT used for when target is executing
	void restore_auto_trace(ept_pages& normal_ept, ept_pages& target_ept);
}  // namespace hv::ept