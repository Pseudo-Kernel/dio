
#pragma once

typedef struct _DIOUM_DRIVER_CONTEXT		DIOUM_DRIVER_CONTEXT;

typedef struct _DIOUM_PORT_RANGE {
	USHORT StartAddress;
	USHORT EndAddress;
} DIOUM_PORT_RANGE;


#ifdef __cplusplus
extern "C" {
#endif

DIOUM_DRIVER_CONTEXT *
APIENTRY
DioInitialize(
	VOID);

BOOL
APIENTRY
DioShutdown(
	IN DIOUM_DRIVER_CONTEXT *Context);

BOOL
APIENTRY
DioRegisterPortAddressRange(
	IN DIOUM_DRIVER_CONTEXT *Context, 
	IN ULONG AddressRangeCount, 
	IN DIOUM_PORT_RANGE *AddressRanges);

BOOL
APIENTRY
DioReadPortMultiple(
	IN DIOUM_DRIVER_CONTEXT *Context, 
	IN PUCHAR Buffer, 
	IN ULONG BufferLength, 
	OPTIONAL OUT ULONG *ReturnedDataLength);

BOOL
APIENTRY
DioWritePortMultiple(
	IN DIOUM_DRIVER_CONTEXT *Context, 
	IN PUCHAR Buffer, 
	IN ULONG BufferLength, 
	OPTIONAL OUT ULONG *TransferredDataLength);

BOOL
APIENTRY
DioGetXorMask(
	IN DIOUM_DRIVER_CONTEXT *Context, 
	OUT UCHAR *ReadXorMask, 
	OUT UCHAR *WriteXorMask);

BOOL
APIENTRY
DioSetXorMask(
	IN DIOUM_DRIVER_CONTEXT *Context, 
	IN UCHAR ReadXorMask, 
	IN UCHAR WriteXorMask, 
	IN BOOL SetRead, 
	IN BOOL SetWrite);



BOOL
APIENTRY
DioVfIoctlTest(
	IN DIOUM_DRIVER_CONTEXT *Context, 
	IN ULONG AddressRangeCountMinimum, 
	IN ULONG AddressRangeCountMaximum, 
	IN ULONG TestCount);

#ifdef __cplusplus
}
#endif

