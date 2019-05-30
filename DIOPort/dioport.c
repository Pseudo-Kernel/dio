
#define	INITGUID

#include <ntddk.h>
#include <ntstrsafe.h>
#include "../Include/dioctl.h"
#include "dioport.h"

//
// Global variables.
//

//	{75BEC7D6-7F4E-4DAE-9A2B-B4D09B839B18}
DEFINE_GUID(DiopGuidDeviceClass, 0x75BEC7D6, 0x7F4E, 0x4DAE, 0x9A, 0x2B, 0xB4, 0xD0, 0x9B, 0x83, 0x9B, 0x18);

HANDLE DiopRegKeyHandle = NULL;
PDRIVER_OBJECT DiopDriverObject = NULL;
KSPIN_LOCK DiopPortReadWriteLock;
KSPIN_LOCK DiopProcessLock;
volatile PEPROCESS DiopRegisteredProcess = NULL;

#if !defined __DIO_IGNORE_BREAKPOINT
BOOLEAN DiopBreakOnKdAttached = TRUE;
#endif

DIO_CONFIGURATION_BLOCK DiopConfigurationBlock;


//
// Utility functions.
//

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

PSZ
DioPnpRtlLookupMinorFunctionName(
	IN ULONG MinorFunction)
{
	#define	__CASE_SELECT_STRING(_s)		case (_s): return #_s

	switch (MinorFunction)
	{
	__CASE_SELECT_STRING(IRP_MN_START_DEVICE					 );
	__CASE_SELECT_STRING(IRP_MN_QUERY_REMOVE_DEVICE         	 );
	__CASE_SELECT_STRING(IRP_MN_REMOVE_DEVICE               	 );
	__CASE_SELECT_STRING(IRP_MN_CANCEL_REMOVE_DEVICE        	 );
	__CASE_SELECT_STRING(IRP_MN_STOP_DEVICE                 	 );
	__CASE_SELECT_STRING(IRP_MN_QUERY_STOP_DEVICE           	 );
	__CASE_SELECT_STRING(IRP_MN_CANCEL_STOP_DEVICE          	 );

	__CASE_SELECT_STRING(IRP_MN_QUERY_DEVICE_RELATIONS      	 );
	__CASE_SELECT_STRING(IRP_MN_QUERY_INTERFACE             	 );
	__CASE_SELECT_STRING(IRP_MN_QUERY_CAPABILITIES          	 );
	__CASE_SELECT_STRING(IRP_MN_QUERY_RESOURCES             	 );
	__CASE_SELECT_STRING(IRP_MN_QUERY_RESOURCE_REQUIREMENTS 	 );
	__CASE_SELECT_STRING(IRP_MN_QUERY_DEVICE_TEXT           	 );
	__CASE_SELECT_STRING(IRP_MN_FILTER_RESOURCE_REQUIREMENTS	 );

	__CASE_SELECT_STRING(IRP_MN_READ_CONFIG                 	 );
	__CASE_SELECT_STRING(IRP_MN_WRITE_CONFIG                	 );
	__CASE_SELECT_STRING(IRP_MN_EJECT                       	 );
	__CASE_SELECT_STRING(IRP_MN_SET_LOCK                    	 );
	__CASE_SELECT_STRING(IRP_MN_QUERY_ID                    	 );
	__CASE_SELECT_STRING(IRP_MN_QUERY_PNP_DEVICE_STATE      	 );
	__CASE_SELECT_STRING(IRP_MN_QUERY_BUS_INFORMATION       	 );
	__CASE_SELECT_STRING(IRP_MN_DEVICE_USAGE_NOTIFICATION   	 );
	__CASE_SELECT_STRING(IRP_MN_SURPRISE_REMOVAL            	 );

	default:
		return "UNKNOWN_PNP_MINOR";
	}

	#undef __CASE_SELECT_STRING
}


