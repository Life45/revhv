#pragma once

namespace hv::isr
{
	// ISRs are defined in isr.asm

	extern "C"
	{
		void isr_0();
		void isr_1();
		void isr_2();
		void isr_3();
		void isr_4();
		void isr_5();
		void isr_6();
		void isr_7();
		void isr_8();
		void isr_10();
		void isr_11();
		void isr_12();
		void isr_13();
		void isr_14();
		void isr_16();
		void isr_17();
		void isr_18();
		void isr_19();
		void isr_20();
		void isr_21();
	}

	/// @brief Checks if a vector has an error code
	/// @param vector Vector to check
	/// @return True if the vector has an error code, false otherwise
	inline bool vector_has_error_code(uint16_t vector)
	{
		// #DF, #TS, #NP, #SS, #GP, #PF, #AC, #CP
		return vector == 8 || vector == 10 || vector == 11 || vector == 12 || vector == 13 || vector == 14 || vector == 17 || vector == 21;
	}
}  // namespace hv::isr