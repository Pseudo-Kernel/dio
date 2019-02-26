#pragma once

#include <Windows.h>
#include "../Include/dioctl.h"

class CPortAccessService
{
public:
	CPortAccessService(LPWSTR DeviceName);
	~CPortAccessService(void);

	BOOL RequestPortAccess(ULONG Count, USHORT StartAddress1, USHORT EndAddress1, ...);
	BOOL RequestPortAccess(ULONG Count, DIO_PORTACCESS_ENTRY *PortAccess);
	BOOL RequestPortAccess(USHORT StartAddress, USHORT EndAddress);
	BOOL DisablePortAccess();

private:
	BOOL InternalDeviceControl(HANDLE hDevice, ULONG IoControlCode, PVOID InBuffer, ULONG InBufferLength);

	DIO_PACKET_PORTACCESS *InternalBuildPortAccessPacket(ULONG *PacketLength, ULONG Count, va_list Args);
	DIO_PACKET_PORTACCESS *InternalBuildPortAccessPacket(ULONG *PacketLength, ULONG Count, DIO_PORTACCESS_ENTRY *PortAccess);
	VOID InternalFreePortAccessPacket(DIO_PACKET_PORTACCESS *PortAccessPacket);

	WCHAR m_DeviceName[MAX_PATH];
};

