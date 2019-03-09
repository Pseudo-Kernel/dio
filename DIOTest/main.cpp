
#include <cstdio>
#include <conio.h>
#include <intrin.h>
#include <Windows.h>
#include <Shlwapi.h>
#include "../Include/dioctl.h"

#include "DriverService.h"
#include "PortAccessService.h"

#pragma comment(lib, "shlwapi.lib")



void PortAccessTest()
{
	FILE *out = fopen("log.txt", "w");
	printf("Testing port access...\n");

	//
	// Test the access for whole port.
	//

	for (int i = 0; i < 0x10000; i++)
	{
		BOOL Allowed = FALSE;

		__try
		{
			//
			// WARNING : Accessing unknown port address is dangerous.
			//           Even read access, It may cause unexpected behavior such as BSOD, system crash, etc.
			//

			UCHAR c = __inbyte((USHORT)i);
			Allowed = TRUE;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			// Failed to read from the port because permission is denied.
		}

		if (out)
		{
			if (Allowed)
				printf("Access allowed for port 0x%04x\n", i);

			fprintf(out, "Port 0x%04x : %s\n", i, 
				Allowed ? "Allowed" : "Denied");
		}
	}

	if (out)
		fclose(out);

	printf("End of the test\n\n");
}

int main()
{
	//
	// Create the driver service object.
	// Service name = "Dioport"
	// Driver path = "<Current Directory>\Dioport.sys"
	//

	CDriverService *Service = new CDriverService(L"Dioport", L"Dioport.sys", FALSE);

	//
	// Create the port access service object.
	// Device name = "\\\\.\\dioport" (symbolic link of device driver)
	//

	CPortAccessService *PortService = new CPortAccessService(L"\\\\.\\dioport");
	BOOL IsLoaded = FALSE;

	do
	{
		//
		// Forcibly stop and uninstall the driver.
		//

		printf("Stopping the driver...\n");
		Service->Uninstall(TRUE);

		if (!Service->Install(TRUE))
		{
			printf("Failed to install driver service\n");
			break;
		}

		//
		// Start the driver.
		//

		printf("Starting driver...\n");
		if (!Service->Start())
		{
			printf("Failed to start driver service\n");
			break;
		}

		IsLoaded = TRUE;

		//
		// Request the port access for following 3 address ranges:
		//
		// 1. 0x7000 to 0x700f
		// 2. 0x7020 to 0x702f
		// 3. 0x7040 to 0x705f
		//

		printf("Request for port access...\n");
		if (!PortService->RequestPortAccess(3, 0x7000, 0x700f, 0x7020, 0x702f, 0x7040, 0x705f))
		{
			printf("Failed to request port access\n");
			break;
		}

		//
		// Test the port.
		//

		PortAccessTest();
	} while (FALSE);

	if (IsLoaded)
	{
		//
		// Stop the service if started.
		//

		printf("Press any key to stop driver");
		getch();
		printf("\n");

		printf("Stopping driver service...\n");
		Service->Stop();
	}

	//
	// Do the cleanup.
	//

	delete PortService;
	delete Service;

	printf("Press Ctrl+C to exit\n");

	Sleep(INFINITE);

	return 0;
}

