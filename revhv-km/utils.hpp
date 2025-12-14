#pragma once
#include "includes.h"

namespace utils
{
	/// @brief Canonicalizes a virtual address
	/// @param va Virtual address
	/// @return Canonicalized virtual address
	constexpr uint64_t canonicalize(const uint64_t va);

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

	namespace segment
	{
		/// @brief Calculates the base address of a segment
		/// @param gdt GDT register
		/// @param selector Segment selector
		/// @return Base address of the segment
		uint64_t base_address(const segment_descriptor_register_64& gdt, const segment_selector& selector);

		/// @brief Calculates the limit of a segment
		/// @param gdt GDT register
		/// @param selector Segment selector
		/// @return Limit of the segment
		uint32_t limit(const segment_descriptor_register_64& gdt, const segment_selector& selector);

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
		}
	}  // namespace segment
}  // namespace utils

#include "utils.inl"