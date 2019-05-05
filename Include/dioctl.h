
#pragma once

//
// Port access structure for I/O control.
//

#define	DIO_IOFN_READ_PORT				0x801
#define DIO_IOFN_WRITE_PORT				0x802

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

#define	DIO_CREATE_IOCTL(_fn)					CTL_CODE(FILE_DEVICE_UNKNOWN, (_fn), METHOD_BUFFERED, FILE_ANY_ACCESS)

#define	DIO_IOCTL_READ_PORT						DIO_CREATE_IOCTL(DIO_IOFN_READ_PORT)
#define DIO_IOCTL_WRITE_PORT					DIO_CREATE_IOCTL(DIO_IOFN_WRITE_PORT)



#pragma pack(push, 1)

#define DIO_MAXIMUM_PORT_IO_REQUEST				256

/**
 *	@brief	Port range structure.
 *
 *	Describes port address range.\n
 */
typedef struct _DIO_PORT_RANGE {
	USHORT StartAddress;	//!< Starting port address.
	USHORT EndAddress;		//!< Ending port address.
} DIO_PORT_RANGE;

#pragma warning(push)
#pragma warning(disable: 4200)

/**
 *	@brief	Port access packet structure.
 *
 *	Contains one or multiple port address ranges.\n
 *	[RangeCount] [AddressRange1, AddressRange2, ... AddressRangeN] [Data]
 */
typedef struct _DIO_PACKET_PORT_IO {
	ULONG RangeCount;				//!< Count of DIO_PORT_RANGE.
	DIO_PORT_RANGE AddressRange[];	//!< Address range to read/write.
	// UCHAR Data[];
} DIO_PACKET_PORT_IO;
#pragma warning(pop)

#define	PACKET_PORT_IO_GET_LENGTH(_range_cnt)	\
	( sizeof(DIO_PACKET_PORT_IO) + (_range_cnt) * sizeof(DIO_PORT_RANGE) )

#define	PACKET_PORT_IO_GET_DATA_ADDRESS(_port_io)	\
	( (PUCHAR)((_port_io)->AddressRange + (_port_io)->RangeCount) )


/**
 *	@brief	IOCTL packet.
 */
typedef union _DIO_PACKET {
	DIO_PACKET_PORT_IO PortIo;
} DIO_PACKET;

#pragma pack(pop)

