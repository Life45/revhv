#include "serial.h"

namespace serial
{
	// Handle to the serial port
	static HANDLE comHandle = NULL;

	NTSTATUS initialize(UINT16 portBase, const wchar_t* portName)
	{
		//
		// We still need to open a handle to the serial port to use it. My guess is that this is because
		// when the port is not used, the OS powers down the UART controller to D3, most likely unmapping the I/O range or
		// stopping the clock. Opening a handle keeps the UART controller powered on.
		//

		UNICODE_STRING uniPortName;
		RtlInitUnicodeString(&uniPortName, portName);

		OBJECT_ATTRIBUTES objAttr;
		InitializeObjectAttributes(&objAttr, &uniPortName, OBJ_KERNEL_HANDLE, NULL, NULL);

		IO_STATUS_BLOCK ioStatusBlock;
		auto status = ZwCreateFile(&comHandle, GENERIC_READ | GENERIC_WRITE, &objAttr, &ioStatusBlock, NULL, FILE_ATTRIBUTE_NORMAL,
								   0,  // no sharing
								   FILE_OPEN, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);

		if (!NT_SUCCESS(status))
		{
			return status;
		}

		// 2. Ensure DLAB is 0 before setting IER
		__outbyte(portBase + SERIAL_LCR, 0x00);

		// 3. Disable Interrupts
		__outbyte(portBase + SERIAL_IER, 0x00);

		// 4. Enable DLAB (Divisor Latch Access Bit)
		__outbyte(portBase + SERIAL_LCR, 0x80);

		// 5. Set Divisor for 115200 Baud (Divisor = 1)
		__outbyte(portBase + SERIAL_DLL, 0x01);
		__outbyte(portBase + SERIAL_DLH, 0x00);

		// 6. Clear DLAB and set 8 bits, No Parity, 1 Stop Bit (8N1)
		__outbyte(portBase + SERIAL_LCR, 0x03);

		// 7. Enable FIFO, clear them, 14-byte threshold
		__outbyte(portBase + SERIAL_FCR, 0xC7);

		// 8. Enable IRQs (required for some hardware to drive lines), RTS/DSR set
		// Bit 3 (OUT2) is often required for interrupts, but also sometimes for output to work at all.
		__outbyte(portBase + SERIAL_MCR, 0x0B);

		// 9. Clear any pending status/data
		(void)__inbyte(portBase + SERIAL_LSR);
		(void)__inbyte(portBase + SERIAL_RBR);
		(void)__inbyte(portBase + SERIAL_IIR);
		(void)__inbyte(portBase + SERIAL_MSR);

		return STATUS_SUCCESS;
	}

	void deinitialize()
	{
		if (comHandle != NULL)
		{
			(void)ZwClose(comHandle);
			comHandle = NULL;
		}
	}

	void write_byte(UINT16 portBase, UCHAR data)
	{
		// Busy wait until the transmitter holding register is empty.
		while (!(__inbyte(portBase + SERIAL_LSR) & 0x20))
		{
			;
		}

		__outbyte(portBase + SERIAL_THR, data);
	}

	void write_string(UINT16 portBase, const char* string)
	{
		for (SIZE_T i = 0; string[i] != '\0'; ++i)
		{
			if (string[i] == '\n')
			{
				write_byte(portBase, '\r');
			}

			write_byte(portBase, string[i]);
		}
	}
}  // namespace serial