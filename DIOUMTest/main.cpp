
#include <stdio.h>
#include <Windows.h>
#include "../Include/dioum.h"

#pragma comment(lib, "../Include/dioum.lib")

int main()
{
	DIOUM_DRIVER_CONTEXT *Context = DioInitialize();

	DIOUM_PORT_RANGE PortRange[] = {
		{ 0x7000, 0x703f }, 
		{ 0x7050, 0x707f }, 
	};

	UCHAR Buffer[0x100];
	ULONG ReturnedLength;

	if (!Context)
	{
		printf("DIO failed to initialize\n");
		return -1;
	}

//	DioVfIoctlTest(Context, 0, 129, 100000);

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

		for (int i = 0; i < ARRAYSIZE(Buffer); i++)
			Buffer[i] = (UCHAR)i;

		if (!DioWritePortMultiple(Context, Buffer, ARRAYSIZE(Buffer), &ReturnedLength))
		{
			printf("Failed to write to the port\n");
			break;
		}

	} while(FALSE);

	if (Context)
		DioShutdown(Context);

	return 0;
}

