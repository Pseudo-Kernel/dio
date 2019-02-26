
#include <ntddk.h>
//#include <intrin.h>
#include "iomap.h"
#include "../Include/dioctl.h"

#define DIO_ALLOC(_size)						ExAllocatePoolWithTag(NonPagedPool, (_size), 'OIDp')
#define DIO_FREE(_addr)							ExFreePoolWithTag((_addr), 'OIDp')

#define	DIO_IN_DEBUG_BREAKPOINT()	{	\
	if (!(*KdDebuggerNotPresent))		\
		__debugbreak();					\
}


C_ASSERT( sizeof(PEPROCESS) == 4 );

//
// Global variables.
//

PDRIVER_OBJECT DioDriverObject = NULL;
PDEVICE_OBJECT DioDeviceObject = NULL;

UNICODE_STRING DioDeviceName;
UNICODE_STRING DioDosDeviceName;

IO_ACCESS_MAP *DioIoAccessMap = NULL;

//KSPIN_LOCK DioProcessLock;
volatile PEPROCESS DioRegisteredProcess = NULL;


//
// Our I/O control packet helper function.
//

BOOLEAN
DioValidatePacketBuffer(
	IN DIO_PACKET *Packet, 
	IN ULONG InputBufferLength, 
	IN ULONG IoControlCode)
{
	switch (IoControlCode)
	{
	case DIO_IOCTL_SET_PORTACCESS:
		{
			ULONG Count;

			if (InputBufferLength < sizeof(Packet->PortAccess))
				return FALSE;

			Count = Packet->PortAccess.Count;
			if (Count > DIO_PORTACCESS_ENTRY_MAXIMUM)
				return FALSE;

			if (Count * sizeof(DIO_PORTACCESS_ENTRY) > InputBufferLength)
				return FALSE;
		}
		break;

	case DIO_IOCTL_RESET_PORTACCESS:
		// Do nothing
		break;

	default:
		return FALSE;
	}

	return TRUE;
}

BOOLEAN
DioSetIoAccessMap(
	IN DIO_PACKET_PORTACCESS *PortAccess OPTIONAL, 
	IN OUT IO_ACCESS_MAP *AccessMap)
{
	ULONG Count;
	DIO_PORTACCESS_ENTRY *Entry;
	ULONG i;

	if (!PortAccess)
	{
		// Zero out the access map.
	//	RtlZeroMemory(AccessMap->Map, sizeof(AccessMap->Map));
		RtlFillMemory(AccessMap->Map, sizeof(AccessMap->Map), -1);
		return TRUE;
	}

	if (PortAccess->Count > DIO_PORTACCESS_ENTRY_MAXIMUM)
		return FALSE;

	RtlFillMemory(AccessMap->Map, sizeof(AccessMap->Map), -1);

	Entry = (DIO_PORTACCESS_ENTRY *)(PortAccess + 1);

	for (i = 0; i < PortAccess->Count; i++)
	{
		ULONG Address = (ULONG)(USHORT)Entry[i].StartAddress;
		ULONG AddressEnd = (ULONG)(USHORT)Entry[i].EndAddress;

		while (Address < AddressEnd)
		{
			AccessMap->Map[Address >> 3] &= ~(UCHAR)(1 << (Address & 0x07));
			Address++;
		}
	}

	return TRUE;
}

BOOLEAN
DioRegisterSelf(
	VOID)
{
	PEPROCESS CurrentProcess = PsGetCurrentProcess();
	PEPROCESS RegisteredProcess = NULL;

	RegisteredProcess = (PEPROCESS)_InterlockedCompareExchange((LONG *)&DioRegisteredProcess, (LONG)CurrentProcess, 0);

	if (RegisteredProcess != NULL && RegisteredProcess != CurrentProcess)
		return FALSE;

	ObfReferenceObject(CurrentProcess);
	return TRUE;
}

BOOLEAN
DioUnregister(
	IN BOOLEAN DisableIoAccess)
{
	PEPROCESS RegisteredProcess = NULL;
	PEPROCESS CurrentProcess = PsGetCurrentProcess();
	
	RegisteredProcess = (PEPROCESS)_InterlockedCompareExchange((LONG *)&DioRegisteredProcess, 0, (LONG)CurrentProcess);
	if (!RegisteredProcess)
		return FALSE;

	if (DisableIoAccess)
		Ke386IoSetAccessProcess(RegisteredProcess, 0);

	ObfDereferenceObject(RegisteredProcess);
	return TRUE;
}

BOOLEAN
DioForceUnregister(
	IN HANDLE UnregisterProcessId, 
	IN BOOLEAN DisableIoAccess)
{
	PEPROCESS RegisteredProcess = DioRegisteredProcess;
	if (RegisteredProcess != NULL)
	{
		if (UnregisterProcessId == PsGetProcessId(RegisteredProcess))
		{
			if (DisableIoAccess)
				Ke386IoSetAccessProcess(RegisteredProcess, 0);

			ObfDereferenceObject(RegisteredProcess);

			DioRegisteredProcess = NULL;
			return TRUE;
		}
	}

	return FALSE;
}


//
// Our callback function.
//

VOID
DioCreateProcessNotifyRoutine(
	IN HANDLE ParentId, 
	IN HANDLE ProcessId, 
	IN BOOLEAN Create)
{
	if (!Create)
	{
		DioForceUnregister(ProcessId, TRUE);
	}
}


