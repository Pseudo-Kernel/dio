
#include <cstdio>
#include <intrin.h>
#include <Windows.h>
#include <Shlwapi.h>
#include "../Include/dioctl.h"

#include "DriverService.h"
#include "PortAccessService.h"

#pragma comment(lib, "shlwapi.lib")



VOID PortAccessTest()
{
	printf("Testing port access...\n");

	for (int i = 0; i < 0x10000; i++)
	{
		__try
		{
			volatile UCHAR c = __inbyte((USHORT)i);
			printf("Port 0x%04x allowed\n", i);
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}

	printf("End of the test\n\n");
}

int main()
{
	CDriverService *Service = new CDriverService(L"Dioport", L"Dioport.sys", FALSE);
	CPortAccessService *PortService = new CPortAccessService(L"\\\\.\\dioport");

	try
	{
		Service->Uninstall(TRUE);

		if (!Service->Install(TRUE))
			throw ("Failed to install driver service\n");

		if (!Service->Start())
			throw ("Failed to start driver service\n");

		if (!PortService->RequestPortAccess(3, 0, 0xff, 0x2000, 0x20ff, 0x8000, 0x80ff))
			throw ("Failed to request port access\n");

		PortAccessTest();
	}
	catch (CHAR *Message)
	{
		puts(Message);
	}


	printf("\nEnd\n");

	delete PortService;
	delete Service;

	Sleep(INFINITE);

	return 0;
}

