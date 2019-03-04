
#pragma once


//
// Port access structure for I/O control.
//

#define	DIO_IOCODE_SET_PORTACCESS				0x801
#define DIO_IOCODE_RESET_PORTACCESS				0x802

#define DIO_IOCTL_SET_PORTACCESS				CTL_CODE(FILE_DEVICE_UNKNOWN, DIO_IOCODE_SET_PORTACCESS, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DIO_IOCTL_RESET_PORTACCESS				CTL_CODE(FILE_DEVICE_UNKNOWN, DIO_IOCODE_RESET_PORTACCESS, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push, 1)

#define DIO_PORTACCESS_ENTRY_MAXIMUM			256

typedef struct _DIO_PORTACCESS_ENTRY {
	USHORT StartAddress;
	USHORT EndAddress;
} DIO_PORTACCESS_ENTRY;

#pragma warning(push)
#pragma warning(disable: 4200)
typedef struct _DIO_PACKET_PORTACCESS {
	ULONG Count;
	DIO_PORTACCESS_ENTRY Entry[];
} DIO_PACKET_PORTACCESS;
#pragma warning(pop)

typedef union _DIO_PACKET {
	DIO_PACKET_PORTACCESS PortAccess;
} DIO_PACKET;

#pragma pack(pop)
