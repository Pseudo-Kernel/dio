
#include <ntddk.h>
#include <ntstrsafe.h>
#include "../Include/dioctl.h"

//
// Definitions for options to compile.
//

#define	__DIO_DENY_CONVENTIONAL_PORT_ACCESS		// To deny the conventional port access
#define	__DIO_SUPPORT_UNLOAD					// To support driver unload
#define __DIO_IGNORE_BREAKPOINT					// This option overrides DIO_IN_DEBUG_BREAKPOINT() to do nothing.
//#define __DIO_IOCTL_TEST_MODE					// Define if you want to run with IOCTL test mode only. Real port I/O is not performed.

// Port address range for maximum 640 (=128x5) channels. (640 channels for input, 640 channels for output)
#define DIO_PORT_ADDRESS_START					0x7000
#define DIO_PORT_ADDRESS_END					0x704f

C_ASSERT(
	DIO_PORT_ADDRESS_START < DIO_PORT_ADDRESS_END && 
	DIO_PORT_ADDRESS_START < 0x10000 && 
	DIO_PORT_ADDRESS_END < 0x10000);


//
// Global helper macros.
//

#define DIO_ALLOC(_size)						ExAllocatePoolWithTag(NonPagedPool, (_size), 'OIDp')
#define DIO_FREE(_addr)							ExFreePoolWithTag((_addr), 'OIDp')

#define	DTRACE(_fmt, ...)						DioDbgTrace(TRUE, (_fmt), __VA_ARGS__)
#define	DFTRACE(_fmt, ...)						DioDbgTrace(TRUE, ("%s: " _fmt), __FUNCTION__, __VA_ARGS__)
#define	DTRACE_DBG(_fmt, ...)					DioDbgTrace(FALSE, (_fmt), __VA_ARGS__)
#define	DFTRACE_DBG(_fmt, ...)					DioDbgTrace(FALSE, ("%s: " _fmt), __FUNCTION__, __VA_ARGS__)


#define	DASSERT(_expr) {	\
	if (!(_expr)) {			\
		__debugbreak();		\
	}						\
}

#ifdef __DIO_IGNORE_BREAKPOINT
#define	DIO_IN_DEBUG_BREAKPOINT()
#else
#define	DIO_IN_DEBUG_BREAKPOINT() {		\
	if (!(*KdDebuggerNotPresent)) {		\
		__debugbreak();					\
	}									\
}
#endif


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

DIO_CONFIGURATION_BLOCK DiopConfigurationBlock;

#ifdef __DIO_DENY_CONVENTIONAL_PORT_ACCESS
/**
 *	@brief	Predefined range of well-known port address.
 *
 *	We only allow maximum 640 channels here.
 */
DIO_PORT_RANGE DiopConventionalPortAddressRangeListForDeny[] = {
	{ 0x0000, DIO_PORT_ADDRESS_START - 1 }, 
	/* Do not deny DIO board address 0x7000 ~ 0x7000 + 0x10 * BoardCount,
	   where the BoardCount = 5 (total 640 channels) */
	{ DIO_PORT_ADDRESS_END + 1, 0xffff }, 
};
#endif



VOID
DioDbgTrace(
	IN BOOLEAN ForceOutput, 
	IN PSZ Format, 
	...)
/**
 *	@brief	Prints the debug string.
 *	
 *	Note that this function only prints if configuration bit DIO_CFGB_SHOW_DEBUG_OUTPUT is set.
 *
 *	@param	[in] ForceOutput			Forces debug string output regardless of configuration bit.
 *	@param	[in] Format					Printf-like format ASCII string.
 *	@param	[in] ...					Parameter list.
 *	@return								None.
 *	
 */
{
	va_list Args;

	if (!ForceOutput && 
		!(DiopConfigurationBlock.ConfigurationBits & DIO_CFGB_SHOW_DEBUG_OUTPUT))
		return;

	va_start(Args, Format);
	vDbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, Format, Args);
	va_end(Args);
}


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
		Text[(i + 1) * 3] = 0;

		if (RemainingTextLength <= 3)
			break;

		RemainingTextLength -= 3;
	}

	DTRACE("%s\n => %s\n", Message, Text);
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

