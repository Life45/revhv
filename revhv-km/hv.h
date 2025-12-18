#pragma once
#include "includes.h"
#include "memory.h"
#include "vcpu.h"

namespace hv
{
	constexpr ULONG pool_tag = 'REVH';
	struct hypervisor
	{
		memory::host_page_tables host_page_tables;

		cr3 system_cr3;

		// Dynamic array of vCPUs
		size_t vcpu_count;
		vcpu::vcpu* vcpus;
	};

	extern hypervisor g_hv;

	/// @brief Starts the hypervisor and virtualizes all processors
	bool start();

	/// @brief Stops the hypervisor and devirtualizes all processors
	void stop();
}  // namespace hv