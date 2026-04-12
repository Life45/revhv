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

	/// @brief When enabled, the hypervisor tries to hide its timing footprint with TSC offsets and preemption timer.
	/// 	   Preemption timer is sometimes not exposed in nested virtualization, this option can be disabled to avoid launch failure.
	constexpr bool tsc_hiding_enabled = true;

	/// @brief Shared state for the auto-trace-on-driver-load feature
	struct onload_target_state
	{
		sync::spin_lock lock;
		bool active;
		char name[hypercall::max_onload_name_length];
	};

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

		// Onload auto-trace target, checked by the MmLoadSystemImage hook
		onload_target_state onload_target;
	};

	extern hypervisor g_hv;

	/// @brief Starts the hypervisor and virtualizes all processors
	bool start();

	/// @brief Stops the hypervisor and devirtualizes all processors
	void stop();
}  // namespace hv