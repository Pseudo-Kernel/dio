
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <Windows.h>
#include "../Include/dioctl.h"
#include "../Include/dioum.h"
#include "dioum_internal.h"

#define DFTRACE(_fmt, ...)			DTRACE(__FUNCTION__ ": " _fmt, __VA_ARGS__)


VOID
CDECL
DTRACE(
	IN PSZ Format, 
	...)
/**
 *	@brief	Prints the debug string.
 *	
 *	Note that this function can print 512-length string at most.
 *
 *	@param	[in] Format					Printf-like format ASCII string.
 *	@param	[in] ...					Parameter list.
 *	@return								None.
 *	
 */
{
	va_list Args;
	CHAR Buffer[512] = { 0 } ;

	va_start(Args, Format);
	_vsnprintf(Buffer, ARRAYSIZE(Buffer) - 1, Format, Args);
	va_end(Args);

	OutputDebugStringA(Buffer);
}

PVOID
APIENTRY
DiopAllocate(
	IN ULONG Size)
/**
 *	@brief	Allocates the memory.
 *	
 *	@param	[in] Size					Size of memory to allocate.
 *	@return								Non-NULL if succeeded.
 *	
 */
{
	PVOID Pointer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);

//	if (Pointer)
//		memset(Pointer, 0, Size);

	return Pointer;
}

VOID
APIENTRY
DiopFree(
	IN PVOID Pointer)
/**
 *	@brief	Frees the memory which is allocated by DiopAllocate().
 *	
 *	@param	[in] Pointer				Allocated memory.
 *	@return								None.
 *	
 */
{
	HeapFree(GetProcessHeap(), 0, Pointer);
}

BOOL
APIENTRY
DiopValidateContext(
	IN DIOUM_DRIVER_CONTEXT *Context)
/**
 *	@brief	Validates the driver context.
 *	
 *	@param	[in] Context				Driver context to validate.
 *	@return								FALSE if invalid.
 *	
 */
{
	if (!Context)
		return FALSE;

	if (Context->Magic != DIOUM_CONTEXT_MAGIC)
		return FALSE;

	return TRUE;
}

BOOL
APIENTRY
DiopGetDataLength(
	IN ULONG AddressRangeCount, 
	IN DIO_PORT_RANGE *AddressRange, 
	OUT ULONG *DataLength)
{
	ULONG i;
	ULONG Length = 0;

	for (i = 0; i < AddressRangeCount; i++)
	{
		USHORT StartAddress = AddressRange[i].StartAddress;
		USHORT EndAddress = AddressRange[i].EndAddress;

		if (StartAddress > EndAddress)
			return FALSE;

		Length += EndAddress - StartAddress + 1;
	}

	if (DataLength)
		*DataLength = Length;

	return TRUE;
}

ULONG
APIENTRY
DiopUnsafeXorCopy(
	OUT PUCHAR DestinationBuffer, 
	IN PUCHAR SourceBuffer, 
	IN ULONG CopyLength, 
	IN UCHAR XorMask)
{
	ULONG i;

	for (i = 0; i < CopyLength; i++)
		DestinationBuffer[i] = SourceBuffer[i] ^ XorMask;

	return CopyLength;
}


DIOUM_DRIVER_CONTEXT *
APIENTRY
DioInitialize(
	VOID)
{
	DIOUM_DRIVER_CONTEXT *Context = (DIOUM_DRIVER_CONTEXT *)DiopAllocate(sizeof(*Context));
	BOOLEAN InitializedCritSection = FALSE;
	
	do
	{
		if (!Context)
			break;

		if (!InitializeCriticalSectionAndSpinCount(&Context->CriticalSection, 0x1000))
			break;

		InitializedCritSection = TRUE;

		Context->Handle = CreateFileW(L"\\\\.\\dioport", GENERIC_READ | GENERIC_WRITE, 
			0, NULL, OPEN_EXISTING, 0, NULL);

		if (Context->Handle == INVALID_HANDLE_VALUE)
			break;

		Context->ReadXorMask = 0x00;
		Context->WriteXorMask = 0xff;
		Context->InputBuffer.Packet.PortIo.RangeCount = 0;
		Context->OutputBuffer.Packet.PortIo.RangeCount = 0;

		Context->Magic = DIOUM_CONTEXT_MAGIC;

		return Context;

	} while(FALSE);

	if (InitializedCritSection)
		DeleteCriticalSection(&Context->CriticalSection);

	if (Context->Handle != NULL && Context->Handle != INVALID_HANDLE_VALUE)
		CloseHandle(Context->Handle);

	DiopFree(Context);

	return NULL;
}

