
#include <ntddk.h>
#include <ntstrsafe.h>
#include "../Include/dioctl.h"

//#define	DIO_DENY_CONVENTIONAL_PORT_ACCESS	// To deny the conventional port access
#define	DIO_SUPPORT_UNLOAD						// To support driver unload
#define DIO_TEST_MODE							// Define if you want to run with IOCTL test mode only. Real port I/O is not performed.


#define DIO_ALLOC(_size)						ExAllocatePoolWithTag(NonPagedPool, (_size), 'OIDp')
#define DIO_FREE(_addr)							ExFreePoolWithTag((_addr), 'OIDp')

#define	DIO_TRACE(_fmt, ...)					DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, (_fmt), __VA_ARGS__)
#define	DIO_FUNC_TRACE(_fmt, ...)				DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, ("%s: " _fmt), __FUNCTION__, __VA_ARGS__)

#define	DIO_ASSERT(_expr)	\
	if (!(_expr)) {			\
		__debugbreak();		\
	}

#define	DIO_IN_DEBUG_BREAKPOINT() {		\
	if (!(*KdDebuggerNotPresent)) {		\
		__debugbreak();					\
	}									\
}



//
// Global variables.
//

PDRIVER_OBJECT DiopDriverObject = NULL;
PDEVICE_OBJECT DiopDeviceObject = NULL;

UNICODE_STRING DiopDeviceName;
UNICODE_STRING DiopDosDeviceName;

KSPIN_LOCK DiopPortReadWriteLock;

KSPIN_LOCK DiopForceUnregisterLock;
volatile PEPROCESS DiopRegisteredProcess = NULL;

#ifdef DIO_DENY_CONVENTIONAL_PORT_ACCESS
/**
 *	@brief	Predefined range of well-known port address.
 *
 *	Since the DIO board address starts at 0x7000, we only allow maximum 1024 channels here.
 */
DIO_PORT_RANGE DiopConventionalPortAddressRangeListForDeny[] = {
	{ 0x0000, 0x6fff }, /* 0x0000 ~ 0x6fff */
	/* Do not deny DIO board address 0x7000 ~ 0x7000 + 0x10 * BoardCount,
	   where the BoardCount = 8 (total 1024 channels) */
	{ 0x7080, 0xffff }, /* 0x7080 ~ 0xffff */
};
#endif

//
// Our I/O control packet helper function.
//

VOID
DioDbgDumpBytes(
	IN PSZ Message, 
	IN ULONG DumpLength, 
	IN ULONG DumpLengthMaximum, 
	IN PUCHAR Buffer)
/**
 *	@brief	Prints the memory bytes.
 *	
 *	Note that this function can print 128 bytes at most.
 *
 *	@param	[in] Message				ASCII string to be printed.
 *	@param	[in] DumpLength				Length of the bytes to be printed.
 *	@param	[in] DumpLengthMaximum		Maximum length of the bytes to be printed.
 *	@param	[in] Buffer					Buffer address which contains the data to be printed.
 *	@return								None.
 *	
 */
{
	CHAR Text[128 * (2 + 1) + 1] = { 0 };
	ULONG RemainingTextLength = ARRAYSIZE(Text);
	ULONG BufferLength = min(DumpLength, DumpLengthMaximum);
	ULONG i;

	for (i = 0; i < BufferLength; i++)
	{
		RtlStringCbPrintfA(Text + i * 3, RemainingTextLength, "%02X ", Buffer[i]);
		Text[i * 3] = 0;

		if (RemainingTextLength <= 3)
			break;

		RemainingTextLength -= 3;
	}

	DIO_TRACE("%s\n => %s\n", Message, Text);
}

BOOLEAN
DioTestPortRange(
	IN USHORT StartAddress, 
	IN USHORT EndAddress)
/**
 *	@brief	Tests the address range is accessible or not.
 *	
 *	@param	[in] StartAddress			Starting port address.
 *	@param	[in] EndAddress				Ending port address.
 *	@return								Returns FALSE if non-accessible, TRUE otherwise.
 *	
 */
{
	ULONG i;

	if (StartAddress > EndAddress)
		return FALSE;

#ifdef DIO_DENY_CONVENTIONAL_PORT_ACCESS
	for (i = 0; i < ARRAYSIZE(DiopConventionalPortAddressRangeListForDeny); i++)
	{
		DIO_PORT_RANGE *AddressRange = DiopConventionalPortAddressRangeListForDeny + i;

		if ((StartAddress <= AddressRange->StartAddress && AddressRange->StartAddress <= EndAddress) || 
			(StartAddress <= AddressRange->EndAddress && AddressRange->EndAddress <= EndAddress))
			return FALSE;
	}
#endif

	return TRUE;
}

