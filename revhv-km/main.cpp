#include "includes.h"
#include "serial.h"
#include "hv.h"
#include "hooks.h"

EXTERN_C VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UNREFERENCED_PARAMETER(DriverObject);
	LOG_INFO("Driver unloading...");
	serial::deinitialize();

	LOG_INFO_DBGPRINT("Driver unloaded");
}

VOID thread_entry(NTSTATUS* Status)
{
	if (!hv::start())
	{
		LOG_ERROR("Failed to virtualize processors");
		*Status = STATUS_UNSUCCESSFUL;
	}

	*Status = STATUS_SUCCESS;
	PsTerminateSystemThread(STATUS_SUCCESS);
}

EXTERN_C NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);

	if (hv::serial_output_enabled)
	{
		if (!NT_SUCCESS(serial::initialize(serial::SERIAL_COM_1, serial::SERIAL_COM_1_NAME)))
		{
			LOG_ERROR_DBGPRINT("Failed to initialize serial port %u", serial::SERIAL_COM_1);
			return STATUS_UNSUCCESSFUL;
		}

		LOG_INFO_DBGPRINT("Initialized serial port %u", serial::SERIAL_COM_1);
	}

	LOG_INFO("Driver initialized, starting the hypervisor...");

	//
	// Since manual mappers usually call DriverEntry from the context of the mapper process,
	// we create a system thread to accurately capture the system CR3, etc.
	// While this opens up some "detection vectors" for anti-cheats, etc. It is not entirely in the scope of this project.
	//

	NTSTATUS status = STATUS_UNSUCCESSFUL;

	HANDLE thread_handle;
	OBJECT_ATTRIBUTES thread_attributes;
	InitializeObjectAttributes(&thread_attributes, nullptr, OBJ_KERNEL_HANDLE, nullptr, nullptr);

	if (!NT_SUCCESS(PsCreateSystemThread(&thread_handle, THREAD_ALL_ACCESS, &thread_attributes, nullptr, nullptr, reinterpret_cast<PKSTART_ROUTINE>(thread_entry), &status)))
	{
		LOG_ERROR("Failed to create the initialization system thread");
		return STATUS_UNSUCCESSFUL;
	}

	PETHREAD thread_object = nullptr;
	NTSTATUS ref_status = ObReferenceObjectByHandle(thread_handle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, reinterpret_cast<PVOID*>(&thread_object), nullptr);

	// Handle is no longer needed once we have the object pointer
	ZwClose(thread_handle);

	if (!NT_SUCCESS(ref_status))
	{
		LOG_ERROR("Failed to reference thread object");
		return STATUS_UNSUCCESSFUL;
	}

	NTSTATUS wait_status = KeWaitForSingleObject(thread_object, Executive, KernelMode, FALSE, nullptr);

	ObDereferenceObject(thread_object);

	if (!NT_SUCCESS(wait_status))
	{
		LOG_ERROR("Failed to wait for the initialization thread to finish");
		return STATUS_UNSUCCESSFUL;
	}

	if (!NT_SUCCESS(status))
	{
		LOG_ERROR("Hypervisor failed to start");
		return STATUS_UNSUCCESSFUL;
	}

	LOG_INFO_DBGPRINT("Hypervisor started successfully");

	hv::hooks::initialize();

	return STATUS_SUCCESS;
}