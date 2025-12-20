#include "hv.h"

namespace hv
{
	hypervisor g_hv;

	static bool allocate_vcpus()
	{
		auto logicalProcessorCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

		g_hv.vcpu_count = logicalProcessorCount;
		g_hv.vcpus = reinterpret_cast<vcpu::vcpu*>(memory::allocate_map_nx_pool(logicalProcessorCount * sizeof(vcpu::vcpu), g_hv.system_cr3));
		if (!g_hv.vcpus)
		{
			LOG_ERROR("Failed to allocate vCPUs");
			return false;
		}

		memset(g_hv.vcpus, 0, logicalProcessorCount * sizeof(vcpu::vcpu));

		return true;
	}

	bool start()
	{
		g_hv = {0};
		g_hv.system_cr3.flags = __readcr3();

		if (!allocate_vcpus())
		{
			LOG_ERROR("Failed to allocate vCPUs");
			return false;
		}

		if (!memory::map_host_page_tables(g_hv.host_page_tables, g_hv.system_cr3))
		{
			LOG_ERROR("Failed to map host page tables");
			return false;
		}

		utils::for_each_cpu(
			[&](size_t i)
			{
				g_hv.vcpus[i].core_id = i;
				if (!vcpu::virtualize(&g_hv.vcpus[i]))
				{
					LOG_ERROR("Failed to virtualize vCPU %lu", i);
					return false;
				}
			});

		return true;
	}

	void stop()
	{
	}
}  // namespace hv
