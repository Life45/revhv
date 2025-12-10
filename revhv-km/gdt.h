#pragma once
#include "includes.h"

namespace hv::gdt
{
	constexpr segment_selector null_segment_selector = {0, 0, 0};
	constexpr segment_selector host_cs = {0, 0, 1};
	constexpr segment_selector host_ss = {0, 0, 2};
	// Keep in mind TSS is a system descriptor therefore extended to 16 bytes, thus you can think as it occupies 2 index slots in the GDT
	constexpr segment_selector host_tr = {0, 0, 3};

	constexpr auto host_descriptor_count = 5;

	/// @brief Initializes the host TSS
	/// @param tss Pointer to the TSS
	/// @param ist1 Pointer to the IST(1)
	/// @param ist2 Pointer to the IST(2)
	/// @param ist3 Pointer to the IST(3)
	/// @param ist_size Size of one IST
	void initialize_tss(task_state_segment_64* tss, const uint8_t* ist1, const uint8_t* ist2, const uint8_t* ist3, size_t ist_size);

	/// @brief Initializes the host GDT
	/// @param gdt Pointer to the GDT
	/// @param tss Pointer to the TSS
	void initialize(segment_descriptor_32* gdt, task_state_segment_64* tss);
}  // namespace hv::gdt