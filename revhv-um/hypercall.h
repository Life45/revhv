#pragma once
#include "includes.h"
#include "../common/hypercall_types.hpp"
#include "../common/logging_types.hpp"
#include "../common/trace_log.hpp"

namespace hv
{
	/// @brief Returns true when the hypervisor was detected on all cores at startup.
	/// Defined in main.cpp.
	bool is_present();
}  // namespace hv

namespace hv::hypercall
{
	/// @brief Executes VMCALL instruction
	/// @param number hypercall number
	/// @param arg1 argument 1
	/// @param arg2 argument 2
	/// @param arg3 argument 3
	/// @param key hypercall key (should be HYPERCALL_KEY for a valid hypercall)
	/// @return result of the hypercall
	extern "C" uint64_t __fastcall __vmcall(uint64_t number, uint64_t arg1 = 0, uint64_t arg2 = 0, uint64_t arg3 = 0, uint64_t key = HYPERCALL_KEY);

	/// @brief Pings the hv from the current core
	/// @return True if the hv responded correctly, false otherwise
	bool ping_hv();

	/// @brief Flushes the standard log messages into the provided buffer.
	/// @param messages Vector to flush the messages into
	/// @return The number of messages flushed into the vector
	bool flush_std_logs(std::vector<logging::standard_log_message>& messages);

	/// @brief Reads memory from target virtual address space into a local buffer.
	/// @param target_va Target virtual address in the selected address space
	/// @param out_buffer Destination buffer in current process
	/// @param size Number of bytes to read
	/// @param target_cr3 Target address space CR3 (0 = system CR3)
	/// @return Number of bytes read
	size_t read_vmemory(uint64_t target_va, void* out_buffer, size_t size, uint64_t target_cr3 = 0);

	/// @brief Writes memory from a local buffer into target virtual address space.
	/// @param target_va Target virtual address in the selected address space
	/// @param in_buffer Source buffer in current process
	/// @param size Number of bytes to write
	/// @param target_cr3 Target address space CR3 (0 = system CR3)
	/// @return Number of bytes written
	size_t write_vmemory(uint64_t target_va, const void* in_buffer, size_t size, uint64_t target_cr3 = 0);

	bool auto_trace_enable(uint64_t target_va, size_t target_size);
	bool auto_trace_disable();

	/// @brief Flushes binary trace entries from a specific core's ring buffer.
	/// The calling thread must be pinned to the target core for cache locality.
	/// @param core_id  Logical core whose trace buffer to drain
	/// @param out      Destination buffer (must hold at least max_entries elements)
	/// @param max_entries Maximum entries to drain per call
	/// @return Number of entries actually flushed (0 if empty or on error)
	uint64_t drain_trace_logs(uint32_t core_id, trace::entry* out, uint64_t max_entries);
}  // namespace hv::hypercall