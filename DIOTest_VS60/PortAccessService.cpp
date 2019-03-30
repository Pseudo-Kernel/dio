
#ifndef _WIN32_WINNT
#define _WIN32_WINNT			0x500
#endif

#include "PortAccessService.h"


CPortAccessService::CPortAccessService(LPWSTR DeviceName)
/**
 *	@brief	Class constructor.
 *
 *	Note that the maximum length of DeviceName is MAX_PATH, including the null-terminator.
 *	
 *	@param	[in] DeviceName				Device name that starts with "\\\\.\\"
 *	@return								None.
 *	
 */
{
#if _MSC_VER >= 1300
	wcscpy_s(m_DeviceName, _countof(m_DeviceName), DeviceName);
#else
	wcscpy(m_DeviceName, DeviceName);
#endif
}

CPortAccessService::~CPortAccessService(void)
/**
 *	@brief	Class destructor.
 *	
 *	@return								None.
 *	
 */
{
}

BOOL CPortAccessService::InternalDeviceControl(HANDLE hDevice, ULONG IoControlCode, PVOID InBuffer, ULONG InBufferLength)
/**
 *	@brief	Do IOCTL with device.
 *
 *	This method is reserved for internal use.
 *	
 *	@param	[in] hDevice				The handle of the device.
 *	@param	[in] IoControlCode			IOCTL code.
 *	@param	[in] InBuffer				Input buffer for IOCTL.
 *	@param	[in] InBufferLength			Input buffer length for IOCTL.
 *	@return								Non-zero if successful.
 *	
 */
{
	DWORD BytesReturned = 0;
	BOOL Result = ::DeviceIoControl(hDevice, IoControlCode, InBuffer, InBufferLength, NULL, 0, &BytesReturned, NULL);
	DWORD LastError = GetLastError();

	return (Result && GetLastError() == ERROR_SUCCESS);
}

DIO_PACKET_PORTACCESS *CPortAccessService::InternalBuildPortAccessPacket(ULONG *PacketLength, ULONG Count, va_list Args)
/**
 *	@brief	Builds port access packet for IOCTL.
 *
 *	This method is reserved for internal use.
 *	
 *	@param	[out] PacketLength			Address of variable that receives the size of the packet.
 *	@param	[in] Count					Count of address range pairs.
 *	@param	[in] Args					Argument list which contains address range pairs.
 *	@return								Non-null if successful.
 *	
 */
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
/**
 *	@brief	Builds port access packet for IOCTL.
 *
 *	This method is reserved for internal use.
 *	
 *	@param	[out] PacketLength			Address of variable that receives the size of the packet.
 *	@param	[in] Count					Count of port access entries.
 *	@param	[in] Args					Address of port access entries.
 *	@return								Non-null if successful.
 *	
 */
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
/**
 *	@brief	Releases the port access packet.
 *
 *	This method is reserved for internal use.
 *	
 *	@param	[in] PortAccessPacket		Address of port access packet.
 *	@return								None.
 *	
 */
{
	delete [] ((BYTE *)PortAccessPacket);
}

BOOL CPortAccessService::RequestPortAccess(ULONG Count, ...)
/**
 *	@brief	Requests the enable access for port.
 *
 *	Note that only the recent port access setting will be applied.\n
 *	That is, previous setting will be deleted when you call RequestPortAccess().\n
 *	Port access range is inclusive.\n
 *	\n
 *	To request access for (one or) multiple ranges, see following examples:\n
 *	\n
 *	1 address range (0x7000 to 0x701f)\n
 *	=> RequestPortAccess(1, 0x7000, 0x701f);\n
 *	2 address ranges (0x7000 to 0x701f), (0x7030 to 0x703f)\n
 *	=> RequestPortAccess(2, 0x7000, 0x701f, 0x7030, 0x703f);\n
 *	3 address ranges (0x7000 to 0x701f), (0x7030 to 0x703f), (0x7050 to 0x705f)\n
 *	=> RequestPortAccess(3, 0x7000, 0x701f, 0x7030, 0x703f, 0x7050, 0x705f);\n
 *	N address ranges (s1 to d1), (s2 to d2), ..., (sN to dN)\n
 *	=> RequestPortAccess(N, s1, d1, s2, d2, ..., sN, dN);\n
 *	\n
 *	
 *	@param	[in] Count					Count of port address range pair.\n
 *										If this value is zero, other parameters will be ignored.
 *	@param	[in] ...					Address range pairs (USHORT, USHORT).
 *	@return								Non-zero if successful.
 *	
 */
{
	va_list Args;
	va_start(Args, Count);

	ULONG PacketLength;
	DIO_PACKET_PORTACCESS *Packet = InternalBuildPortAccessPacket(&PacketLength, Count, Args);

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
 *	@brief	Requests the enable access for port.
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
	ULONG PacketLength;
	DIO_PACKET_PORTACCESS *Packet = InternalBuildPortAccessPacket(&PacketLength, Count, PortAccess);
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
 *	@brief	Requests the enable access for port.
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
 *	@brief	Requests the disable access for whole port address.
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

