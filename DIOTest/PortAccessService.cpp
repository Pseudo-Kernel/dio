#include "PortAccessService.h"


CPortAccessService::CPortAccessService(LPWSTR DeviceName)
{
	wcscpy_s(m_DeviceName, _countof(m_DeviceName), DeviceName);
}

CPortAccessService::~CPortAccessService(void)
{
}

BOOL CPortAccessService::InternalDeviceControl(HANDLE hDevice, ULONG IoControlCode, PVOID InBuffer, ULONG InBufferLength)
{
	DWORD BytesReturned = 0;
	BOOL Result = ::DeviceIoControl(hDevice, IoControlCode, InBuffer, InBufferLength, NULL, 0, &BytesReturned, NULL);
	DWORD LastError = GetLastError();

	return (Result && GetLastError() == ERROR_SUCCESS);
}

DIO_PACKET_PORTACCESS *CPortAccessService::InternalBuildPortAccessPacket(ULONG *PacketLength, ULONG Count, va_list Args)
{
	if (Count > DIO_PORTACCESS_ENTRY_MAXIMUM)
		return NULL;

	ULONG Length = Count * sizeof(DIO_PORTACCESS_ENTRY) + sizeof(DIO_PACKET_PORTACCESS);
	DIO_PACKET_PORTACCESS *Packet = (DIO_PACKET_PORTACCESS *) new BYTE[Length];

	Packet->Count = Count;
	for (ULONG i = 0; i < Count; i++)
	{
		Packet->Entry[i].StartAddress = va_arg(Args, USHORT);
		Packet->Entry[i].EndAddress = va_arg(Args, USHORT);
	}

	*PacketLength = Length;

	return Packet;
}

DIO_PACKET_PORTACCESS *CPortAccessService::InternalBuildPortAccessPacket(ULONG *PacketLength, ULONG Count, DIO_PORTACCESS_ENTRY *PortAccess)
{
	if (Count > DIO_PORTACCESS_ENTRY_MAXIMUM)
		return NULL;

	ULONG Length = Count * sizeof(DIO_PORTACCESS_ENTRY) + sizeof(DIO_PACKET_PORTACCESS);
	DIO_PACKET_PORTACCESS *Packet = (DIO_PACKET_PORTACCESS *) new BYTE[Length];

	Packet->Count = Count;
	for (ULONG i = 0; i < Count; i++)
		Packet->Entry[i] = PortAccess[i];

	*PacketLength = Length;

	return Packet;
}

VOID CPortAccessService::InternalFreePortAccessPacket(DIO_PACKET_PORTACCESS *PortAccessPacket)
{
	delete [] ((BYTE *)PortAccessPacket);
}

BOOL CPortAccessService::RequestPortAccess(ULONG Count, USHORT StartAddress1, USHORT EndAddress1, ...)
{
	va_list Args;
	DIO_PACKET_PORTACCESS *Packet;
	ULONG PacketLength;

	va_start(Args, Count);
	Packet = InternalBuildPortAccessPacket(&PacketLength, Count, Args);
	va_end(Args);

	if (!Packet)
		return FALSE;

	HANDLE hDevice = ::CreateFileW(m_DeviceName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hDevice == INVALID_HANDLE_VALUE)
	{
		InternalFreePortAccessPacket(Packet);
		return FALSE;
	}

	if (!InternalDeviceControl(hDevice, DIO_IOCTL_SET_PORTACCESS, Packet, PacketLength))
	{
		InternalFreePortAccessPacket(Packet);
		CloseHandle(hDevice);
		return FALSE;
	}

	InternalFreePortAccessPacket(Packet);
	CloseHandle(hDevice);

	return TRUE;
}

BOOL CPortAccessService::RequestPortAccess(ULONG Count, DIO_PORTACCESS_ENTRY *PortAccess)
{
	DIO_PACKET_PORTACCESS *Packet;
	ULONG PacketLength;

	Packet = InternalBuildPortAccessPacket(&PacketLength, Count, PortAccess);
	if (!Packet)
		return FALSE;

	HANDLE hDevice = ::CreateFileW(m_DeviceName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hDevice == INVALID_HANDLE_VALUE)
	{
		InternalFreePortAccessPacket(Packet);
		return FALSE;
	}

	if (!InternalDeviceControl(hDevice, DIO_IOCTL_SET_PORTACCESS, Packet, PacketLength))
	{
		InternalFreePortAccessPacket(Packet);
		CloseHandle(hDevice);
		return FALSE;
	}

	InternalFreePortAccessPacket(Packet);
	CloseHandle(hDevice);

	return TRUE;
}

BOOL CPortAccessService::RequestPortAccess(USHORT StartAddress, USHORT EndAddress)
{
	DIO_PORTACCESS_ENTRY PortAccess;
	PortAccess.StartAddress = StartAddress;
	PortAccess.EndAddress = EndAddress;

	return RequestPortAccess(1, &PortAccess);
}

BOOL CPortAccessService::DisablePortAccess()
{
	HANDLE hDevice = ::CreateFileW(m_DeviceName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hDevice == INVALID_HANDLE_VALUE)
		return FALSE;

	if (!InternalDeviceControl(hDevice, DIO_IOCTL_RESET_PORTACCESS, NULL, 0))
	{
		CloseHandle(hDevice);
		return FALSE;
	}

	CloseHandle(hDevice);
	return TRUE;
}