NTSTATUS
DioReadRegistryValue(
	IN HANDLE KeyHandle, 
	IN PWSTR ValueNameString, 
	IN ULONG ExpectedValueType, 
	IN OUT PVOID Value, 
	IN ULONG ValueLength, 
	OPTIONAL OUT ULONG *ResultLength)
{
	UNICODE_STRING ValueName;
	NTSTATUS Status = STATUS_UNSUCCESSFUL;
	KEY_VALUE_PARTIAL_INFORMATION *PartialInformation = NULL;
	ULONG RequiredLength = sizeof(*PartialInformation);
	ULONG PartialInformationLength = 0;
	ULONG RetryCount = 0;

	RtlInitUnicodeString(&ValueName, ValueNameString);

	do
	{
		if (PartialInformation)
			DIO_FREE(PartialInformation);

		PartialInformationLength = RequiredLength;
		PartialInformation = (KEY_VALUE_PARTIAL_INFORMATION *)DIO_ALLOC(PartialInformationLength);

		if (PartialInformation)
		{
			Status = ZwQueryValueKey(KeyHandle, &ValueName, KeyValuePartialInformation, 
				PartialInformation, PartialInformationLength, &RequiredLength);

			if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
				break;
		}

	} while (!NT_SUCCESS(Status) && RetryCount++ < 10);

	if (NT_SUCCESS(Status))
	{
		if (PartialInformation->Type != ExpectedValueType)
			Status = STATUS_INVALID_PARAMETER;
		else if (PartialInformation->DataLength > ValueLength)
			Status = STATUS_BUFFER_TOO_SMALL;
		else
		{
			// Copy to the caller-supplied buffer.
			RtlCopyMemory(Value, PartialInformation->Data, PartialInformation->DataLength);

			if (ResultLength)
				*ResultLength = PartialInformation->DataLength;
		}
	}

	// Finally free the buffer
	if (PartialInformation)
		DIO_FREE(PartialInformation);

	return Status;
}

