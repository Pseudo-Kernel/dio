
#pragma once

typedef struct _IO_ACCESS_MAP {
	UCHAR Map[8192];
} IO_ACCESS_MAP;

BOOLEAN
Ke386SetIoAccessMap(
	IN ULONG MapNumber, 
	IN IO_ACCESS_MAP *AccessMap);

BOOLEAN
Ke386QueryIoAccessMap(
	IN ULONG MapNumber, 
	IN OUT IO_ACCESS_MAP *AccessMap);

BOOLEAN
Ke386IoSetAccessProcess(
	IN PEPROCESS Process, 
	IN ULONG MapNumber);


