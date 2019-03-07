
#include <cstdio>
#include <conio.h>
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
			// WARNING : Accessing unknown port address is dangerous.
			//           It may cause unexpected behavior such as BSOD, system crash, etc.

			UCHAR c = __inbyte((USHORT)i);
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
	BOOL IsLoaded = FALSE;

	do
	{
		printf("Stopping the driver...\n");
		Service->Uninstall(TRUE);

		if (!Service->Install(TRUE))
		{
			printf("Failed to install driver service\n");
			break;
		}

		printf("Starting driver...\n");
		if (!Service->Start())
		{
			printf("Failed to start driver service\n");
			break;
		}

		IsLoaded = TRUE;

		printf("Request for port access...\n");
		if (!PortService->RequestPortAccess(3, 0x7000, 0x700f, 0x7020, 0x702f, 0x7040, 0x705f))
		{
			printf("Failed to request port access\n");
			break;
		}

		PortAccessTest();
	} while (FALSE);

	if (IsLoaded)
	{
		printf("Press any key to stop driver");
		getch();
		printf("\n");

		printf("Stopping driver service...\n");
		Service->Stop();
	}

	delete PortService;
	delete Service;

	printf("Press Ctrl+C to exit\n");

	Sleep(INFINITE);

	return 0;
}

