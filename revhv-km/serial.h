#pragma once
#include "includes.h"

namespace serial
{
	// Standard 16550 UART port base addresses
	constexpr UINT16 SERIAL_COM_1 = 0x3F8;	// COM1
	constexpr UINT16 SERIAL_COM_2 = 0x2F8;	// COM2

	// Usual default names for serial ports (not guaranteed to be correct)
	constexpr auto SERIAL_COM_1_NAME = L"\\Device\\Serial0";
	constexpr auto SERIAL_COM_2_NAME = L"\\Device\\Serial1";

	// Register offsets
	constexpr UINT8 SERIAL_THR = 0;	 // Transmitter Holding Register (Write)
	constexpr UINT8 SERIAL_RBR = 0;	 // Receiver Buffer Register (Read)
	constexpr UINT8 SERIAL_DLL = 0;	 // Divisor Latch Low (Write)
	constexpr UINT8 SERIAL_DLH = 1;	 // Divisor Latch High (Write)
	constexpr UINT8 SERIAL_IER = 1;	 // Interrupt Enable Register
	constexpr UINT8 SERIAL_IIR = 2;	 // Interrupt Identification Register (Read)
	constexpr UINT8 SERIAL_FCR = 2;	 // FIFO Control Register (Write)
	constexpr UINT8 SERIAL_LCR = 3;	 // Line Control Register
	constexpr UINT8 SERIAL_MCR = 4;	 // Modem Control Register
	constexpr UINT8 SERIAL_LSR = 5;	 // Line Status Register
	constexpr UINT8 SERIAL_MSR = 6;	 // Modem Status Register
	constexpr UINT8 SERIAL_SCR = 7;	 // Scratch Register

	/// @brief Configures UART for 115200 Baud, 8 Data bits, No Parity, 1 Stop bit
	/// @param portBase The base address of the UART port (eg. SERIAL_COM_1)
	/// @param portName The name of the serial port (eg. SERIAL_COM_1_NAME)
	/// @return STATUS_SUCCESS if initialization was successful, status code if it failed
	NTSTATUS initialize(UINT16 portBase, const wchar_t* portName);

	/// @brief Deinitializes the serial port
	void deinitialize();

	/// @brief Writes a byte to the UART
	/// @param portBase The base address of the UART port (eg. SERIAL_COM_1)
	/// @param data The byte to write
	void write_byte(UINT16 portBase, UCHAR data);

	/// @brief Writes a string to the UART
	/// @param portBase The base address of the UART port (eg. SERIAL_COM_1)
	/// @param string The string to write
	void write_string(UINT16 portBase, const char* string);
}  // namespace serial