NTSTATUS
DioOpenDriverParametersRegistry(
	IN PUNICODE_STRING DriverRegistryPath, 
	OUT HANDLE *RegistryKeyHandle)
{
	HANDLE KeyHandle;
	HANDLE SubkeyHandle;
	ULONG Disposition;
	OBJECT_ATTRIBUTES ObjectAttributes;
	UNICODE_STRING SubkeyName;
	NTSTATUS Status;

	// Must check IRQL = Passive level.
	DASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	DFTRACE("Opening registry key: %wZ\n", DriverRegistryPath);

	InitializeObjectAttributes(&ObjectAttributes, DriverRegistryPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
	Status = ZwOpenKey(&KeyHandle, KEY_ALL_ACCESS, &ObjectAttributes);

	DFTRACE("Status 0x%08lx\n", Status);
	if (!NT_SUCCESS(Status))
		return Status;

	DFTRACE("Creating the subkey...\n");

	RtlInitUnicodeString(&SubkeyName, L"Parameters");
	InitializeObjectAttributes(&ObjectAttributes, &SubkeyName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, KeyHandle, NULL);
	Status = ZwCreateKey(&SubkeyHandle, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL, REG_OPTION_NON_VOLATILE, &Disposition);

	// Close the root key because it is no longer used.
	ZwClose(KeyHandle);

	DFTRACE("Status 0x%08lx\n", Status);
	if (!NT_SUCCESS(Status))
		return Status;

	*RegistryKeyHandle = SubkeyHandle;
	
	return Status;
}

NTSTATUS
DioQueryDriverParameters(
	IN HANDLE KeyHandle, 
	OUT ULONG *AddressRangeCount, 
	OUT DIO_PORT_RANGE *AddressRanges, 
	IN ULONG AddressRangesLength, 
	OUT ULONG *DeviceReported)
{
	NTSTATUS Status;

	ULONG HwAddressRangeCount;
	ULONG ResultLength;

	// Must check IRQL = Passive level.
	DASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	Status = DioReadRegistryValue(KeyHandle, L"AddressRangeCount", REG_DWORD, &HwAddressRangeCount, sizeof(HwAddressRangeCount), NULL);
	if (!NT_SUCCESS(Status))
		return Status;

	if (HwAddressRangeCount > DIO_MAXIMUM_PORT_RANGES)
		return STATUS_UNSUCCESSFUL;

	if (HwAddressRangeCount * sizeof(DIO_PORT_RANGE) > AddressRangesLength)
		return STATUS_BUFFER_TOO_SMALL;

	Status = DioReadRegistryValue(KeyHandle, L"AddressRanges", REG_BINARY, AddressRanges, AddressRangesLength, &ResultLength);

	if (!NT_SUCCESS(Status))
		return Status;

	// DeviceReported [optional]
	if (!NT_SUCCESS(
			DioReadRegistryValue(KeyHandle, L"DeviceReported", REG_DWORD, DeviceReported, 
				sizeof(*DeviceReported), &ResultLength)
		))
	{
		*DeviceReported = 0;
	}

	*AddressRangeCount = HwAddressRangeCount;
	
	return Status;
}

//
// Function for hardware resources.
//

NTSTATUS
DioCmBuildResourceList(
	IN ULONG AddressRangeCount, 
	IN DIO_PORT_RANGE *AddressRanges, 
	OUT CM_RESOURCE_LIST **ResourceList, 
	OUT ULONG *ResourceListSize)
{
	CM_RESOURCE_LIST *List;
	ULONG ListSize;
	ULONG i;

	// ResourceList
	// + DescriptorList[]
	//  + PartialResourceList.PartialDescriptors[]

	if (AddressRangeCount > DIO_MAXIMUM_PORT_RANGES)
		return STATUS_INVALID_PARAMETER;

	ListSize = FIELD_OFFSET(CM_RESOURCE_LIST, List[0].PartialResourceList.PartialDescriptors[AddressRangeCount]);

	List = (CM_RESOURCE_LIST *)DIO_ALLOC(ListSize);
	if (!List)
		return STATUS_INSUFFICIENT_RESOURCES;

	//
	// Build the our resource list.
	// We have only one CM_FULL_RESOURCE_DESCRIPTOR.
	//

	List->Count = 1;
	List->List[0].BusNumber = 0; //-1;
	List->List[0].InterfaceType = Isa;

	List->List[0].PartialResourceList.Revision = 1;
	List->List[0].PartialResourceList.Version = 1;
	List->List[0].PartialResourceList.Count = AddressRangeCount;

	for (i = 0; i < AddressRangeCount; i++)
	{
		USHORT StartAddress = AddressRanges[i].StartAddress;
		USHORT Length = AddressRanges[i].EndAddress - AddressRanges[i].StartAddress + 1;

		DFTRACE("HwPortResource[%d]: 0x%04hx - 0x%04hx\n", i, AddressRanges[i].StartAddress, AddressRanges[i].EndAddress);

		List->List[0].PartialResourceList.PartialDescriptors[i].Type = CmResourceTypePort;
		List->List[0].PartialResourceList.PartialDescriptors[i].ShareDisposition = CmResourceShareDriverExclusive; //CmResourceShareDeviceShared
		List->List[0].PartialResourceList.PartialDescriptors[i].Flags = CM_RESOURCE_PORT_IO;

		// Do double type casting to prevent the sign-extension.
		List->List[0].PartialResourceList.PartialDescriptors[i].u.Port.Start.QuadPart = (LONGLONG)(ULONGLONG)StartAddress;
		List->List[0].PartialResourceList.PartialDescriptors[i].u.Port.Length = Length;
	}

	*ResourceList = List;
	*ResourceListSize = ListSize;

	return STATUS_SUCCESS;
}

VOID
DioCmFreeResourceList(
	IN CM_RESOURCE_LIST *ResourceList)
{
	DIO_FREE(ResourceList);
}

DECLSPEC_DEPRECATED
NTSTATUS
DioPnpClaimHardwareResources(
	IN PDRIVER_OBJECT DriverObject, 
	IN HANDLE ParameterKeyHandle, 
	IN ULONG AddressRangeCount, 
	IN DIO_PORT_RANGE *AddressRanges, 
	OUT PDEVICE_OBJECT *PhysicalDeviceObject)
{
	NTSTATUS Status;
	CM_RESOURCE_LIST *ResourceList;
	ULONG ResourceListSize;
	BOOLEAN ConflictDetected = FALSE;

	// https://www.e-reading.club/chapter.php/147098/415/Cant_-_Writing_Windows_WDM_Device_Drivers.html

	// ResourceList
	// + DescriptorList[]
	//  + PartialResourceList.PartialDescriptors[]

	Status = DioCmBuildResourceList(AddressRangeCount, AddressRanges, &ResourceList, &ResourceListSize);
	if (!NT_SUCCESS(Status))
		return Status;

	DIO_IN_DEBUG_BREAKPOINT();

	// Claim our hardware resources for use
	DFTRACE("Claiming hardware resources...\n");
	Status = IoReportResourceForDetection(DriverObject, ResourceList, ResourceListSize, NULL, NULL, 0, &ConflictDetected);

	// If there was no conflict, report our device to PnP manager.
	if (NT_SUCCESS(Status) && !ConflictDetected)
	{
		PDEVICE_OBJECT DeviceObjectOwnedByPnP = NULL;

		DFTRACE("Claiming succeeded with no conflict\n");
		DFTRACE("Reporting our device to PnP manager...\n");

		Status = IoReportDetectedDevice(DriverObject, Isa, -1, -1, ResourceList, NULL, TRUE, &DeviceObjectOwnedByPnP);

		DFTRACE("Status 0x%08lx\n", Status);

		if (!NT_SUCCESS(Status))
		{
			CM_RESOURCE_LIST NullResourceList;
			NTSTATUS UnclaimStatus;

			DFTRACE("Failed to report the detected device. Unclaiming resources...\n");

			RtlZeroMemory(&NullResourceList, sizeof(NullResourceList));
			NullResourceList.Count = 0;

			UnclaimStatus = IoReportResourceForDetection(DriverObject, &NullResourceList, sizeof(NullResourceList), NULL, NULL, 0, &ConflictDetected);
			if (!NT_SUCCESS(UnclaimStatus))
				DFTRACE("WARNING - Unclaim status 0x%08lx\n", UnclaimStatus);
		}
		else
		{
			ULONG DeviceReported = 1;
			NTSTATUS WriteStatus = RtlWriteRegistryValue(RTL_REGISTRY_HANDLE, (PCWSTR)ParameterKeyHandle, L"DeviceReported", REG_DWORD, &DeviceReported, sizeof(DeviceReported));

			if(!NT_SUCCESS(WriteStatus))
				DFTRACE("WARNING - Cannot write device report state (0x%08lx)\n", WriteStatus);

			*PhysicalDeviceObject = DeviceObjectOwnedByPnP;
		}
	}

	DioCmFreeResourceList(ResourceList);

	return Status;
}

DECLSPEC_DEPRECATED
NTSTATUS
DioPnpUnclaimHardwareResources(
	IN PDRIVER_OBJECT DriverObject)
{
	BOOLEAN ConflictDetected = FALSE;
	CM_RESOURCE_LIST ResourceList;

	RtlZeroMemory(&ResourceList, sizeof(ResourceList));
	ResourceList.Count = 0;
	return IoReportResourceForDetection(DriverObject, &ResourceList, sizeof(ResourceList), NULL, NULL, 0, &ConflictDetected);
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
	IN USHORT EndAddress, 
	IN DIO_PORT_RANGE *AddressRangesAvailable, 
	IN ULONG AddressRangeCount)
/**
 *	@brief	Tests the address range is accessible or not.
 *	
 *	@param	[in] StartAddress			Starting port address.
 *	@param	[in] EndAddress				Ending port address.
 *	@param	[in] AddressRangesAvailable	Contains multiple port address ranges that claimed by PnP manager.
 *	@param	[in] AddressRangeCount		Count of port address ranges.
 *	@return								Returns FALSE if non-accessible, TRUE otherwise.
 *	
 */
{
	ULONG i;

	if (StartAddress > EndAddress)
		return FALSE;

	for (i = 0; i < AddressRangeCount; i++)
	{
		DIO_PORT_RANGE *AddressRange = AddressRangesAvailable + i;

		if (AddressRange->StartAddress <= StartAddress && StartAddress <= AddressRange->EndAddress && 
			AddressRange->StartAddress <= EndAddress && EndAddress <= AddressRange->EndAddress && 
			StartAddress <= EndAddress)
			return TRUE;
	}

	return FALSE;
}


BOOLEAN
DiopIsPortRangesOverlapping(
	IN DIO_PORT_RANGE *AddressRanges, 
	IN ULONG AddressRangeCount)
{
	ULONG i, j;

	for (i = 0; i < AddressRangeCount; i++)
	{
		DIO_PORT_RANGE Range1 = AddressRanges[i];

		for (j = i + 1; j < AddressRangeCount; j++)
		{
			DIO_PORT_RANGE Range2 = AddressRanges[j];

			if (Range2.StartAddress > Range2.EndAddress || 
				DIO_IS_CONFLICTING_ADDRESSES(Range1.StartAddress, Range1.EndAddress, Range2.StartAddress, Range2.EndAddress))
				return TRUE;
		}
	}

	return FALSE;
}

BOOLEAN
DiopValidatePacketBuffer(
	IN DIO_PACKET *Packet, 
	IN ULONG InputBufferLength, 
	IN ULONG OutputBufferLength, 
	IN ULONG IoControlCode, 
	IN DIO_PORT_RANGE *AddressRangesAvailable, 
	IN ULONG AddressRangeCount)
/**
 *	@brief	Validates the packet buffer.
 *	
 *	This function is reserved for internal use.
 *
 *	@param	[in] Packet					Address of packet buffer.
 *	@param	[in] InputBufferLength		Length of input buffer which points the packet structure.
 *	@param	[in] OutputBufferLength		Length of output buffer.
 *	@param	[in] IoControlCode			Related IOCTL code of packet buffer.
 *	@param	[in] AddressRangesAvailable	Contains multiple port address ranges that claimed by PnP manager.
 *	@param	[in] AddressRangeCount		Count of port address ranges.
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
			if (RangeCount > DIO_MAXIMUM_PORT_RANGES)
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

				if (DioTestPortRange(AddressRange->StartAddress, AddressRange->EndAddress, AddressRangesAvailable, AddressRangeCount))
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
	IN DIO_PORT_RANGE *AvailableRanges, 
	IN ULONG AvailableRangeCount, 
	OPTIONAL IN OUT PUCHAR Buffer, 
	IN ULONG BufferLength, 
	OUT ULONG *TransferredLength, 
	IN BOOLEAN Write)
/**
 *	@brief	Do the direct port I/O for given address range.
 *	
 *	@param	[in] Ranges					Contains one or multiple port range(s).
 *	@param	[in] Count					Number of port range.
 *	@param	[in] AvailableRanges		Contains multiple port address ranges that claimed by PnP manager.
 *	@param	[in] AvailableRangeCount	Count of port address ranges.
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
	
	if (Count > DIO_MAXIMUM_PORT_RANGES)
	{
		DFTRACE_DBG("Maximum entry count exceeded\n");
		return FALSE;
	}

	if (!DIO_IS_OPTION_ENABLED(DIO_CFGB_ALLOW_PORT_RANGE_OVERLAP))
	{
		if (DiopIsPortRangesOverlapping(Ranges, Count))
		{
			DFTRACE_DBG("Range overlapping detected\n");
			return FALSE;
		}
	}

	for (i = 0; i < Count; i++)
	{
		DFTRACE_DBG("[%d] 0x%x - 0x%x\n", i, Ranges[i].StartAddress, Ranges[i].EndAddress);

		if (!DioTestPortRange(Ranges[i].StartAddress, Ranges[i].EndAddress, AvailableRanges, AvailableRangeCount))
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
	BOOLEAN Result = FALSE;
	HANDLE CurrentProcessId = PsGetProcessId(CurrentProcess);
	KIRQL Irql;

	KeAcquireSpinLock(&DiopProcessLock, &Irql);

	RegisteredProcess = (PEPROCESS)InterlockedCompareExchangePointer((PVOID *)&DiopRegisteredProcess, (PVOID)CurrentProcess, 0);
	if (!RegisteredProcess)
	{
		ObfReferenceObject(CurrentProcess);
		Result = TRUE;
	}

	KeReleaseSpinLock(&DiopProcessLock, Irql);
	
	if (Result)
		DFTRACE("Registered %d\n", CurrentProcessId);
	else
		DFTRACE("Failed to register %d because it is already registered\n", CurrentProcessId);


	return Result;
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
	BOOLEAN Result = FALSE;
	HANDLE RegisteredProcessId;
	KIRQL Irql;

	KeAcquireSpinLock(&DiopProcessLock, &Irql);

	RegisteredProcess = (PEPROCESS)InterlockedCompareExchangePointer((PVOID *)&DiopRegisteredProcess, 0, (PVOID)CurrentProcess);
	if (RegisteredProcess == CurrentProcess)
	{
		RegisteredProcessId = PsGetProcessId(RegisteredProcess);
		ObfDereferenceObject(RegisteredProcess);
		Result = TRUE;
	}

	KeReleaseSpinLock(&DiopProcessLock, Irql);

	if (Result)
		DFTRACE("Unregistered %d\n", RegisteredProcessId);
	else
		DFTRACE("Failed to unregister because current process is not in registered state\n");

	return Result;
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
	BOOLEAN Unregistered = FALSE;
	PEPROCESS RegisteredProcess;
	KIRQL Irql;

//	DFTRACE("Trying to unregister %d\n", UnregisterProcessId);

	// Acquire the lock so following code is not executed simultaneously.
	KeAcquireSpinLock(&DiopProcessLock, &Irql);

	RegisteredProcess = (PEPROCESS)InterlockedCompareExchangePointer((PVOID *)&DiopRegisteredProcess, 0, 0);

	if (RegisteredProcess)
	{
		if (UnregisterProcessId == PsGetProcessId(RegisteredProcess))
		{
			// Use xchg instead
			InterlockedExchangePointer((PVOID *)&DiopRegisteredProcess, 0);

			// OK, dereference it
			ObfDereferenceObject(RegisteredProcess);

			Unregistered = TRUE;
		}
	}

	KeReleaseSpinLock(&DiopProcessLock, Irql);

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
	NTSTATUS Status = STATUS_NOT_SUPPORTED;
	PIO_STACK_LOCATION IoStackLocation = IoGetCurrentIrpStackLocation(Irp);

	UNREFERENCED_PARAMETER(DeviceObject);

	DFTRACE("Major %d, Minor %d\n", IoStackLocation->MajorFunction, IoStackLocation->MinorFunction);

	Irp->IoStatus.Status = Status;
	Irp->IoStatus.Information = 0;

	IofCompleteRequest(Irp, IO_NO_INCREMENT);

	return Status;
}

NTSTATUS
DioDispatchSystemControl(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp)
/**
 *	@brief	Dispatch routine for IRP_MJ_SYSTEM_CONTROL.
 *
 *  This routine does nothing without passing down IRP to the lower level device.
 *	
 *	@param	[in] DeviceObject			Device object.
 *	@param	[in] Irp					Irp object.
 *	@return								STATUS_SUCCESS always.
 *	
 */
{
	// I hate WMI...
	DIO_DEVICE_EXTENSION *DeviceExtension = (DIO_DEVICE_EXTENSION *)DeviceObject->DeviceExtension;

	IoSkipCurrentIrpStackLocation(Irp);
	return IoCallDriver(DeviceExtension->LowerLevelDeviceObject, Irp);
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
	NTSTATUS Status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(DeviceObject);

	if (!DioRegisterSelf())
	{
		DFTRACE("Process 0x%p is already registered\n", DiopRegisteredProcess);
		Status = STATUS_ACCESS_DENIED;
	}

	Irp->IoStatus.Status = Status;
	Irp->IoStatus.Information = 0;

	IofCompleteRequest(Irp, IO_NO_INCREMENT);

	return Status;
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

	DioUnregister();

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
	DIO_DEVICE_EXTENSION *DeviceExtension;
	PIO_STACK_LOCATION IoStackLocation;
	ULONG IoControlCode;
	ULONG InputBufferLength;
	ULONG OutputBufferLength;
	ULONG DataOffset;
	ULONG OutputActualLength;
	DIO_PACKET *Packet;
	NTSTATUS Status;
	PEPROCESS CurrentProcess;
	BOOLEAN Critical;

	UNREFERENCED_PARAMETER(DeviceObject);

	DIO_IN_DEBUG_BREAKPOINT();

	CurrentProcess = PsGetCurrentProcess();
	Status = STATUS_SUCCESS;
	Critical = FALSE;

	DeviceExtension = (DIO_DEVICE_EXTENSION *)DeviceObject->DeviceExtension;
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
			Critical = TRUE;
			break;
		}

		DFTRACE_DBG("IOCTL from process %d\n", PsGetProcessId(CurrentProcess));

		if (METHOD_FROM_CTL_CODE(IoControlCode) != METHOD_BUFFERED)
		{
			Status = STATUS_NOT_SUPPORTED;
			Critical = TRUE;
			break;
		}


		//
		// 2. Validate the buffer.
		//

		Packet = (DIO_PACKET *)Irp->AssociatedIrp.SystemBuffer;
		if (!DiopValidatePacketBuffer(Packet, InputBufferLength, OutputBufferLength, IoControlCode, 
				DeviceExtension->PortResources, DeviceExtension->PortRangeCount))
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
							DeviceExtension->PortResources, 
							DeviceExtension->PortRangeCount, 
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
							DeviceExtension->PortResources, 
							DeviceExtension->PortRangeCount, 
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

	return Status;
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

	PsSetCreateProcessNotifyRoutine(DiopCreateProcessNotifyRoutine, TRUE);

	DioUnregister();

	ZwClose(DiopRegKeyHandle);

	DFTRACE("Byebye!\n\n");

#else
	DFTRACE("*** STOP! ***\n\n");
	__debugbreak();
#endif
}