BOOLEAN
DiopValidatePacketBuffer(
	IN DIO_PACKET *Packet, 
	IN ULONG InputBufferLength, 
	IN ULONG OutputBufferLength, 
	IN ULONG IoControlCode)
/**
 *	@brief	Validates the packet buffer.
 *	
 *	This function is reserved for internal use.
 *
 *	@param	[in] Packet					Address of packet buffer.
 *	@param	[in] InputBufferLength		Length of input buffer which points the packet structure.
 *	@param	[in] OutputBufferLength		Length of output buffer.
 *	@param	[in] IoControlCode			Related IOCTL code of packet buffer.
 *	@return								Non-zero if successful.
 *	
 */
{
	ULONG i;

	switch (IoControlCode)
	{
	case DIO_IOCTL_READ_PORT:
	case DIO_IOCTL_WRITE_PORT:
		{
			ULONG RangeCount = 0;
			ULONG DataLength = 0;
			ULONG RequiredInputLength = 0;
			ULONG RequiredOutputLength = 0;

			DIO_FUNC_TRACE("InputBufferLength %d, OutputBufferLength %d\n", InputBufferLength, OutputBufferLength);

			RequiredInputLength = sizeof(Packet->PortIo);
			if (InputBufferLength < RequiredInputLength)
			{
				DIO_FUNC_TRACE("RequiredInputLength %d\n", RequiredInputLength);
				return FALSE;
			}

			RangeCount = Packet->PortIo.RangeCount;
			if (RangeCount > DIO_MAXIMUM_PORT_IO_REQUEST)
			{
				DIO_FUNC_TRACE("RangeCount %d\n", RangeCount);
				return FALSE;
			}

			RequiredInputLength += RangeCount * sizeof(DIO_PORT_RANGE);
			if (InputBufferLength < RequiredInputLength)
			{
				DIO_FUNC_TRACE("RequiredInputLength %d\n", RequiredInputLength);
				return FALSE;
			}

			//
			// Calculate the data length to transfer.
			//

			for (i = 0; i < Packet->PortIo.RangeCount; i++)
			{
				DIO_PORT_RANGE *AddressRange = Packet->PortIo.AddressRange + i;

				if (DioTestPortRange(AddressRange->StartAddress, AddressRange->EndAddress))
					DataLength += AddressRange->EndAddress - AddressRange->StartAddress + 1;
			}

			// Data length cannot be equal or bigger than 64K.
			if (DataLength > 0x10000)
			{
				DIO_FUNC_TRACE("DataLength (%d) must be less than 64K\n", DataLength);
				return FALSE;
			}


			// Port read  : InputBuffer  [RangeCount] [Ranges]
			//              OutputBuffer [RangeCount] [Ranges] [Data]
			// Port write : InputBuffer  [RangeCount] [Ranges] [Data]
			//              OutputBuffer [RangeCount] [Ranges]

			RequiredOutputLength = RequiredInputLength;

			if (IoControlCode == DIO_IOCTL_READ_PORT)
				RequiredOutputLength += DataLength;
			else
				RequiredInputLength += DataLength;

			if (InputBufferLength < RequiredInputLength || 
				OutputBufferLength < RequiredOutputLength)
			{
				DIO_FUNC_TRACE("RequiredInputLength %d, RequiredOutputLength %d\n", RequiredInputLength, RequiredOutputLength);
				return FALSE;
			}
		}
		break;

	default:
		DIO_FUNC_TRACE("Unknown IOCTL\n");
		return FALSE;
	}

	return TRUE;
}

BOOLEAN
DioPortIo(
	IN DIO_PORT_RANGE *Ranges, 
	IN ULONG Count, 
	OPTIONAL IN OUT PUCHAR Buffer, 
	IN ULONG BufferLength, 
	OUT ULONG *TransferredLength, 
	IN BOOLEAN Write)
