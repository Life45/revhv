#include "memory.h"
#include "hv.h"

namespace hv::memory
{
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

		// Copy top half of system PML4 to host PML4
		PHYSICAL_ADDRESS system_pml4_pa;
		system_pml4_pa.QuadPart = system_cr3.address_of_page_directory;
		auto system_pml4 = MmGetVirtualForPhysical(system_pml4_pa);
		if (!system_pml4)
		{
			LOG_ERROR("Failed to get virtual address for system PML4");
			return false;
		}

		memcpy(&host_page_tables.pml4e[256], reinterpret_cast<uint8_t*>(system_pml4) + 256, 256 * sizeof(pml4e_64));
		return true;
	}
}  // namespace hv::memory