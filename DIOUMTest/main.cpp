
#include <stdio.h>
#include <Windows.h>
#include "../Include/dioum.h"

#pragma comment(lib, "../Include/dioum.lib")


void PrintBuffer(const char *Message, unsigned char *Buffer, int Length)
{
	printf("%s: \n", Message);

	for (ULONG i = 0; i < Length; i++)
	{
		printf("%02hhX ", Buffer[i]);
		if ((i % 0x10) == 0x0f)
			printf("\n");
	}

	printf("\n");
}

int wmain(int argc, wchar_t **wargv, wchar_t **wenvp)
{
	UCHAR Buffer[0x100];
	ULONG ReturnedLength;

	DIOUM_DRIVER_CONTEXT *Context = DioInitialize();
	DIOUM_PORT_RANGE PortRange[] = {
		{ 0x7000, 0x704f }, 
	};

	if (!Context)
	{
		printf("DIO failed to initialize\n");
		return -1;
	}

	do
	{
		if (!DioRegisterPortAddressRange(Context, ARRAYSIZE(PortRange), PortRange))
		{
			printf("Failed to register port address range\n");
			break;
		}

		if (!DioReadPortMultiple(Context, Buffer, ARRAYSIZE(Buffer), &ReturnedLength))
		{
			printf("Failed to read from the port\n");
			break;
		}

		PrintBuffer("Read", Buffer, ReturnedLength);

//		for (int i = 0; i < ARRAYSIZE(Buffer); i++)
//			Buffer[i] = (UCHAR)i;
//
//		PrintBuffer("Write", Buffer, ReturnedLength);
//
//		if (!DioWritePortMultiple(Context, Buffer, ARRAYSIZE(Buffer), &ReturnedLength))
//		{
//			printf("Failed to write to the port\n");
//			break;
//		}

	} while(FALSE);

	if (Context)
		DioShutdown(Context);

	printf("Press Ctrl+C to exit...\n");

	Sleep(INFINITE);

	return 0;
}