#ifdef __DIO_DENY_CONVENTIONAL_PORT_ACCESS
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
	case DIO_IOCTL_READ_CONFIGURATION:
		//
		// Input: Packet->ReadWriteConfiguration.Version
		// Output: Packet->ReadWriteConfiguration
		//

		if (InputBufferLength < sizeof(Packet->ReadWriteConfiguration.Version))
			return FALSE;

		if (Packet->ReadWriteConfiguration.Version != DIO_DRIVER_CONFIGURATION_VERSION1)
			return FALSE;

		if (OutputBufferLength < sizeof(Packet->ReadWriteConfiguration))
			return FALSE;
		break;

	case DIO_IOCTL_WRITE_CONFIGURATION:
		//
		// Input: Packet->ReadWriteConfiguration
		// Output: Packet->ReadWriteConfiguration
		//

		if (InputBufferLength < sizeof(Packet->ReadWriteConfiguration.Version))
			return FALSE;

		if (Packet->ReadWriteConfiguration.Version != DIO_DRIVER_CONFIGURATION_VERSION1)
			return FALSE;

		if (InputBufferLength < sizeof(Packet->ReadWriteConfiguration))
			return FALSE;

		if (OutputBufferLength < sizeof(Packet->ReadWriteConfiguration))
			return FALSE;
		break;

	case DIO_IOCTL_READ_PORT:
	case DIO_IOCTL_WRITE_PORT:
		{
			ULONG RangeCount = 0;
			ULONG DataLength = 0;
			ULONG RequiredInputLength = 0;
			ULONG RequiredOutputLength = 0;

			DFTRACE_DBG("InputBufferLength %d, OutputBufferLength %d\n", InputBufferLength, OutputBufferLength);

			RequiredInputLength = sizeof(Packet->PortIo);
			if (InputBufferLength < RequiredInputLength)
			{
				DFTRACE_DBG("RequiredInputLength %d\n", RequiredInputLength);
				return FALSE;
			}

			RangeCount = Packet->PortIo.RangeCount;
			if (RangeCount > DIO_MAXIMUM_PORT_IO_REQUEST)
			{
				DFTRACE_DBG("RangeCount %d\n", RangeCount);
				return FALSE;
			}

			RequiredInputLength += RangeCount * sizeof(DIO_PORT_RANGE);
			if (InputBufferLength < RequiredInputLength)
			{
				DFTRACE_DBG("RequiredInputLength %d\n", RequiredInputLength);
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
				DFTRACE_DBG("DataLength (%d) must be less than 64K\n", DataLength);
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
				DFTRACE_DBG("RequiredInputLength %d, RequiredOutputLength %d\n", RequiredInputLength, RequiredOutputLength);
				return FALSE;
			}
		}
		break;

	default:
		DFTRACE_DBG("Unknown IOCTL\n");
		return FALSE;
	}

	return TRUE;
}

BOOLEAN
DiopInternalPortIo(
	IN USHORT BaseAddress, 
	IN OUT PUCHAR Buffer, 
	IN ULONG Length, 
	IN BOOLEAN Write)
