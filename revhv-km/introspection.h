#pragma once
#include "includes.h"

namespace hv::introspection
{
	/// @brief Reads from a guest virtual address for a given guest CR3 into a host buffer
	/// @param guest_cr3 CR3 of the guest to read from
	/// @param guest_virtual_address Guest virtual address to read from
	/// @param buffer Host buffer to read into
	/// @param size Size of the memory to read
	/// @return The number of bytes successfully read into the buffer
	size_t read_guest_virtual(const cr3& guest_cr3, const void* guest_virtual_address, void* buffer, size_t size);

	/// @brief Reads from a guest virtual address using the current guest CR3 into a host buffer
	/// @param guest_virtual_address Guest virtual address to read from
	/// @param buffer Host buffer to read into
	/// @param size Size of the memory to read
	/// @return The number of bytes successfully read into the buffer
	size_t read_guest_virtual(const void* guest_virtual_address, void* buffer, size_t size);

	/// @brief Writes to a guest virtual address for a given guest CR3 from a host buffer
	/// @param guest_cr3 CR3 of the guest to write to
	/// @param guest_virtual_address Guest virtual address to write to
	/// @param buffer Host buffer to write from
	/// @param size Size of the memory to write
	/// @return The number of bytes successfully written from the buffer
	size_t write_guest_virtual(const cr3& guest_cr3, void* guest_virtual_address, const void* buffer, size_t size);

	/// @brief Writes to a guest virtual address using the current guest CR3 from a host buffer
	/// @param guest_virtual_address Guest virtual address to write to
	/// @param buffer Host buffer to write from
	/// @param size Size of the memory to write
	/// @return The number of bytes successfully written from the buffer
	size_t write_guest_virtual(void* guest_virtual_address, const void* buffer, size_t size);
}  // namespace hv::introspection