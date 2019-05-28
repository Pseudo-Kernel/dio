
#ifndef __DIOPORT_H__
#define __DIOPORT_H__

//
// Definitions for options to compile.
//

#define	__DIO_DENY_CONVENTIONAL_PORT_ACCESS		// To deny the conventional port access
#define	__DIO_SUPPORT_UNLOAD					// To support driver unload
//#define __DIO_IGNORE_BREAKPOINT					// This option overrides DIO_IN_DEBUG_BREAKPOINT() to do nothing.
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

typedef struct _DIO_DEVICE_EXTENSION {
	PDEVICE_OBJECT LowerLevelDeviceObject;
	PDEVICE_OBJECT PhysicalDeviceObject;
	UNICODE_STRING PhysicalDeviceSymbolicLinkName;
	UNICODE_STRING FunctionDeviceName;
	UNICODE_STRING FunctionDeviceSymbolicLinkName;

	PNP_DEVICE_STATE DeviceState;	// Our device state
	IO_REMOVE_LOCK RemoveLock;		// Remove lock

	ULONG PortRangeCount;
	DIO_PORT_RANGE PortResources[DIO_MAXIMUM_PORT_RANGES];

} DIO_DEVICE_EXTENSION;


//
// Global variables.
//

//	{75BEC7D6-7F4E-4DAE-9A2B-B4D09B839B18}
//DEFINE_GUID(DiopGuidDeviceClass, 0x75BEC7D6, 0x7F4E, 0x4DAE, 0x9A, 0x2B, 0xB4, 0xD0, 0x9B, 0x83, 0x9B, 0x18);

extern HANDLE DiopRegKeyHandle;
extern PDRIVER_OBJECT DiopDriverObject;
extern KSPIN_LOCK DiopPortReadWriteLock;
extern KSPIN_LOCK DiopProcessLock;
extern volatile PEPROCESS DiopRegisteredProcess;

extern DIO_CONFIGURATION_BLOCK DiopConfigurationBlock;

extern ULONG DiopHwPortRangeCount;
extern DIO_PORT_RANGE DiopHwPortResources[DIO_MAXIMUM_PORT_RANGES];


//
// Utility functions.
//

VOID
DioDbgTrace(
	IN BOOLEAN ForceOutput, 
	IN PSZ Format, 
	...);

PSZ
DioPnpRtlLookupMinorFunctionName(
	IN ULONG MinorFunction);


NTSTATUS
DioReadRegistryValue(
	IN HANDLE KeyHandle, 
	IN PWSTR ValueNameString, 
	IN ULONG ExpectedValueType, 
	IN OUT PVOID Value, 
	IN ULONG ValueLength, 
	OPTIONAL OUT ULONG *ResultLength);

NTSTATUS
DioOpenDriverParametersRegistry(
	IN PUNICODE_STRING DriverRegistryPath, 
	OUT HANDLE *RegistryKeyHandle);

NTSTATUS
DioQueryDriverParameters(
	IN HANDLE KeyHandle, 
	OUT ULONG *AddressRangeCount, 
	OUT DIO_PORT_RANGE *AddressRanges, 
	IN ULONG AddressRangesLength, 
	OUT ULONG *DeviceReported);


//
// Function for hardware resources.
//

NTSTATUS
DioCmBuildResourceList(
	IN ULONG AddressRangeCount, 
	IN DIO_PORT_RANGE *AddressRanges, 
	OUT CM_RESOURCE_LIST **ResourceList, 
	OUT ULONG *ResourceListSize);

VOID
DioCmFreeResourceList(
	IN CM_RESOURCE_LIST *ResourceList);

NTSTATUS
DioPnpClaimHardwareResources(
	IN PDRIVER_OBJECT DriverObject, 
	IN HANDLE ParameterKeyHandle, 
	IN ULONG AddressRangeCount, 
	IN DIO_PORT_RANGE *AddressRanges, 
	OUT PDEVICE_OBJECT *PhysicalDeviceObject);

NTSTATUS
DioPnpUnclaimHardwareResources(
	IN PDRIVER_OBJECT DriverObject);


//
// Our I/O control packet helper function.
//

VOID
DioDbgDumpBytes(
	IN PSZ Message, 
	IN ULONG DumpLength, 
	IN ULONG DumpLengthMaximum, 
	IN PUCHAR Buffer);

BOOLEAN
DioTestPortRange(
	IN USHORT StartAddress, 
	IN USHORT EndAddress);

BOOLEAN
DiopIsConflictingPortRange(
	IN ULONG AddressRangeCount, 
	IN DIO_PORT_RANGE *AddressRanges);

BOOLEAN
DiopValidatePacketBuffer(
	IN DIO_PACKET *Packet, 
	IN ULONG InputBufferLength, 
	IN ULONG OutputBufferLength, 
	IN ULONG IoControlCode);

BOOLEAN
DiopInternalPortIo(
	IN USHORT BaseAddress, 
	IN OUT PUCHAR Buffer, 
	IN ULONG Length, 
	IN BOOLEAN Write);

BOOLEAN
DioPortIo(
	IN DIO_PORT_RANGE *Ranges, 
	IN ULONG Count, 
	OPTIONAL IN OUT PUCHAR Buffer, 
	IN ULONG BufferLength, 
	OUT ULONG *TransferredLength, 
	IN BOOLEAN Write);

BOOLEAN
DioIsRegistered(
	VOID);

BOOLEAN
DioRegisterSelf(
	VOID);

BOOLEAN
DioUnregister(
	VOID);

BOOLEAN
DioForceUnregister(
	IN HANDLE UnregisterProcessId);


//
// Our callback function.
//

VOID
DiopCreateProcessNotifyRoutine(
	IN HANDLE ParentId, 
	IN HANDLE ProcessId, 
	IN BOOLEAN Create);


//
// Our dispatch function.
//

NTSTATUS
DioDispatchNotSupported(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp);

NTSTATUS
DioDispatchCreate(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp);

NTSTATUS
DioDispatchClose(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp);

NTSTATUS
DioDispatchCleanup(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp);

NTSTATUS
DioDispatchIoControl(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp);

NTSTATUS
DioDispatchPnP(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp);

VOID
DioDriverUnload(
	IN PDRIVER_OBJECT DriverObject);

NTSTATUS
DioAddDevice(
	IN PDRIVER_OBJECT DriverObject, 
	IN PDEVICE_OBJECT PhysicalDeviceObject);


//
// 
//


#endif
