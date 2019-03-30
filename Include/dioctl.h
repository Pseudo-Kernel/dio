
#pragma once

//
// Port access structure for I/O control.
//

#define	DIO_IOCODE_SET_PORTACCESS				0x801
#define DIO_IOCODE_RESET_PORTACCESS				0x802

#ifndef _NTDDK_

//
// IOCTL macro definitions in ntddk.h.
//

#define FILE_DEVICE_UNKNOWN             0x00000022
#define METHOD_BUFFERED                 0
#define FILE_ANY_ACCESS                 0

#define CTL_CODE( DeviceType, Function, Method, Access ) (                 \
    ((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method) \
)

#endif

#define DIO_IOCTL_SET_PORTACCESS				CTL_CODE(FILE_DEVICE_UNKNOWN, DIO_IOCODE_SET_PORTACCESS, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DIO_IOCTL_RESET_PORTACCESS				CTL_CODE(FILE_DEVICE_UNKNOWN, DIO_IOCODE_RESET_PORTACCESS, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push, 1)

#define DIO_PORTACCESS_ENTRY_MAXIMUM			256

/**
 *	@brief	Port access entry structure.
 *
 *	Describes port address range.\n
 */
typedef struct _DIO_PORTACCESS_ENTRY {
	USHORT StartAddress;	//!< Starting port address.
	USHORT EndAddress;		//!< Ending port address.
} DIO_PORTACCESS_ENTRY;

#pragma warning(push)
#pragma warning(disable: 4200)

/**
 *	@brief	Port access packet structure.
 *
 *	Contains one or multiple port address ranges.\n
 */
typedef struct _DIO_PACKET_PORTACCESS {
	ULONG Count;			//!< Count of DIO_PORTACCESS_ENTRY.
	DIO_PORTACCESS_ENTRY Entry[];
} DIO_PACKET_PORTACCESS;
#pragma warning(pop)

C_ASSERT(sizeof(DIO_PACKET_PORTACCESS) == sizeof(ULONG));

/**
 *	@brief	IOCTL packet.
 */
typedef union _DIO_PACKET {
	DIO_PACKET_PORTACCESS PortAccess;
} DIO_PACKET;

#pragma pack(pop)

