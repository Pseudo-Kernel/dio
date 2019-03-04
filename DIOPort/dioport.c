
#include <ntddk.h>
#include "iomap.h"
#include "../Include/dioctl.h"

#define	DIO_DENY_CONVENTIONAL_PORT_ACCESS
#define	DIO_SUPPORT_UNLOAD

#pragma message("Define DIO_DENY_CONVENTIONAL_PORT_ACCESS if you want to deny port access for conventional address.")
#pragma message("Define DIO_SUPPORT_UNLOAD if you want to make driver unloadable.")


#define DIO_ALLOC(_size)						ExAllocatePoolWithTag(NonPagedPool, (_size), 'OIDp')
#define DIO_FREE(_addr)							ExFreePoolWithTag((_addr), 'OIDp')

#define	DIO_TRACE(_fmt, ...)					DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, (_fmt), __VA_ARGS__)

#define	DIO_IN_DEBUG_BREAKPOINT() {		\
	if (!(*KdDebuggerNotPresent)) {		\
		__debugbreak();					\
	}									\
}


C_ASSERT( sizeof(PEPROCESS) == 4 );

//
// Global variables.
//

PDRIVER_OBJECT DiopDriverObject = NULL;
PDEVICE_OBJECT DiopDeviceObject = NULL;

UNICODE_STRING DiopDeviceName;
UNICODE_STRING DiopDosDeviceName;

IO_ACCESS_MAP *DiopIoAccessMap = NULL;

KSPIN_LOCK DiopForceUnregisterLock;
volatile PEPROCESS DiopRegisteredProcess = NULL;

#ifdef DIO_DENY_CONVENTIONAL_PORT_ACCESS
DIO_PORTACCESS_ENTRY DiopConventionalPortAccessListForDeny[] = {
	{ 0x0000, 0x6fff }, /* 0x0000 ~ 0x6fff */
	/* Do not deny DIO board address 0x7000 ~ 0x7000 + 0x10 * BoardCount,
	   where the BoardCount = 8 (total 1024 channels) */
	{ 0x7080, 0xffff }, /* 0x7080 ~ 0xffff */
};
#endif

//
// Our I/O control packet helper function.
//