BOOL
APIENTRY
DioGetXorMask(
	IN DIOUM_DRIVER_CONTEXT *Context, 
	OUT UCHAR *ReadXorMask, 
	OUT UCHAR *WriteXorMask)
{
	if (!DiopValidateContext(Context))
		return FALSE;

	if (ReadXorMask)
		*ReadXorMask = Context->ReadXorMask;

	if (WriteXorMask)
		*WriteXorMask = Context->WriteXorMask;

	return TRUE;
}

BOOL
APIENTRY
DioSetXorMask(
	IN DIOUM_DRIVER_CONTEXT *Context, 
	IN UCHAR ReadXorMask, 
	IN UCHAR WriteXorMask, 
	IN BOOL SetRead, 
	IN BOOL SetWrite)
{
	if (!DiopValidateContext(Context))
		return FALSE;

	if (SetRead)
		Context->ReadXorMask = ReadXorMask;

	if (SetWrite)
		Context->WriteXorMask = WriteXorMask;

	return TRUE;
}

BOOL
APIENTRY
DioGetDriverConfiguration(
	IN DIOUM_DRIVER_CONTEXT *Context, 
	OUT ULONG *ConfigurationBits)
{
	BOOL Result = FALSE;
	ULONG ReturnedLength = 0;
	DIO_PACKET_READ_WRITE_CONFIGURATION Packet;

	if (!DiopValidateContext(Context))
		return FALSE;

	ZeroMemory(&Packet, sizeof(Packet));
	Packet.Version = DIO_DRIVER_CONFIGURATION_VERSION1;

	EnterCriticalSection(&Context->CriticalSection);

	Result = DeviceIoControl(
		Context->Handle, 
		DIO_IOCTL_READ_CONFIGURATION, 
		(PVOID)&Packet, 
		sizeof(Packet.Version), 
		(PVOID)&Packet, 
		sizeof(Packet), 
		&ReturnedLength, 
		NULL);

	if (Result && GetLastError() == ERROR_SUCCESS)
	{
		DFTRACE("IOCTL succeeded with %d bytes returned\n", ReturnedLength);

		if (ReturnedLength == sizeof(Packet))
		{
			*ConfigurationBits = Packet.ConfigurationBlock.ConfigurationBits;
			Result = TRUE;
		}
	}

	LeaveCriticalSection(&Context->CriticalSection);

	return Result;
}

BOOL
APIENTRY
DioSetDriverConfiguration(
	IN DIOUM_DRIVER_CONTEXT *Context, 
	IN ULONG ConfigurationBits)
{
	BOOL Result = FALSE;
	ULONG ReturnedLength = 0;
	DIO_PACKET_READ_WRITE_CONFIGURATION Packet;

	if (!DiopValidateContext(Context))
		return FALSE;

	ZeroMemory(&Packet, sizeof(Packet));
	Packet.Version = DIO_DRIVER_CONFIGURATION_VERSION1;
	Packet.ConfigurationBlock.ConfigurationBits = ConfigurationBits;

	EnterCriticalSection(&Context->CriticalSection);

	Result = DeviceIoControl(
		Context->Handle, 
		DIO_IOCTL_WRITE_CONFIGURATION, 
		(PVOID)&Packet, 
		sizeof(Packet), 
		(PVOID)&Packet, 
		sizeof(Packet), 
		&ReturnedLength, 
		NULL);

	if (Result && GetLastError() == ERROR_SUCCESS)
	{
		DFTRACE("IOCTL succeeded with %d bytes returned\n", ReturnedLength);

		if (ReturnedLength == sizeof(Packet))
			Result = TRUE;
	}

	LeaveCriticalSection(&Context->CriticalSection);

	return Result;
}

