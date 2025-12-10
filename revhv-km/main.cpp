#include "includes.h"
#include "serial.h"

EXTERN_C VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UNREFERENCED_PARAMETER(DriverObject);
	LOG_INFO("Driver unloading...");
	serial::deinitialize();

	LOG_INFO_DBGPRINT("Driver unloaded");
}

EXTERN_C NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);

	if (DriverObject)
	{
		DriverObject->DriverUnload = DriverUnload;
	}

	if (!NT_SUCCESS(serial::initialize(serial::SERIAL_COM_1, serial::SERIAL_COM_1_NAME)))
	{
		LOG_ERROR_DBGPRINT("Failed to initialize serial port %u", serial::SERIAL_COM_1);
		return STATUS_UNSUCCESSFUL;
	}

	LOG_INFO_DBGPRINT("Initialized serial port %u", serial::SERIAL_COM_1);

	LOG_INFO("Driver initialized");
	return STATUS_SUCCESS;
}