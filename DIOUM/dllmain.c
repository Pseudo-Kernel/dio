
#include <Windows.h>

HMODULE DiopModuleHandle;


BOOL
WINAPI
DllMain(
	IN HANDLE ModuleHandle, 
	IN DWORD Reason, 
	IN LPVOID Reserved)
{
	switch (Reason)
	{
	case DLL_PROCESS_ATTACH:
		DiopModuleHandle = (HMODULE)ModuleHandle;
		break;

	case DLL_PROCESS_DETACH:
		break;
	}

	return TRUE;
}
