
#include <cstdio>
#include <Windows.h>
#include <Shlwapi.h>
#include "../DIOPort/dioctl.h"

#pragma comment(lib, "shlwapi.lib")

#if 0
	SC_HANDLE hSCManager;
	SC_HANDLE hService;

	WCHAR BinaryPath[MAX_PATH];

	GetCurrentDirectoryW(_countof(BinaryPath), BinaryPath);
	PathAppendW(BinaryPath, L"dioport.sys");

	__asm int 03h
	
	hSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if(!hSCManager)
	{
		printf("Failed to open SC manager\n");
		return -1;
	}

	hService = OpenServiceW(hSCManager, L"Dioport", SERVICE_ALL_ACCESS);
	if(!hService)
	{
		hService = CreateServiceW(hSCManager, L"Dioport", L"Dioport", SERVICE_ALL_ACCESS, 
			SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, 
			BinaryPath, NULL, NULL, NULL, NULL, NULL);
	}

	if(!hService)
	{
		printf("Cannot open/create service\n");
		return -1;
	}

	if(StartServiceW(hService, 0, NULL))
	{
		printf("Started!\n");
		Sleep(INFINITE);
	}
	else
	{
		printf("Failed, Lasterror = %d\n", GetLastError());
		DeleteService(hService);
		Sleep(INFINITE);
	}
#endif

int main()
{
	struct
	{
		DIO_PACKET_PORTACCESS hdr;
		DIO_PORTACCESS_ENTRY entry;
	} s;

	HANDLE hDevice = CreateFileW(L"\\\\.\\Dioport", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);

	printf("hDevice = 0x%x\n", hDevice);

	s.hdr.Count = 1;
	s.entry.StartAddress = 0x0000;
	s.entry.EndAddress = 0xffff;

	DWORD BytesReturned = 0;
	printf("IOCTL result %d\n", 
		DeviceIoControl(hDevice, DIO_IOCTL_SET_PORTACCESS, &s, sizeof(s), NULL, 0, &BytesReturned, NULL));

	printf("Testing port read...\n");
	__asm
	{
		mov dx, 60h
		in al, dx
	}
	printf("OK\n");

	return 0;
}