NTSTATUS
DioAddDevice(
	IN PDRIVER_OBJECT DriverObject, 
	IN PDEVICE_OBJECT PhysicalDeviceObject)
{
	NTSTATUS Status = STATUS_UNSUCCESSFUL;
	PDEVICE_OBJECT DeviceObject = NULL;
	PDEVICE_OBJECT LowerLevelDeviceObject = NULL;
	BOOLEAN SymbolicLinkCreated = FALSE;
	BOOLEAN InterfaceRegistered = FALSE;
	UNICODE_STRING DeviceName;
	UNICODE_STRING DosDeviceName;
	UNICODE_STRING PhysicalDeviceSymbolicLinkName;

	DFTRACE("DriverObject 0x%p, PhysicalDeviceObject 0x%p\n", DriverObject, PhysicalDeviceObject);

	DIO_IN_DEBUG_BREAKPOINT();

	RtlInitUnicodeString(&DeviceName, L"\\Device\\Dioport");
	RtlInitUnicodeString(&DosDeviceName, L"\\DosDevices\\Dioport");

	do
	{
		DIO_DEVICE_EXTENSION *DeviceExtension;

		//
		// Create our new FDO.
		//

		Status = IoCreateDevice(DriverObject, sizeof(DIO_DEVICE_EXTENSION), &DeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
		if (!NT_SUCCESS(Status))
		{
			DFTRACE("IoCreateDevice failed\n");
			break;
		}


		//
		// Create the symbolic link for FDO.
		//

		Status = IoCreateSymbolicLink(&DosDeviceName, &DeviceName);
		if (!NT_SUCCESS(Status))
		{
			DFTRACE("IoCreateSymbolicLink failed\n");
			break;
		}

		SymbolicLinkCreated = TRUE;


		//
		// Attach our FDO to PnP PDO.
		//

		LowerLevelDeviceObject = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
		DFTRACE("IoAttachDeviceToDeviceStack returned 0x%p\n", LowerLevelDeviceObject);
		if (!LowerLevelDeviceObject)
		{
			DTRACE("Failed to attach device\n");
			break;
		}


		//
		// Register device interface so that application can open with it.
		//

		Status = IoRegisterDeviceInterface(PhysicalDeviceObject, &DiopGuidDeviceClass, NULL, &PhysicalDeviceSymbolicLinkName);
		if (!NT_SUCCESS(Status))
		{
			DFTRACE("Failed to register device interface\n");
			break;
		}

		DFTRACE("PhysicalDeviceSymbolicLinkName: %wZ\n", &PhysicalDeviceSymbolicLinkName);
		InterfaceRegistered = TRUE;

		Status = IoSetDeviceInterfaceState(&PhysicalDeviceSymbolicLinkName, TRUE);
		if (!NT_SUCCESS(Status))
		{
			DFTRACE("Failed to set device interface state\n");
			break;
		}


		//
		// Initialize our device extension which contains per-device specific data.
		//

		DeviceExtension = (DIO_DEVICE_EXTENSION *)DeviceObject->DeviceExtension;

		RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));

		DeviceExtension->LowerLevelDeviceObject = LowerLevelDeviceObject;
		DeviceExtension->PhysicalDeviceObject = PhysicalDeviceObject;
		DeviceExtension->PhysicalDeviceSymbolicLinkName = PhysicalDeviceSymbolicLinkName;
		DeviceExtension->FunctionDeviceName = DeviceName;
		DeviceExtension->FunctionDeviceSymbolicLinkName = DosDeviceName;
		DeviceExtension->DeviceState = 0;
		DeviceExtension->PortRangeCount = 0;
		DeviceExtension->DeviceRemoved = FALSE;
		
		IoInitializeRemoveLock(&DeviceExtension->RemoveLock, DIO_POOL_TAG, 0, 0);


		//
		// Clear the initialization flag.
		//

		DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

#if 0
		//
		// PnP manager does not call IRP_MN_START_DEVICE to PDO because PnP manager assumes 
		// that device is already started (where PDO is created by IoReportDetectedDevice()).
		//
		// Invalidating the device state will make PnP manager to call IRP_MN_START_DEVICE.
		//

//		DeviceExtension->DeviceState = PNP_DEVICE_RESOURCE_REQUIREMENTS_CHANGED;
//		IoInvalidateDeviceState(PhysicalDeviceObject);
#endif

		return STATUS_SUCCESS;

	} while (FALSE);

	if (InterfaceRegistered)
		RtlFreeUnicodeString(&PhysicalDeviceSymbolicLinkName);

	if (DeviceObject)
		IoDeleteDevice(DeviceObject);

	if (SymbolicLinkCreated)
		IoDeleteSymbolicLink(&DosDeviceName);


	return STATUS_UNSUCCESSFUL;
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
	NTSTATUS Status = STATUS_UNSUCCESSFUL;
	HANDLE KeyHandle = NULL;
	PDEVICE_OBJECT PhysicalDeviceObject = NULL;
	BOOLEAN ResourceClaimed = FALSE;
	ULONG DeviceReported = 0;
	ULONG i;

	DTRACE(" ############ DIO board I/O helper driver ############ \n");
	DTRACE(" Last built " __DATE__ " " __TIME__ "\n\n");
	DTRACE(" ##################################################### \n\n");

	DFTRACE("Initializing...\n");

	DIO_IN_DEBUG_BREAKPOINT();