BOOL
APIENTRY
DioShutdown(
	IN DIOUM_DRIVER_CONTEXT *Context)
{
	if (!DiopValidateContext(Context))
		return FALSE;

	DeleteCriticalSection(&Context->CriticalSection);

	if (Context->Handle != NULL && Context->Handle != INVALID_HANDLE_VALUE)
		CloseHandle(Context->Handle);

	memset(Context, 0, sizeof(*Context));

	DiopFree(Context);

	return TRUE;
}

BOOL
APIENTRY
DioRegisterPortAddressRange(
	IN DIOUM_DRIVER_CONTEXT *Context, 
	IN ULONG AddressRangeCount, 
	IN DIOUM_PORT_RANGE *AddressRanges)
{
	ULONG i;

	if (!DiopValidateContext(Context))
		return FALSE;

	if (AddressRangeCount > DIO_MAXIMUM_PORT_RANGES)
		return FALSE;

	EnterCriticalSection(&Context->CriticalSection);

	for (i = 0; i < AddressRangeCount; i++)
	{
		Context->InputBuffer.Packet.PortIo.AddressRange[i].StartAddress = AddressRanges[i].StartAddress;
		Context->InputBuffer.Packet.PortIo.AddressRange[i].EndAddress = AddressRanges[i].EndAddress;
	}

	Context->InputBuffer.Packet.PortIo.RangeCount = AddressRangeCount;

	LeaveCriticalSection(&Context->CriticalSection);

	return TRUE;
}

BOOL
APIENTRY
DioReadPortMultiple(
	IN DIOUM_DRIVER_CONTEXT *Context, 
	IN PUCHAR Buffer, 
	IN ULONG BufferLength, 
	OPTIONAL OUT ULONG *ReturnedDataLength)
{
	ULONG DataLength;
	BOOL Result = FALSE;

	if (!DiopValidateContext(Context))
		return FALSE;

	EnterCriticalSection(&Context->CriticalSection);

	if (DiopGetDataLength(
		Context->InputBuffer.Packet.PortIo.RangeCount, 
		Context->InputBuffer.Packet.PortIo.AddressRange, 
		&DataLength) && DataLength <= BufferLength)
	{
		ULONG HeaderLength = PACKET_PORT_IO_GET_LENGTH(Context->InputBuffer.Packet.PortIo.RangeCount);
		ULONG ReturnedLength = 0;

		Result = DeviceIoControl(
			Context->Handle, 
			DIO_IOCTL_READ_PORT, 
			(PVOID)&Context->InputBuffer, 
			HeaderLength, 
			(PVOID)&Context->OutputBuffer, 
			HeaderLength + DataLength, 
			&ReturnedLength, 
			NULL);

		if (Result && GetLastError() == ERROR_SUCCESS)
		{
			DFTRACE("IOCTL succeeded with %d bytes returned\n", ReturnedLength);

			if (ReturnedLength == HeaderLength + DataLength)
			{
				DiopUnsafeXorCopy(Buffer, Context->OutputBuffer.Bytes + HeaderLength, DataLength, Context->ReadXorMask);

				if (ReturnedDataLength)
					*ReturnedDataLength = DataLength;
			}
			else
			{
				DFTRACE("Length mismatched, assuming failed\n");
				Result = FALSE;
			}
		}
	}

	LeaveCriticalSection(&Context->CriticalSection);

	return Result;
}

BOOL
APIENTRY
DioWritePortMultiple(
	IN DIOUM_DRIVER_CONTEXT *Context, 
	IN PUCHAR Buffer, 
	IN ULONG BufferLength, 
	OPTIONAL OUT ULONG *TransferredDataLength)
{
	ULONG DataLength;
	BOOL Result = FALSE;

	if (!DiopValidateContext(Context))
		return FALSE;

	EnterCriticalSection(&Context->CriticalSection);

	if (DiopGetDataLength(
		Context->InputBuffer.Packet.PortIo.RangeCount, 
		Context->InputBuffer.Packet.PortIo.AddressRange, 
		&DataLength) && DataLength <= BufferLength)
	{
		ULONG HeaderLength = PACKET_PORT_IO_GET_LENGTH(Context->InputBuffer.Packet.PortIo.RangeCount);
		ULONG ReturnedLength = 0;

		DiopUnsafeXorCopy(Context->InputBuffer.Bytes + HeaderLength, Buffer, DataLength, Context->WriteXorMask);

		Result = DeviceIoControl(
			Context->Handle, 
			DIO_IOCTL_WRITE_PORT, 
			(PVOID)&Context->InputBuffer, 
			HeaderLength + DataLength, 
			(PVOID)&Context->OutputBuffer, 
			HeaderLength, 
			&ReturnedLength, 
			NULL);

		if (Result && GetLastError() == ERROR_SUCCESS)
		{
			DFTRACE("IOCTL succeeded with %d bytes returned\n", ReturnedLength);

			if (ReturnedLength == HeaderLength)
			{
				if (TransferredDataLength)
					*TransferredDataLength = DataLength;
			}
			else
			{
				DFTRACE("Length mismatched, assuming failed\n");
				Result = FALSE;
			}
		}
	}

	LeaveCriticalSection(&Context->CriticalSection);

	return Result;
}