BOOLEAN
DiopValidatePacketBuffer(
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

VOID
DiopSetIoAccessMap(
	IN DIO_PORTACCESS_ENTRY *PortAccessEntry, 
	IN OUT IO_ACCESS_MAP *AccessMap, 
	IN BOOLEAN DenyAccess)
{
	ULONG Address = (ULONG)(USHORT)PortAccessEntry->StartAddress;
	ULONG AddressEnd = (ULONG)(USHORT)PortAccessEntry->EndAddress;

	if (AddressEnd > 0xffff)
		AddressEnd = 0xffff;

	if (Address <= AddressEnd)
	{
		DIO_TRACE("%s: %s port access 0x%04x - 0x%04x\n", 
			__FUNCTION__, DenyAccess ? "Deny" : "Allow", 
			Address, AddressEnd);
	}
	else
	{
		DIO_TRACE("%s: Port access range 0x%04x - 0x%04x will be ignored\n", 
			__FUNCTION__, Address, AddressEnd);
	}

	if (DenyAccess)
	{
		while (Address <= AddressEnd)
		{
			AccessMap->Map[Address >> 3] |= (UCHAR)(1 << (Address & 0x07));
			Address++;
		}
	}
	else
	{
		while (Address <= AddressEnd)
		{
			AccessMap->Map[Address >> 3] &= ~(UCHAR)(1 << (Address & 0x07));
			Address++;
		}
	}
}

VOID
DioDenyConventionalPortAccess(
	IN OUT IO_ACCESS_MAP *AccessMap)
{
#ifdef DIO_DENY_CONVENTIONAL_PORT_ACCESS
	ULONG i;
	for (i = 0; i < ARRAYSIZE(DiopConventionalPortAccessListForDeny); i++)
		DiopSetIoAccessMap(&DiopConventionalPortAccessListForDeny[i], AccessMap, TRUE);
#endif
}

BOOLEAN
DioSetIoAccessMapByPacket(
	IN DIO_PACKET_PORTACCESS *PortAccess OPTIONAL, 
	IN OUT IO_ACCESS_MAP *AccessMap)
{
	ULONG i;

	RtlFillMemory(AccessMap->Map, sizeof(AccessMap->Map), -1);

	if (!PortAccess)
	{
		DIO_TRACE("%s: Any port access will be denied\n", __FUNCTION__);
		return TRUE;
	}

	if (PortAccess->Count > DIO_PORTACCESS_ENTRY_MAXIMUM)
	{
		DIO_TRACE("%s: Maximum entry count exceeded\n", __FUNCTION__);
		return FALSE;
	}
	
	for (i = 0; i < PortAccess->Count; i++)
		DiopSetIoAccessMap(&PortAccess->Entry[i], AccessMap, FALSE);

	// Deny port access for conventional address.
	DioDenyConventionalPortAccess(AccessMap);

	return TRUE;
}

BOOLEAN
DioRegisterSelf(
	VOID)
{
	PEPROCESS CurrentProcess = PsGetCurrentProcess();
	PEPROCESS RegisteredProcess = NULL;

	RegisteredProcess = (PEPROCESS)_InterlockedCompareExchange((LONG *)&DiopRegisteredProcess, (LONG)CurrentProcess, 0);

	if (RegisteredProcess != NULL && RegisteredProcess != CurrentProcess)
	{
		DIO_TRACE("%s: Failed to register %d because it is already registered\n", 
			__FUNCTION__, PsGetProcessId(CurrentProcess));
		return FALSE;
	}

	ObfReferenceObject(CurrentProcess);

	DIO_TRACE("%s: Registered %d\n", __FUNCTION__, PsGetProcessId(CurrentProcess));

	return TRUE;
}

BOOLEAN
DioUnregister(
	IN BOOLEAN DisableIoAccess)
{
	PEPROCESS RegisteredProcess = NULL;
	PEPROCESS CurrentProcess = PsGetCurrentProcess();
	
	RegisteredProcess = (PEPROCESS)_InterlockedCompareExchange((LONG *)&DiopRegisteredProcess, 0, (LONG)CurrentProcess);
	if (!RegisteredProcess)
	{
		DIO_TRACE("%s: Failed to unregister because no process is registered\n", __FUNCTION__);
		return FALSE;
	}

	if (DisableIoAccess)
		Ke386IoSetAccessProcess(RegisteredProcess, 0);

	DIO_TRACE("%s: Unregistered %d\n", __FUNCTION__, PsGetProcessId(RegisteredProcess));

	ObfDereferenceObject(RegisteredProcess);
	return TRUE;
}

BOOLEAN
DioForceUnregister(
	IN HANDLE UnregisterProcessId)
{
	KIRQL Irql;
	BOOLEAN Unregistered;
	PEPROCESS RegisteredProcess;

//	DIO_TRACE("%s: Trying to unregister %d\n", __FUNCTION__, UnregisterProcessId);

	// Acquire the lock so following code is not executed simultaneously.
	KeAcquireSpinLock(&DiopForceUnregisterLock, &Irql);

	Unregistered = FALSE;
	RegisteredProcess = DiopRegisteredProcess;

	if (RegisteredProcess != NULL)
	{
		if (UnregisterProcessId == PsGetProcessId(RegisteredProcess))
		{
			// WARNING: Calling Ke386IoSetAccessProcess is probited here!
//			if (DisableIoAccess)
//				Ke386IoSetAccessProcess(RegisteredProcess, 0);

			// OK, dereference it
			ObfDereferenceObject(RegisteredProcess);

			// FIXME : Should we insert the barrier here?
			DiopRegisteredProcess = NULL;

			Unregistered = TRUE;
		}
	}

	KeReleaseSpinLock(&DiopForceUnregisterLock, Irql);

	if (Unregistered)
		DIO_TRACE("%s: Unregistered %d\n", __FUNCTION__, UnregisterProcessId);

	return Unregistered;
}


//
// Our callback function.
//

VOID
DiopCreateProcessNotifyRoutine(
	IN HANDLE ParentId, 
	IN HANDLE ProcessId, 
	IN BOOLEAN Create)
{
	UNREFERENCED_PARAMETER(ParentId);

	if (!Create)
	{
		DioForceUnregister(ProcessId);
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
	UNREFERENCED_PARAMETER(DeviceObject);
	UNREFERENCED_PARAMETER(Irp);

	return STATUS_NOT_SUPPORTED;
}


NTSTATUS
DioDispatchCreate(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

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
	UNREFERENCED_PARAMETER(DeviceObject);

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
	PEPROCESS CurrentProcess;

	UNREFERENCED_PARAMETER(DeviceObject);

//	DIO_IN_DEBUG_BREAKPOINT();

	CurrentProcess = PsGetCurrentProcess();
	Status = STATUS_SUCCESS;

	IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
	IoControlCode = IoStackLocation->Parameters.DeviceIoControl.IoControlCode;
	InputBufferLength = IoStackLocation->Parameters.DeviceIoControl.InputBufferLength;

	DIO_TRACE("%s: IOCTL from process %d\n", __FUNCTION__, PsGetProcessId(CurrentProcess));

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
		if (!DiopValidatePacketBuffer(Packet, InputBufferLength, IoControlCode))
		{
			DIO_TRACE("%s: Buffer validation failed\n", __FUNCTION__);
			Status = STATUS_INVALID_PARAMETER;
			break;
		}


		//
		// 3. Dispatch IOCTL request.
		//

		switch (IoControlCode)
		{
		case DIO_IOCTL_SET_PORTACCESS:
			if (!DioRegisterSelf())
			{
				DIO_TRACE("%s: Process 0x%p is already registered\n", __FUNCTION__, DiopRegisteredProcess);
				Status = STATUS_ACCESS_DENIED;
				break;
			}

			// Create the I/O access map from packet.
			if (!DioSetIoAccessMapByPacket(&Packet->PortAccess, DiopIoAccessMap))
			{
				DIO_TRACE("%s: Failed to prepare access map\n", __FUNCTION__);
				Status = STATUS_INVALID_PARAMETER;
				break;
			}

			DIO_TRACE("%s: Setting port permission for process 0x%p (%d)\n", 
				__FUNCTION__, CurrentProcess, PsGetProcessId(CurrentProcess));

			Ke386IoSetAccessProcess(CurrentProcess, 1);
			Ke386SetIoAccessMap(1, DiopIoAccessMap);
			break;

		case DIO_IOCTL_RESET_PORTACCESS:
			if (!DioSetIoAccessMapByPacket(NULL, DiopIoAccessMap))
			{
				DIO_TRACE("%s: Failed to prepare access map\n", __FUNCTION__);
				Status = STATUS_INVALID_PARAMETER;
				break;
			}

			DIO_TRACE("%s: Resetting port permission to initial state for process 0x%p (%d)\n", 
				__FUNCTION__, CurrentProcess, PsGetProcessId(CurrentProcess));

			Ke386IoSetAccessProcess(CurrentProcess, 0);
			break;

		default:
			Status = STATUS_NOT_SUPPORTED;
		}
	} while(FALSE);


	//
	// 4. Complete the request.
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
#ifdef DIO_SUPPORT_UNLOAD
	DIO_TRACE("%s: Shutdowning...\n", __FUNCTION__);

	IoDeleteSymbolicLink(&DiopDosDeviceName);
	IoDeleteDevice(DiopDeviceObject);

	PsSetCreateProcessNotifyRoutine(DiopCreateProcessNotifyRoutine, TRUE);

	DioUnregister(TRUE);

	// FIXME : Is it safe to free the map?
	DIO_FREE(DiopIoAccessMap);
#else
	__debugbreak();
#endif
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

	UNREFERENCED_PARAMETER(RegistryPath);


	DIO_TRACE(" ********** DIO board I/O helper ********** \n");
	DIO_TRACE("Last built " __DATE__ " " __TIME__ "\n");
	DIO_TRACE("%s: Initializing...\n", __FUNCTION__);

	RtlInitUnicodeString(&DiopDeviceName, L"\\Device\\Dioport");
	RtlInitUnicodeString(&DiopDosDeviceName, L"\\DosDevices\\Dioport");

	AccessMap = (IO_ACCESS_MAP *)DIO_ALLOC(sizeof(*AccessMap));
	if (!AccessMap)
	{
		DIO_TRACE("ExAllocatePoolWithTag failed\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	Status = IoCreateDevice(DriverObject, 0, &DiopDeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
	if (!NT_SUCCESS(Status))
	{
		DIO_TRACE("IoCreateDevice failed\n");
		DIO_FREE(AccessMap);

		return Status;
	}

	Status = IoCreateSymbolicLink(&DiopDosDeviceName, &DiopDeviceName);
	if (!NT_SUCCESS(Status))
	{
		IoDeleteDevice(DeviceObject);
		DIO_FREE(AccessMap);
		
		DIO_TRACE("IoCreateSymbolicLink failed\n");
		return Status;
	}

	for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
		DriverObject->MajorFunction[i] = DioDispatchNotSupported;

	DriverObject->MajorFunction[IRP_MJ_CREATE] = DioDispatchCreate;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = DioDispatchClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DioDispatchIoControl;
#ifdef DIO_SUPPORT_UNLOAD
	DriverObject->DriverUnload = DioDriverUnload;
#else
	DriverObject->DriverUnload = NULL;
#endif

	//
	// Register our notify routine.
	//

	Status = PsSetCreateProcessNotifyRoutine(DiopCreateProcessNotifyRoutine, FALSE);
	if (!NT_SUCCESS(Status))
		DIO_TRACE("%s: WARNING - Failed to register notify routine (0x%08lx)\n", __FUNCTION__, Status);

	DIO_TRACE("%s: Initialization done.\n", __FUNCTION__);


	//
	// Initialize the globals.
	//

	KeInitializeSpinLock(&DiopForceUnregisterLock);

	DiopDriverObject = DriverObject;
	DiopDeviceObject = DeviceObject;
	DiopIoAccessMap = AccessMap;

	return Status;
}

