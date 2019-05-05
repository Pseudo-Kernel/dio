
#pragma once


#define	DIOUM_CONTEXT_MAGIC			'WRYY'

typedef struct _DIOUM_DRIVER_CONTEXT {
	ULONG Magic;					// DIOUM_CONTEXT_MAGIC
	HANDLE Handle;
	CRITICAL_SECTION CriticalSection;

	union
	{
		DIO_PACKET Packet;
		UCHAR Bytes[8192 + 256 * 4 + 4];
	} InputBuffer, OutputBuffer, TempBuffer;
} DIOUM_DRIVER_CONTEXT;

