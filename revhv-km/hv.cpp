#include "hv.h"

namespace hv
{
	hypervisor g_hv;

	bool start()
	{
		g_hv = {0};
		g_hv.system_cr3.flags = __readcr3();

		if (!memory::map_host_page_tables(g_hv.host_page_tables, g_hv.system_cr3))
		{
			LOG_ERROR("Failed to map host page tables");
			return false;
		}
	}

	void stop()
	{
	}
}  // namespace hv
