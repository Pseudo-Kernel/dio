#pragma once

#include <Windows.h>
#include "../Include/dioctl.h"

class CPortAccessService
{
public:
	CPortAccessService(LPWSTR DeviceName);
	~CPortAccessService(void);

	//
	// The prototype (ULONG, USHORT, USHORT, ...) has changed to (ULONG, ...) because of compiler bug.
	// In following code, 0x7000 and 0x7010 will be ignored because VS2010 compiler forgots to push first 2 USHORTs.
	// RequestPortAccess(3, 0x7000, 0x700f, 0x7010, 0x701f, 0x7020, 0x702f);
	//
	BOOL RequestPortAccess(ULONG Count, ...);

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