BOOL
APIENTRY
DioVfTest(
	IN DIOUM_DRIVER_CONTEXT *Context, 
	IN ULONG TestFlag, 
	IN ULONG TestCount)
{
	UCHAR Buffer[24] = { 0 };
	ULONG Dummy;

	if (!DiopValidateContext(Context))
		return FALSE;

	if (TestFlag & DIOUM_VF_IO_READ)
		ReadFile(Context->Handle, Buffer, sizeof(Buffer), &Dummy, NULL);

	if (TestFlag & DIOUM_VF_IO_WRITE)
		WriteFile(Context->Handle, Buffer, sizeof(Buffer), &Dummy, NULL);

	return TRUE;
}

BOOL
APIENTRY
DioVfIoctlTest(
	IN DIOUM_DRIVER_CONTEXT *Context, 
	IN ULONG AddressRangeCountMinimum, 
	IN ULONG AddressRangeCountMaximum, 
	IN ULONG TestCount)
{
#if 1
	return FALSE;
#else
	ULONG i;

	if (!DiopValidateContext(Context))
		return FALSE;

	if (AddressRangeCountMaximum > AddressRangeCountMaximum)
		return FALSE;

	EnterCriticalSection(&Context->CriticalSection);

	srand(GetTickCount());

	for (i = 0; i < TestCount; i++)
	{
		ULONG j = 0;
		ULONG ReturnedLength = 0;
		ULONG AddressRangeCount = AddressRangeCountMinimum + 
			(rand() % (AddressRangeCountMaximum - AddressRangeCountMinimum + 1));
		BOOL Result = FALSE;

		ULONG InputBufferLength;
		ULONG OutputBufferLength;

		Context->InputBuffer.Packet.PortIo.RangeCount = AddressRangeCount;

		for (j = 0; j < 1024; j++)
			*((UCHAR *)Context->InputBuffer.Packet.PortIo.AddressRange + j) = (UCHAR)rand();

		for (j = 0; j < 16; j++)
		{
			Context->InputBuffer.Packet.PortIo.AddressRange[j].StartAddress = 0x7000;
			Context->InputBuffer.Packet.PortIo.AddressRange[j].EndAddress = 0x7000 + (rand() & 0x3ff);
		}

		InputBufferLength = rand() % 8192;
		OutputBufferLength = rand() % 8192;
		DFTRACE("InputBufferLength %d, OutputBufferLength %d\n", InputBufferLength, OutputBufferLength);

		Result = DeviceIoControl(
			Context->Handle, 
			DIO_IOCTL_READ_PORT, 
			(PVOID)&Context->InputBuffer, 
			InputBufferLength, 
			(PVOID)&Context->OutputBuffer, 
			OutputBufferLength, 
			&ReturnedLength, 
			NULL);

		DFTRACE("Read: Result %d, ReturnedLength %d, LastError %d\n", 
			Result, ReturnedLength, GetLastError());

//		Result = DeviceIoControl(
//			Context->Handle, 
//			DIO_IOCTL_WRITE_PORT, 
//			(PVOID)&Context->InputBuffer, 
//			InputBufferLength, 
//			(PVOID)&Context->OutputBuffer, 
//			OutputBufferLength, 
//			&ReturnedLength, 
//			NULL);
//
//		DTRACE("Write: Result %d, ReturnedLength %d, LastError %d\n", 
//			Result, ReturnedLength, GetLastError());
	}

	LeaveCriticalSection(&Context->CriticalSection);

	return TRUE;
#endif
}