//
// Our dispatch function.
//

NTSTATUS
DioDispatchNotSupported(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp)
{
	return STATUS_NOT_SUPPORTED;
}


NTSTATUS
DioDispatchCreate(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp)
{
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	IofCompleteRequest(Irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

NTSTATUS
DioDispatchClose(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp)
{
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	IofCompleteRequest(Irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

NTSTATUS
DioDispatchIoControl(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp)
{
	PIO_STACK_LOCATION IoStackLocation;
	ULONG IoControlCode;
	ULONG InputBufferLength;
	DIO_PACKET *Packet;
	NTSTATUS Status;

	DIO_IN_DEBUG_BREAKPOINT();

	Status = STATUS_SUCCESS;
	IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
	IoControlCode = IoStackLocation->Parameters.DeviceIoControl.IoControlCode;
	InputBufferLength = IoStackLocation->Parameters.DeviceIoControl.InputBufferLength;

	do
	{
		//
		// 1. Make sure that we're using buffered IOCTL.
		//

		if (METHOD_FROM_CTL_CODE(IoControlCode) != METHOD_BUFFERED)
		{
			Status = STATUS_NOT_SUPPORTED;
			break;
		}


		//
		// 2. Validate the buffer.
		//

		Packet = (DIO_PACKET *)Irp->AssociatedIrp.SystemBuffer;
		if (!DioValidatePacketBuffer(Packet, InputBufferLength, IoControlCode))
		{
			Status = STATUS_INVALID_PARAMETER;
			break;
		}

		switch (IoControlCode)
		{
		case DIO_IOCTL_SET_PORTACCESS:
			if (!DioRegisterSelf())
			{
				Status = STATUS_ACCESS_DENIED;
				break;
			}

			// Create the I/O access map from packet.
			if (!DioSetIoAccessMap(&Packet->PortAccess, DioIoAccessMap))
			{
				Status = STATUS_INVALID_PARAMETER;
				break;
			}

			Ke386SetIoAccessMap(1, DioIoAccessMap);
			Ke386IoSetAccessProcess(PsGetCurrentProcess(), 1);
			break;

		case DIO_IOCTL_RESET_PORTACCESS:
			if (!DioSetIoAccessMap(NULL, DioIoAccessMap))
			{
				Status = STATUS_INVALID_PARAMETER;
				break;
			}

			Ke386IoSetAccessProcess(PsGetCurrentProcess(), 0);
			break;

		default:
			Status = STATUS_NOT_SUPPORTED;
		}
	} while(FALSE);


	//
	// 3. Complete the request.
	//

	Irp->IoStatus.Status = Status;
	Irp->IoStatus.Information = 0;

	IofCompleteRequest(Irp, IO_NO_INCREMENT);


	return STATUS_SUCCESS;
}

VOID
DioDriverUnload(
	IN PDRIVER_OBJECT DriverObject)
{
	DbgPrint("%s\n", __FUNCTION__);

	IoDeleteSymbolicLink(&DioDosDeviceName);
	IoDeleteDevice(DioDeviceObject);

	PsSetCreateProcessNotifyRoutine(DioCreateProcessNotifyRoutine, TRUE);

	DioUnregister(TRUE);

	// FIXME : Is it safe to free the map?
	DIO_FREE(DioIoAccessMap);
}


//
// Driver main entry.
//

NTSTATUS
DriverEntry(
	IN PDRIVER_OBJECT DriverObject, 
	IN PUNICODE_STRING RegistryPath)
{
	NTSTATUS Status;
	IO_ACCESS_MAP *AccessMap;
	PDEVICE_OBJECT DeviceObject;
	ULONG i;

	DbgPrint("%s\n", __FUNCTION__);

	RtlInitUnicodeString(&DioDeviceName, L"\\Device\\Dioport");
	RtlInitUnicodeString(&DioDosDeviceName, L"\\DosDevices\\Dioport");

	AccessMap = (IO_ACCESS_MAP *)DIO_ALLOC(sizeof(*AccessMap));
	if (!AccessMap)
	{
		DbgPrint("ExAllocatePoolWithTag failed\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	Status = IoCreateDevice(DriverObject, 0, &DioDeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
	if (!NT_SUCCESS(Status))
	{
		DbgPrint("IoCreateDevice failed\n");
		DIO_FREE(AccessMap);

		return Status;
	}

	Status = IoCreateSymbolicLink(&DioDosDeviceName, &DioDeviceName);
	if (!NT_SUCCESS(Status))
	{
		IoDeleteDevice(DeviceObject);
		DIO_FREE(AccessMap);
		
		DbgPrint("IoCreateSymbolicLink failed\n");
		return Status;
	}

	for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
		DriverObject->MajorFunction[i] = DioDispatchNotSupported;

	DriverObject->MajorFunction[IRP_MJ_CREATE] = DioDispatchCreate;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = DioDispatchClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DioDispatchIoControl;
	DriverObject->DriverUnload = DioDriverUnload;

	//
	// Initialize the globals.
	//

//	KeInitializeSpinLock(&DioProcessLock);

	DioDriverObject = DriverObject;
	DioDeviceObject = DeviceObject;
	DioIoAccessMap = AccessMap;

	return Status;
}

