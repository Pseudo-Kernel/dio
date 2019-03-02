
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
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}

		if (out)
		{
			if (Allowed)
				printf("Access allowed for port 0x%04x\n", i);

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
		printf("Stopping the driver...\n");
		Service->Uninstall(TRUE);

		if (!Service->Install(TRUE))
			throw ("Failed to install driver service\n");

		printf("Starting driver...\n");
		if (!Service->Start())
			throw ("Failed to start driver service\n");

		printf("Request for port access...\n");
		if (!PortService->RequestPortAccess(3, 0x7000, 0x700f, 0x7020, 0x702f, 0x7040, 0x705f))
			throw ("Failed to request port access\n");

		printf("Testing port access...\n");
		PortAccessTest();

		printf("Stopping driver service...\n");
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