/**
 *	@brief	Do the direct port I/O for given address range.
 *	
 *	@param	[in] Ranges					Contains one or multiple port range(s).
 *	@param	[in] Count					Number of port range.
 *	@param	[in, out, opt] Buffer		Address of I/O buffer. This parameter can be NULL.\n
 *										case 1) Buffer == NULL\n
 *										- Returns the test result whether the access is allowed or not.\n
 *										case 2) Buffer != NULL && Write == FALSE\n
 *										- Results will be copied to the Buffer.\n
 *										case 3) Buffer != NULL && Write != FALSE\n
 *										- Contents of Buffer will be sent to given address range.\n
 *	@param	[in] BufferLength			Caller-supplied buffer length in bytes.
 *	@param	[out] TransferredLength		Address of variable that receives the transferred length in bytes.
 *	@param	[in] Write					Port input if FALSE, port output otherwise.
 *	@return								Non-zero if successful.
 *	
 */
{
	ULONG i;
	KIRQL Irql;
	ULONG TotalLength = 0;

	DIO_FUNC_TRACE("Range count = %d\n", Count);
	
	if (Count > DIO_MAXIMUM_PORT_IO_REQUEST)
	{
		DIO_FUNC_TRACE("Maximum entry count exceeded\n");
		return FALSE;
	}

	for (i = 0; i < Count; i++)
	{
		DIO_FUNC_TRACE("[%d] 0x%x - 0x%x\n", i, Ranges[i].StartAddress, Ranges[i].EndAddress);

		if (!DioTestPortRange(Ranges[i].StartAddress, Ranges[i].EndAddress))
		{
			DIO_FUNC_TRACE("[%d] Inaccessible address range\n", i);
			return FALSE;
		}

		TotalLength += Ranges[i].EndAddress - Ranges[i].StartAddress + 1;
	}

	if (!Buffer)
	{
		DIO_FUNC_TRACE("Null buffer, transfer ignored\n");
		return TRUE;
	}

	if (BufferLength < TotalLength)
	{
		DIO_FUNC_TRACE("Insufficient buffer length (BufferLength %d, RequiredLength %d)\n", 
			BufferLength, TotalLength);
		return FALSE;
	}

	if (Write)
		DIO_FUNC_TRACE("Writing to the port...\n");
	else
		DIO_FUNC_TRACE("Reading from the port...\n");

	// Only one instance at most can access the port simultaneously.
	KeAcquireSpinLock(&DiopPortReadWriteLock, &Irql);

	for (i = 0; i < Count; i++)
	{
		ULONG Length = Ranges[i].EndAddress - Ranges[i].StartAddress + 1;

#ifdef DIO_TEST_MODE
		if (Write)
			DioDbgDumpBytes("Writing bytes", Length, 16, Buffer + TotalLength);
		else
			DioDbgDumpBytes("Reading bytes", Length, 16, Buffer + TotalLength);
#else
		__writeeflags(__readeflags() & ~(1 << 10));

		if (Write)
			__outbytestring(Ranges[i].StartAddress, Buffer + TotalLength, Length);
		else
			__inbytestring(Ranges[i].StartAddress, Buffer + TotalLength, Length);

		TotalLength += Length;
#endif
	}

	KeReleaseSpinLock(&DiopPortReadWriteLock, Irql);

	DIO_FUNC_TRACE("Total %d bytes transferred\n", TotalLength);

	if (TransferredLength)
		*TransferredLength = TotalLength;

	return TRUE;
}


BOOLEAN
DioRegisterSelf(
	VOID)
/**
 *	@brief	Registers caller process to permit port I/O access.
 *	
 *	@return								Non-zero if successful.
 *	
 */
{
	PEPROCESS CurrentProcess = PsGetCurrentProcess();
	PEPROCESS RegisteredProcess = NULL;

	RegisteredProcess = (PEPROCESS)_InterlockedCompareExchangePointer((PVOID *)&DiopRegisteredProcess, (PVOID)CurrentProcess, 0);

	if (RegisteredProcess != NULL && RegisteredProcess != CurrentProcess)
	{
		DIO_FUNC_TRACE("Failed to register %d because it is already registered\n", 
			PsGetProcessId(CurrentProcess));
		return FALSE;
	}

	ObfReferenceObject(CurrentProcess);

	DIO_FUNC_TRACE("Registered %d\n", PsGetProcessId(CurrentProcess));

	return TRUE;
}

BOOLEAN
DioUnregister(
	VOID)
/**
 *	@brief	Unregisters caller process.
 *	
 *	@return								Non-zero if successful.
 *	
 */
{
	PEPROCESS RegisteredProcess = NULL;
	PEPROCESS CurrentProcess = PsGetCurrentProcess();
	
	RegisteredProcess = (PEPROCESS)_InterlockedCompareExchangePointer((PVOID *)&DiopRegisteredProcess, 0, (PVOID)CurrentProcess);
	if (!RegisteredProcess)
	{
		DIO_FUNC_TRACE("Failed to unregister because no process is registered\n");
		return FALSE;
	}

	DIO_FUNC_TRACE("Unregistered %d\n", PsGetProcessId(RegisteredProcess));

	ObfDereferenceObject(RegisteredProcess);
	return TRUE;
}

