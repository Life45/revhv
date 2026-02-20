#pragma once
#include "includes.h"
#include "memory.h"
#include "vcpu.h"
#include "sync.hpp"
#include "logging.h"

namespace hv
{
	/// @brief When enabled, LOG_xxx macros will also output to the serial port.
	constexpr bool serial_output_enabled = true;

	struct hypervisor
	{
		memory::host_page_tables host_page_tables;

		cr3 system_cr3;

		sync::atomic_int crash_in_progress;
		sync::atomic_int crash_ack_core_count;

		// Dynamic array of vCPUs
		size_t vcpu_count;
		vcpu::vcpu* vcpus;

		// Standard logger shared across all cores
		logging::standard_logger logger;
	};

	extern hypervisor g_hv;

	/// @brief Starts the hypervisor and virtualizes all processors
	bool start();

	/// @brief Stops the hypervisor and devirtualizes all processors
	void stop();
}  // namespace hv