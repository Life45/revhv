#pragma once
#include "includes.h"

namespace utils
{
	/// @brief Canonicalizes a virtual address
	/// @param va Virtual address
	/// @return Canonicalized virtual address
	constexpr uint64_t canonicalize(const uint64_t va);

	/// @brief Memset function relying only on intrinsics
	/// @param dest Destination pointer
	/// @param value Value to set
	/// @param size Size of the memory to set
	void memset(void* dest, uint8_t value, size_t size);

	/// @brief Memcpy function relying only on intrinsics
	/// @param dest Destination pointer
	/// @param src Source pointer
	/// @param size Size of the memory to copy
	void memcpy(void* dest, const void* src, size_t size);

	/// @brief Sets a bit in the value
	/// @tparam T Type of the value
	/// @param value Value to set the bit in
	/// @param bit Bit to set
	template <typename T>
	void set_bit(T& value, const uint64_t bit);

	/// @brief Clears a bit in the value
	/// @tparam T Type of the value
	/// @param value Value to clear the bit in
	/// @param bit Bit to clear
	template <typename T>
	void clear_bit(T& value, const uint64_t bit);

	/// @brief Checks if a bit is set in the value
	/// @param value Value to check
	/// @param bit Bit to check
	/// @return True if the bit is set, false otherwise
	bool is_bit_set(const uint64_t value, const uint64_t bit);

	/// @brief Executes a function on each CPU core
	/// @tparam Func Function type to execute
	/// @param func Function to execute on each CPU core
	template <typename Func>
	void for_each_cpu(Func func);

	/// @brief Unblocks NMIs via IRETQ and busy waits forever with interrupts enabled
	/// @param cs_selector Code segment selector from restore context
	extern "C" void _declspec(noreturn) cpu_hang_unblock_nmi(uint16_t cs_selector);

	namespace segment
	{
		/// @brief Calculates the base address of a segment
		/// @param gdt GDT register
		/// @param selector Segment selector
		/// @return Base address of the segment
		uint64_t base_address(const segment_descriptor_register_64& gdt, const segment_selector& selector);

		/// @brief Calculates the access rights of a segment for VMCS
		/// @param gdt GDT register
		/// @param selector Segment selector
		/// @return Access rights of the segment to be used in the VMCS
		vmx_segment_access_rights access_rights(const segment_descriptor_register_64& gdt, const segment_selector& selector);

		extern "C"
		{
			segment_selector read_cs();
			segment_selector read_ss();
			segment_selector read_ds();
			segment_selector read_es();
			segment_selector read_fs();
			segment_selector read_gs();
			segment_selector read_tr();
			segment_selector read_ldtr();

			void write_ds(uint16_t selector);
			void write_es(uint16_t selector);
			void write_fs(uint16_t selector);
			void write_gs(uint16_t selector);
			void write_tr(uint16_t selector);
			void write_ldtr(uint16_t selector);
		}
	}  // namespace segment
}  // namespace utils

#include "utils.inl"