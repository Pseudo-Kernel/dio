
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
	FILE *out = fopen("log.txt", "w");
	printf("Testing port access...\n");

	for (int i = 0; i < 0x10000; i++)
	{
		BOOL Allowed = FALSE;
		__try
		{
			volatile UCHAR c = __inbyte((USHORT)i);
			Allowed = TRUE;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
		}

		if(out)
		{
			fprintf(out, "Port 0x%04x : %s\n", i, 
				Allowed ? "Allowed" : "Denied");
		}
	}

	if(out)
		fclose(out);

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

		printf("Stopping service...\n");
		Service->Stop();
	}
	catch (CHAR *Message)
	{
		puts(Message);
	}

	delete PortService;
	delete Service;

	printf("Press Ctrl+C to exit\n");

	Sleep(INFINITE);

	return 0;
}