/**
 *	@brief	Do the direct port I/O for given address.
 *	
 *	This function is reserved for internal use.
 *	
 *	@param	[in] BaseAddress			Base address to read/write.
 *	@param	[in, out] Buffer			Address of buffer.
 *	@param	[in] Length					Length in bytes to read/write.
 *	@param	[in] Write					Port input if FALSE, port output otherwise.
 *	@return								Non-zero if successful.
 *	
 */
{
	ULONG i;
	ULONG Address;

	Address = BaseAddress;

	if (Address >= 0x10000 || Length >= 0x10000 || Address + Length > 0x10000)
		return FALSE;

	if (Write)
	{
		for (i = 0; i < Length; i++)
			__outbyte((USHORT)(Address + i), Buffer[i]);
	}
	else
	{
		for (i = 0; i < Length; i++)
			Buffer[i] = __inbyte((USHORT)(Address + i));
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
	ULONG RequiredLength = 0;
	ULONG IoLength = 0;
	BOOLEAN Result = TRUE;

	DFTRACE_DBG("Range count = %d\n", Count);
	
	if (Count > DIO_MAXIMUM_PORT_IO_REQUEST)
	{
		DFTRACE_DBG("Maximum entry count exceeded\n");
		return FALSE;
	}

	for (i = 0; i < Count; i++)
	{
		DFTRACE_DBG("[%d] 0x%x - 0x%x\n", i, Ranges[i].StartAddress, Ranges[i].EndAddress);

		if (!DioTestPortRange(Ranges[i].StartAddress, Ranges[i].EndAddress))
		{
			DFTRACE_DBG("[%d] Inaccessible address range\n", i);
			return FALSE;
		}

		RequiredLength += Ranges[i].EndAddress - Ranges[i].StartAddress + 1;
	}

	if (!Buffer)
	{
		DFTRACE_DBG("Null buffer, transfer ignored\n");
		return TRUE;
	}

	if (BufferLength < RequiredLength)
	{
		DFTRACE_DBG("Insufficient buffer length (BufferLength %d, RequiredLength %d)\n", 
			BufferLength, RequiredLength);
		return FALSE;
	}

	if (Write)
		DFTRACE_DBG("Writing to the port...\n");
	else
		DFTRACE_DBG("Reading from the port...\n");


	// Only one instance at most can access the port simultaneously.
	KeAcquireSpinLock(&DiopPortReadWriteLock, &Irql);

	DIO_IN_DEBUG_BREAKPOINT();

	for (i = 0; i < Count; i++)
	{
		ULONG Length = Ranges[i].EndAddress - Ranges[i].StartAddress + 1;

#ifdef __DIO_IOCTL_TEST_MODE
		if (Write)
			DioDbgDumpBytes("Writing bytes", Length, 16, Buffer + IoLength);
		else
		{
			ULONG j;

			for (j = 0; j < Length; j++)
				Buffer[IoLength + j] = ((j & 0x0f) << 4) | (j & 0x0f);

			DioDbgDumpBytes("Reading bytes", Length, 16, Buffer + IoLength);
		}
#else
		if (!DiopInternalPortIo(Ranges[i].StartAddress, Buffer + IoLength, Length, Write))
		{
			DFTRACE_DBG(" *** WARNING: Unexpected I/O failure\n");
			Result = FALSE;
			break;
		}
#endif

		IoLength += Length;
	}

	KeReleaseSpinLock(&DiopPortReadWriteLock, Irql);

	DFTRACE_DBG("Total %d bytes transferred\n", IoLength);

	if (TransferredLength)
		*TransferredLength = IoLength;

	return Result;
}

BOOLEAN
DioIsRegistered(
	VOID)
/**
 *	@brief	Returns the result whether the caller process is registered or not.
 *	
 *	@return								Non-zero if caller is already registered.
 *	
 */
{
	return (BOOLEAN)(DiopRegisteredProcess == PsGetCurrentProcess());
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

	RegisteredProcess = (PEPROCESS)InterlockedCompareExchangePointer((PVOID *)&DiopRegisteredProcess, (PVOID)CurrentProcess, 0);

	if (RegisteredProcess != NULL && RegisteredProcess != CurrentProcess)
	{
		DFTRACE("Failed to register %d because it is already registered\n", 
			PsGetProcessId(CurrentProcess));
		return FALSE;
	}

	ObfReferenceObject(CurrentProcess);

	DFTRACE("Registered %d\n", PsGetProcessId(CurrentProcess));

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
	
	RegisteredProcess = (PEPROCESS)InterlockedCompareExchangePointer((PVOID *)&DiopRegisteredProcess, 0, (PVOID)CurrentProcess);
	if (!RegisteredProcess)
	{
		DFTRACE("Failed to unregister because no process is registered\n");
		return FALSE;
	}

	DFTRACE("Unregistered %d\n", PsGetProcessId(RegisteredProcess));

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

//	DFTRACE("Trying to unregister %d\n", UnregisterProcessId);

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
			InterlockedExchangePointer((volatile PVOID *)&DiopRegisteredProcess, 0);

			Unregistered = TRUE;
		}
	}

	KeReleaseSpinLock(&DiopForceUnregisterLock, Irql);

	if (Unregistered)
		DFTRACE("Unregistered %d\n", UnregisterProcessId);

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
		DFTRACE("Process 0x%p is already registered\n", DiopRegisteredProcess);
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

	DioUnregister();

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	IofCompleteRequest(Irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

NTSTATUS
DioDispatchCleanup(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp)
/**
 *	@brief	Dispatch routine for IRP_MJ_CLEANUP.
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
	ULONG DataOffset;
	ULONG OutputActualLength;
	DIO_PACKET *Packet;
	NTSTATUS Status;
	PEPROCESS CurrentProcess;

	UNREFERENCED_PARAMETER(DeviceObject);

	DIO_IN_DEBUG_BREAKPOINT();

	CurrentProcess = PsGetCurrentProcess();
	Status = STATUS_SUCCESS;

	IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
	IoControlCode = IoStackLocation->Parameters.DeviceIoControl.IoControlCode;
	InputBufferLength = IoStackLocation->Parameters.DeviceIoControl.InputBufferLength;
	OutputBufferLength = IoStackLocation->Parameters.DeviceIoControl.OutputBufferLength;
	OutputActualLength = 0;

	do
	{
		//
		// 1. Make sure that caller is already registered and using buffered IOCTL.
		//

		if (!DioIsRegistered())
		{
			DFTRACE("Process %d is not allowed\n", PsGetProcessId(CurrentProcess));
			Status = STATUS_ACCESS_DENIED;
			break;
		}

		DFTRACE_DBG("IOCTL from process %d\n", PsGetProcessId(CurrentProcess));

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
			DFTRACE_DBG("Buffer validation failed\n");
			Status = STATUS_INVALID_PARAMETER;
			break;
		}


		//
		// 3. Dispatch IOCTL request.
		//

		switch (IoControlCode)
		{
		case DIO_IOCTL_READ_CONFIGURATION:
			// Read driver configuration
			DFTRACE_DBG("Read driver configuration\n");
			if (Packet->ReadWriteConfiguration.Version == DIO_DRIVER_CONFIGURATION_VERSION1)
			{
				Packet->ReadWriteConfiguration.ConfigurationBlock = DiopConfigurationBlock;
				OutputActualLength = sizeof(Packet->ReadWriteConfiguration);
			}
			break;

		case DIO_IOCTL_WRITE_CONFIGURATION:
			// Write driver configuration
			DFTRACE_DBG("Write driver configuration\n");
			if (Packet->ReadWriteConfiguration.Version == DIO_DRIVER_CONFIGURATION_VERSION1)
			{
				DiopConfigurationBlock = Packet->ReadWriteConfiguration.ConfigurationBlock;
				OutputActualLength = sizeof(Packet->ReadWriteConfiguration);
			}
			break;

		case DIO_IOCTL_READ_PORT:
			// Input from the port.
			DFTRACE_DBG("Port read request from process 0x%p (%d)\n", 
				CurrentProcess, PsGetProcessId(CurrentProcess));

			DataOffset = PACKET_PORT_IO_GET_LENGTH(Packet->PortIo.RangeCount);

			if (!DioPortIo(Packet->PortIo.AddressRange, 
							Packet->PortIo.RangeCount, 
							PACKET_PORT_IO_GET_DATA_ADDRESS(&Packet->PortIo), 
							OutputBufferLength - DataOffset, 
							&OutputActualLength, 
							FALSE))
			{
				DFTRACE_DBG("I/O failed\n");
				Status = STATUS_UNSUCCESSFUL;
				break;
			}

			OutputActualLength += PACKET_PORT_IO_GET_LENGTH(Packet->PortIo.RangeCount);
			break;

		case DIO_IOCTL_WRITE_PORT:
			// Output to the port.
			DFTRACE_DBG("Port write request from process 0x%p (%d)\n", 
				CurrentProcess, PsGetProcessId(CurrentProcess));

			DataOffset = PACKET_PORT_IO_GET_LENGTH(Packet->PortIo.RangeCount);

			if (!DioPortIo(Packet->PortIo.AddressRange, 
							Packet->PortIo.RangeCount, 
							PACKET_PORT_IO_GET_DATA_ADDRESS(&Packet->PortIo), 
							InputBufferLength - DataOffset, 
							NULL, 
							TRUE))
			{
				DFTRACE_DBG("I/O failed\n");
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
#ifdef __DIO_SUPPORT_UNLOAD
	DFTRACE("Shutdowning...\n");

	IoDeleteSymbolicLink(&DiopDosDeviceName);
	IoDeleteDevice(DiopDeviceObject);

	PsSetCreateProcessNotifyRoutine(DiopCreateProcessNotifyRoutine, TRUE);

	DioUnregister();

	DFTRACE("Byebye!\n\n");

#else
	DFTRACE("*** STOP! ***\n\n");
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

	DTRACE(" ############ DIO board I/O helper driver ############ \n");
	DTRACE(" Last built " __DATE__ " " __TIME__ "\n\n");
	DTRACE(" ##################################################### \n\n");

	DFTRACE("Initializing...\n");

	RtlInitUnicodeString(&DiopDeviceName, L"\\Device\\Dioport");
	RtlInitUnicodeString(&DiopDosDeviceName, L"\\DosDevices\\Dioport");

	Status = IoCreateDevice(DriverObject, 0, &DiopDeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
	if (!NT_SUCCESS(Status))
	{
		DFTRACE("IoCreateDevice failed\n");
		return Status;
	}

	Status = IoCreateSymbolicLink(&DiopDosDeviceName, &DiopDeviceName);
	if (!NT_SUCCESS(Status))
	{
		IoDeleteDevice(DeviceObject);
		DFTRACE("IoCreateSymbolicLink failed\n");
		return Status;
	}

	for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
		DriverObject->MajorFunction[i] = DioDispatchNotSupported;

	DriverObject->MajorFunction[IRP_MJ_CREATE] = DioDispatchCreate;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = DioDispatchClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DioDispatchIoControl;
	DriverObject->MajorFunction[IRP_MJ_CLEANUP] = DioDispatchCleanup;
#ifdef __DIO_SUPPORT_UNLOAD
	DriverObject->DriverUnload = DioDriverUnload;
#else
	DriverObject->DriverUnload = NULL;
#endif

	//
	// Register our notify routine.
	//

	Status = PsSetCreateProcessNotifyRoutine(DiopCreateProcessNotifyRoutine, FALSE);
	if (!NT_SUCCESS(Status))
		DFTRACE("WARNING - Failed to register notify routine (0x%08lx)\n", Status);

	DFTRACE("Initialization done.\n");


	//
	// Initialize the globals.
	//

	KeInitializeSpinLock(&DiopPortReadWriteLock);
	KeInitializeSpinLock(&DiopForceUnregisterLock);

	DiopDriverObject = DriverObject;
	DiopDeviceObject = DeviceObject;

	DiopConfigurationBlock.ConfigurationBits = 0;

	return Status;
}

