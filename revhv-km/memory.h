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

	/// @brief Struct representing the state of MTRRs
	struct mtrr_state
	{
		ia32_mtrr_def_type_register def_type;
		bool fixed_supported;

		// Fixed-range MTRRs
		//   [ 0.. 7] FIX64K  – 8  x 64 KB 0x00000–0x7FFFF
		//   [ 8..23] FIX16K  – 16 x 16 KB 0x80000–0xBFFFF
		//   [24..87] FIX4K   – 64 x  4 KB 0xC0000–0xFFFFF
		uint8_t fixed[IA32_MTRR_FIX_COUNT];

		// Variable-range MTRRs
		struct
		{
			ia32_mtrr_physbase_register base;
			ia32_mtrr_physmask_register mask;
		} variable[IA32_MTRR_VARIABLE_COUNT];

		uint8_t variable_count;
	};

	/// @brief Maps the host page tables by copying the top half of system PML4, and the entire physical memory in a single PML4E via 1GB large pages.
	/// @return True if the host page tables were mapped successfully, false otherwise
	bool map_host_page_tables(host_page_tables& host_page_tables, const cr3& system_cr3);

	/// @brief Allocates an NX pool and maps it to the host page tables
	/// @note Do not use this function after vmlaunch
	/// @param size Size of the memory to allocate
	/// @return True if the memory was allocated and mapped successfully, false otherwise
	void* allocate_map_nx_pool(size_t size, const cr3& system_cr3);

	/// @brief Reads the MTRR state of the current processor and stores it in the provided mtrr_state struct
	/// @param state Reference to an mtrr_state struct where the MTRR state will be stored
	void read_mtrrs(mtrr_state& state);

	/// @brief Gets the memory type for a given physical address and size based on the MTRR state
	/// @param state Reference to the mtrr_state struct containing the MTRR state
	/// @param physical_address The physical address to get the memory type for
	/// @param target_size The size of the memory range to get the memory type for
	/// @param is_uniform Reference to a bool that will be set to true if the memory type is uniform across the entire range, false otherwise
	/// @return The memory type for the given physical address and size. If not uniform, the memory type of most restrictive MTRR will be returned.
	uint8_t get_mtrr_range_memory_type(const mtrr_state& state, uint64_t physical_address, size_t target_size, bool& is_uniform);

	/// @brief Translates a guest virtual address to a guest physical address using the provided CR3
	/// @param guest_cr3 CR3 of the guest to use for translation
	/// @param gva Guest virtual address to translate
	/// @param offset_to_next_page Offset to next page (basically amount of safe bytes to read, then a second translation will be needed)
	/// @return The guest physical address corresponding to the given guest virtual address, or 0 if the translation failed
	uint64_t gva_to_gpa(const cr3& guest_cr3, uint64_t gva, size_t* offset_to_next_page = nullptr);

	/// @brief Translates a guest virtual address to a host-mapped virtual address using the provided CR3
	/// @param guest_cr3 CR3 of the guest to use for translation
	/// @param gva Guest virtual address to translate
	/// @param offset_to_next_page Offset to next page (basically amount of safe bytes to read, then a second translation will be needed)
	/// @return The host-mapped virtual address corresponding to the given guest virtual address, or nullptr if the translation failed
	uint64_t gva_to_hva(const cr3& guest_cr3, uint64_t gva, size_t* offset_to_next_page = nullptr);
}  // namespace hv::memory