BOOLEAN
DioForceUnregister(
	IN HANDLE UnregisterProcessId)
/**
 *	@brief	Forcibly unregisters specified process.
 *	
 *	@param	[in] UnregisterProcessId	Process id to unregister.
 *	@return								Non-zero if successful.
 *	
 */
{
	KIRQL Irql;
	BOOLEAN Unregistered;
	PEPROCESS RegisteredProcess;

//	DIO_FUNC_TRACE("Trying to unregister %d\n", UnregisterProcessId);

	// Acquire the lock so following code is not executed simultaneously.
	KeAcquireSpinLock(&DiopForceUnregisterLock, &Irql);

	Unregistered = FALSE;
	RegisteredProcess = DiopRegisteredProcess;

	if (RegisteredProcess != NULL)
	{
		if (UnregisterProcessId == PsGetProcessId(RegisteredProcess))
		{
			// OK, dereference it
			ObfDereferenceObject(RegisteredProcess);

			// We don't need barrier, use xchg instead
			_InterlockedExchangePointer((volatile PVOID *)&DiopRegisteredProcess, 0);

			Unregistered = TRUE;
		}
	}

	KeReleaseSpinLock(&DiopForceUnregisterLock, Irql);

	if (Unregistered)
		DIO_FUNC_TRACE("Unregistered %d\n", UnregisterProcessId);

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
/**
 *	@brief	Our process notify routine.
 *	
 *	This function is reserved for internal use.
 *
 *	@param	[in] ParentId				ID of parent process.
 *	@param	[in] ProcessId				ID of target process.
 *	@param	[in] Create					Zero for process deletion. Other values for process creation.
 *	@return								None.
 *	
 */
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
/**
 *	@brief	Default dispatch routine for not supported functions.
 *	
 *	@param	[in] DeviceObject			Device object.
 *	@param	[in] Irp					Irp object.
 *	@return								STATUS_NOT_SUPPORTED always.
 *	
 */
{
	UNREFERENCED_PARAMETER(DeviceObject);
	UNREFERENCED_PARAMETER(Irp);

	return STATUS_NOT_SUPPORTED;
}


NTSTATUS
DioDispatchCreate(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp)
/**
 *	@brief	Dispatch routine for IRP_MJ_CREATE.
 *	
 *	@param	[in] DeviceObject			Device object.
 *	@param	[in] Irp					Irp object.
 *	@return								STATUS_SUCCESS always.
 *	
 */
{
	UNREFERENCED_PARAMETER(DeviceObject);

	if (!DioRegisterSelf())
	{
		DIO_FUNC_TRACE("Process 0x%p is already registered\n", DiopRegisteredProcess);
		return STATUS_ACCESS_DENIED;
	}

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	IofCompleteRequest(Irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

NTSTATUS
DioDispatchClose(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp)
/**
 *	@brief	Dispatch routine for IRP_MJ_CLOSE.
 *	
 *	@param	[in] DeviceObject			Device object.
 *	@param	[in] Irp					Irp object.
 *	@return								STATUS_SUCCESS always.
 *	
 */
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
/**
 *	@brief	Dispatch routine for IRP_MJ_DEVICE_CONTROL.
 *	
 *	@param	[in] DeviceObject			Device object.
 *	@param	[in] Irp					Irp object.
 *	@return								STATUS_SUCCESS always.
 *	
 */
{
	PIO_STACK_LOCATION IoStackLocation;
	ULONG IoControlCode;
	ULONG InputBufferLength;
	ULONG OutputBufferLength;
	ULONG OutputActualLength;
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
	OutputBufferLength = IoStackLocation->Parameters.DeviceIoControl.OutputBufferLength;
	OutputActualLength = 0;

	DIO_FUNC_TRACE("IOCTL from process %d\n", PsGetProcessId(CurrentProcess));

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
		if (!DiopValidatePacketBuffer(Packet, InputBufferLength, OutputBufferLength, IoControlCode))
		{
			DIO_FUNC_TRACE("Buffer validation failed\n");
			Status = STATUS_INVALID_PARAMETER;
			break;
		}


		//
		// 3. Dispatch IOCTL request.
		//

		switch (IoControlCode)
		{
		case DIO_IOCTL_READ_PORT:
			// Input from the port.
			DIO_FUNC_TRACE("Port read request from process 0x%p (%d)\n", 
				CurrentProcess, PsGetProcessId(CurrentProcess));

			if (!DioPortIo(Packet->PortIo.AddressRange, 
							Packet->PortIo.RangeCount, 
							PACKET_PORT_IO_GET_DATA_ADDRESS(&Packet->PortIo), 
							OutputBufferLength, 
							&OutputActualLength, 
							FALSE))
			{
				DIO_FUNC_TRACE("I/O failed\n");
				Status = STATUS_UNSUCCESSFUL;
				break;
			}

			OutputActualLength += PACKET_PORT_IO_GET_LENGTH(Packet->PortIo.RangeCount);
			break;

		case DIO_IOCTL_WRITE_PORT:
			// Output to the port.
			DIO_FUNC_TRACE("Port write request from process 0x%p (%d)\n", 
				CurrentProcess, PsGetProcessId(CurrentProcess));

			if (!DioPortIo(Packet->PortIo.AddressRange, 
							Packet->PortIo.RangeCount, 
							PACKET_PORT_IO_GET_DATA_ADDRESS(&Packet->PortIo), 
							InputBufferLength, 
							NULL, 
							TRUE))
			{
				DIO_FUNC_TRACE("I/O failed\n");
				Status = STATUS_UNSUCCESSFUL;
				break;
			}

			OutputActualLength += PACKET_PORT_IO_GET_LENGTH(Packet->PortIo.RangeCount);
			break;

		default:
			Status = STATUS_NOT_SUPPORTED;
		}
	} while(FALSE);


	//
	// 4. Complete the request.
	//

	Irp->IoStatus.Status = Status;
	Irp->IoStatus.Information = OutputActualLength;

	IofCompleteRequest(Irp, IO_NO_INCREMENT);


	return STATUS_SUCCESS;
}

VOID
DioDriverUnload(
	IN PDRIVER_OBJECT DriverObject)
/**
 *	@brief	Dispatch routine for DriverUnload.
 *	
 *	@param	[in] DriverObject			Driver object.
 *	@return								None.
 *	
 */
{
#ifdef DIO_SUPPORT_UNLOAD
	DIO_FUNC_TRACE("Shutdowning...\n");

	IoDeleteSymbolicLink(&DiopDosDeviceName);
	IoDeleteDevice(DiopDeviceObject);

	PsSetCreateProcessNotifyRoutine(DiopCreateProcessNotifyRoutine, TRUE);

	DioUnregister();

	DIO_FUNC_TRACE("Byebye!\n\n");

#else
	DIO_FUNC_TRACE("*** STOP! ***\n\n");
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
/**
 *	@brief	Driver start entry.
 *	
 *	@param	[in] DriverObject			Driver object.
 *	@param	[in] RegistryPath			Registry path for driver services.
 *	@return								STATUS_SUCCESS if successful.
 *	
 */
{
	NTSTATUS Status;
	PDEVICE_OBJECT DeviceObject;
	ULONG i;

	UNREFERENCED_PARAMETER(RegistryPath);

	DIO_TRACE(" ********** DIO board I/O helper driver ********** \n");
	DIO_TRACE("Last built " __DATE__ " " __TIME__ "\n\n");
	DIO_FUNC_TRACE("Initializing...\n");

	RtlInitUnicodeString(&DiopDeviceName, L"\\Device\\Dioport");
	RtlInitUnicodeString(&DiopDosDeviceName, L"\\DosDevices\\Dioport");

	Status = IoCreateDevice(DriverObject, 0, &DiopDeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
	if (!NT_SUCCESS(Status))
	{
		DIO_FUNC_TRACE("IoCreateDevice failed\n");
		return Status;
	}

	Status = IoCreateSymbolicLink(&DiopDosDeviceName, &DiopDeviceName);
	if (!NT_SUCCESS(Status))
	{
		IoDeleteDevice(DeviceObject);
		DIO_FUNC_TRACE("IoCreateSymbolicLink failed\n");
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
		DIO_FUNC_TRACE("WARNING - Failed to register notify routine (0x%08lx)\n", Status);

	DIO_FUNC_TRACE("Initialization done.\n");


	//
	// Initialize the globals.
	//

	KeInitializeSpinLock(&DiopPortReadWriteLock);
	KeInitializeSpinLock(&DiopForceUnregisterLock);

	DiopDriverObject = DriverObject;
	DiopDeviceObject = DeviceObject;

	return Status;
}

