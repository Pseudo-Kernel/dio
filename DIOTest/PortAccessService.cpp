#include "PortAccessService.h"


CPortAccessService::CPortAccessService(LPWSTR DeviceName)
{
#if _MSC_VER >= 1300
	wcscpy_s(m_DeviceName, _countof(m_DeviceName), DeviceName);
#else
	wcscpy(m_DeviceName, DeviceName);
#endif
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
/**
 *	@brief	Request the enable access for port.
 *
 *	Note that only the recent port access setting will be applied.\n
 *	That is, previous setting will be deleted when you call RequestPortAccess().\n
 *	Port access range is inclusive.\n
 *	\n
 *	To request access for (one or) multiple ranges, following examples are valid:\n
 *	RequestPortAccess(1, 0x7000, 0x701f); // enable access for 0x7000 - 0x701f\n
 *	RequestPortAccess(2, 0x7000, 0x701f, 0x7030, 0x703f); // enable access for 0x7000 - 0x701f, 0x7030 - 0x703f\n
 *	
 *	@param	[in] Count					Count of port address range pair.\n
 *										If this value is zero, other parameters will be ignored.
 *	@param	[in] StartAddress1			1st starting address of port.
 *	@param	[in] EndAddress1			1st ending address of port.
 *	@param	[in] ...					Address range pairs.
 *	@return								Non-zero if successful.
 *	
 */
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
/**
 *	@brief	Request the enable access for port.
 *
 *	Note that only the recent port access setting will be applied.\n
 *	That is, previous setting will be deleted when you call RequestPortAccess().\n
 *	Port access range is inclusive.\n
 *	
 *	@param	[in] Count					Count of port access entry.
 *	@param	[in] PortAccess				Address of port access entries.
 *	@return								Non-zero if successful.
 *	
 */
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
/**
 *	@brief	Request the enable access for port.
 *
 *	Note that only the recent port access setting will be applied.\n
 *	That is, previous setting will be deleted when you call RequestPortAccess().\n
 *	Port access range is inclusive.\n
 *	
 *	@param	[in] StartAddress			Starting address of port.
 *	@param	[in] EndAddress				Ending address of port.
 *	@return								Non-zero if successful.
 *	
 */
{
	DIO_PORTACCESS_ENTRY PortAccess;
	PortAccess.StartAddress = StartAddress;
	PortAccess.EndAddress = EndAddress;

	return RequestPortAccess(1, &PortAccess);
}

BOOL CPortAccessService::DisablePortAccess()
/**
 *	@brief	Request the disable access for whole port address.
 *
 *	@return								Non-zero if successful.
 *	
 */
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

