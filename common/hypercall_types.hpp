#pragma once
#include "trace_log.hpp"
using uint8_t = unsigned char;
using uint16_t = unsigned short;
using uint32_t = unsigned int;
using uint64_t = unsigned long long;

namespace hv::hypercall
{
	constexpr uint64_t HYPERCALL_KEY = 0x5245564856;  // "REVHV" ASCII

	constexpr size_t MAX_VMEM_CHUNK_SIZE = 0x1000;

	// Structure for a virtual memory read/write hypercall requests (Since 5 arguments with the hypercall id don't fit in registers)
	struct vmem_request
	{
		uint64_t guest_buffer;
		uint64_t target_va;
		size_t size;
		uint64_t target_cr3;
	};

	struct apic_info
	{
		uint64_t lapic_mmio_phys_base;
		bool x2apic;
	};

	/// Maximum number of per-vCPU exact-address transition configurations (must be a power of 2).
	constexpr uint32_t max_exact_transition_cfgs = 64;

	/// Scope for the at_config_ept_transition hypercall.
	enum class at_cfg_scope : uint64_t
	{
		generic = 0,	 // Update the fallback config applied when no exact address matches
		exact_addr = 1,	 // Install or replace a config for a single specific guest RIP
	};

	/// @brief Payload for the at_config_ept_transition hypercall.
	/// Passed by pointer in r8; the scope is passed directly in rdx.
	struct at_config_request
	{
		uint64_t exact_addr;			  // Target RIP (only when scope = exact_addr)
		::trace::ept_transition_cfg cfg;  // Data-field mapping to install
	};

	/// Maximum length of driver name for the onload auto-trace target.
	constexpr size_t max_onload_name_length = 64;

	/// @brief Request for the set_onload_target hypercall, driver_name is a null-terminated string.
	struct onload_target_request
	{
		char driver_name[max_onload_name_length];
	};

	enum hypercall_number : uint64_t
	{
		ping = 1,
		flush_standard_logs,
		read_vmem,
		write_vmem,
		ept_hook,
		enable_auto_trace,
		disable_auto_trace,
		flush_trace_logs,
		get_apic_info,
		test_host_df,  // Causes host double fault by thrashing the host RSP!
		at_config_ept_transition,
		set_onload_target,	  // Store driver name to watch for; auto-trace fires when it loads
		clear_onload_target,  // Clear any pending onload auto-trace target
		hypercall_max
	};
}  // namespace hv::hypercall