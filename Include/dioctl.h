
#pragma once

//
// Port access structure for I/O control.
//

#define	DIO_IOFN_READ_CONFIGURATION		0x801
#define DIO_IOFN_WRITE_CONFIGURATION	0x802
#define	DIO_IOFN_READ_PORT				0x803
#define DIO_IOFN_WRITE_PORT				0x804

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

#define DIO_IOCTL_READ_CONFIGURATION			DIO_CREATE_IOCTL(DIO_IOFN_READ_CONFIGURATION)
#define DIO_IOCTL_WRITE_CONFIGURATION			DIO_CREATE_IOCTL(DIO_IOFN_WRITE_CONFIGURATION)
#define	DIO_IOCTL_READ_PORT						DIO_CREATE_IOCTL(DIO_IOFN_READ_PORT)
#define DIO_IOCTL_WRITE_PORT					DIO_CREATE_IOCTL(DIO_IOFN_WRITE_PORT)



#pragma pack(push, 1)

//
// Structure for Port I/O.
//

#define DIO_MAXIMUM_PORT_RANGES				256

/**
 *	@brief	Port range structure.
 *
 *	Describes port address range.\n
 */
typedef struct _DIO_PORT_RANGE {
	USHORT StartAddress;	//!< Starting port address.
	USHORT EndAddress;		//!< Ending port address.
} DIO_PORT_RANGE;


#define DIO_IS_CONFLICTING_ADDRESSES(_s1, _e1, _s2, _e2)	(					\
	((USHORT)(_s1)) <= ((USHORT)(_s2)) && ((USHORT)(_s2)) <= ((USHORT)(_e1)) || \
	((USHORT)(_s1)) <= ((USHORT)(_e2)) && ((USHORT)(_e2)) <= ((USHORT)(_e1))	\
)


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




//
// Structure for Configuration Read/Write.
//

#define DIO_CFGB_SHOW_DEBUG_OUTPUT				0x00000001
#define DIO_CFGB_ALLOW_PORT_RANGE_OVERLAP		0x00000002

/**
 *	@brief	Configuration structure.
 *
 *	Describes driver configuration.
 */
typedef	struct _DIO_CONFIGURATION_BLOCK {
	ULONG ConfigurationBits;			// Combination of DIO_CFGB_XXX
	ULONG Reserved[3];
} DIO_CONFIGURATION_BLOCK;

// Version of driver configuration data. higher 8bit means major version, lower 8bit means minor version.
#define DIO_DRIVER_CONFIGURATION_VERSION1			0x0100

/**
 *	@brief	Configuration read/write packet structure.
 *
 *	Data format may vary depending on the value of Version field.
 */
typedef struct _DIO_PACKET_READ_WRITE_CONFIGURATION {
	ULONG Version;					// DIO_DRIVER_CONFIGURATION_VERSION1
	DIO_CONFIGURATION_BLOCK ConfigurationBlock;
} DIO_PACKET_READ_WRITE_CONFIGURATION;




/**
 *	@brief	IOCTL packet.
 */
typedef union _DIO_PACKET {
	DIO_PACKET_PORT_IO PortIo;
	DIO_PACKET_READ_WRITE_CONFIGURATION ReadWriteConfiguration;
} DIO_PACKET;

#pragma pack(pop)