#if 0
	do
	{
		//
		// In our driver, initialization process is shown below:
		//
		// 0. Our driver is loaded and I/O manager calls DriverEntry() in the system thread context.
		// 1. Check the registry value (DeviceReported) is non-zero or not.
		// 2. Do the following only if the value (DeviceReported) is zero or non-existent:
		//   2-a. Claim the hardware resources by calling IoReportResourceForDetection().
		//   2-b. After the resources are successfully claimed, call IoReportDetectedDevice() to report to the PnP manager.
		//        The PnP manager will create its own PDO for our FDO. Note that PDO is not ours. PnP manager manages it.
		//   2-c. Write to the registry value (DeviceReported) so that driver can recognize the device is already reported.
		// 3. Register IRP_MJ_XXX, DriverUnload, AddDevice routine to the driver object.
		// 4. After the DriverEntry returns, I/O manager calls AddDevice.
		// 5. AddDevice() does the following:
		//   5-a. Create the FDO by calling IoCreateDevice().
		//   5-b. Attach our FDO to PDO by calling IoAttachDeviceToDeviceStack().
		//   5-c. Register device interface of PDO so it can be opened by application.
		// 6. Since we have reported our device by using IoReportDetectedDevice(), 
		//    The I/O manager will not call PnP IRP_MN_START_DEVICE.
		//    To issue IRP_MN_START_DEVICE, we'll use IoInvalidateDeviceState().
		//    The driver must handle the PnP IRP_MN_QUERY_PNP_DEVICE_STATE to work properly.
		// 7. And there are many, many things to do in PnP handler...
		// 

		Status = DioOpenDriverParametersRegistry(RegistryPath, &KeyHandle);
		if (!NT_SUCCESS(Status))
		{
			DFTRACE("Failed to open driver parameters\n");
			break;
		}

		Status = DioQueryDriverParameters(KeyHandle, &DiopHwPortRangeCount, DiopHwPortResources, sizeof(DiopHwPortResources), &DeviceReported);
		if (!NT_SUCCESS(Status))
		{
			DFTRACE("Failed to query the driver parameters\n");
			break;
		}

	//	if (!DeviceReported)
	//	{
	//		// This is the first time to claim hardware resources
	//		Status = DioPnpClaimHardwareResources(DriverObject, KeyHandle, DiopHwPortRangeCount, DiopHwPortResources, &PhysicalDeviceObject);
	//		if (!NT_SUCCESS(Status))
	//		{
	//			DFTRACE("Failed to claim hardware resources\n");
	//			break;
	//		}
	//
	//		ResourceClaimed = TRUE;
	//
	//	}
	} while (FALSE);

	if (!NT_SUCCESS(Status))
	{
		if (KeyHandle)
			ZwClose(KeyHandle);

	//	if (ResourceClaimed)
	//		DioPnpUnclaimHardwareResources(DriverObject);

		return Status;
	}
#endif


	for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
		DriverObject->MajorFunction[i] = DioDispatchNotSupported;

	DriverObject->MajorFunction[IRP_MJ_CREATE] = DioDispatchCreate;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = DioDispatchClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DioDispatchIoControl;
	DriverObject->MajorFunction[IRP_MJ_CLEANUP] = DioDispatchCleanup;
	DriverObject->MajorFunction[IRP_MJ_PNP] = DioDispatchPnP;
	DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = DioDispatchSystemControl; // WMI
	DriverObject->DriverExtension->AddDevice = DioAddDevice;
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
	KeInitializeSpinLock(&DiopProcessLock);

	DiopDriverObject = DriverObject;
	DiopRegKeyHandle = KeyHandle;

	DiopConfigurationBlock.ConfigurationBits = 0;

	return Status;
}
