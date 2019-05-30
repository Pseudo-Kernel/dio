
#define INITGUID

#include <stdio.h>
#include <Windows.h>
#include <SetupAPI.h>
//#include <DEVPKEY.H>
//#include <cfgmgr32.h>
#include "../Include/dioum.h"

//	{75BEC7D6-7F4E-4DAE-9A2B-B4D09B839B18}
DEFINE_GUID(DiopGuidDeviceClass, 0x75BEC7D6, 0x7F4E, 0x4DAE, 0x9A, 0x2B, 0xB4, 0xD0, 0x9B, 0x83, 0x9B, 0x18);

#pragma comment(lib, "setupapi.lib")


BOOL DioLookupStringInMultistring(PWSTR Multistring, ULONG MultistringCount, PWSTR LookupString, BOOL CaseSensitive)
{
	//
	// REG_MULTI_SZ format:
	// String1 String2 String3
	// => String\0String2\0String3\0\0
	//
	// where \0 means null terminator.
	//

	ULONG Offset;
	ULONG SubstrLength;
	ULONG LookupStringLength;
	BOOL Matched;

	Matched = FALSE;
	LookupStringLength = (ULONG)wcslen(LookupString);

	for (Offset = 0; Offset < MultistringCount && !Matched; )
	{
		if (MultistringCount - Offset < LookupStringLength)
			break;

		if (CaseSensitive)
			Matched = !wcscmp(Multistring + Offset, LookupString);
		else
			Matched = !_wcsicmp(Multistring + Offset, LookupString);

		// Calculate the length of substring.
		for (SubstrLength = 0; SubstrLength + Offset < MultistringCount; SubstrLength++)
		{
			if (!Multistring[SubstrLength + Offset])
				break;
		}

//		printf("%ws\n", Multistring + Offset);

		Offset += SubstrLength + 1;
	}

//	printf("\n");

	return Matched;
}

BOOL DiopLookupDevice(GUID *ClassGuid, PWSTR DeviceHwid, HDEVINFO *DevInfoHandle, SP_DEVINFO_DATA *DevInfo)
{
	HDEVINFO hDevInfo = INVALID_HANDLE_VALUE;
	SP_DEVINFO_DATA InfoData;
	DWORD DeviceIndex;
	PBYTE Buffer = NULL;
	ULONG BufferLength = 0;
	ULONG RequiredBufferLength;
	BOOL Result = FALSE;

	HANDLE Heap;

	Heap = GetProcessHeap();

	do
	{
		hDevInfo = SetupDiGetClassDevsW(/*ClassGuid*/NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);

		if (hDevInfo == INVALID_HANDLE_VALUE)
			break;

		BufferLength = 0x1000;
		DeviceIndex = 0;
		ZeroMemory(&InfoData, sizeof(InfoData));
		InfoData.cbSize = sizeof(InfoData);

		while (SetupDiEnumDeviceInfo(hDevInfo, DeviceIndex, &InfoData))
		{
			if ((ClassGuid && !memcmp(&InfoData.ClassGuid, ClassGuid, sizeof(GUID))) || 
				!ClassGuid)
			{
				DWORD PropertyValueType;

				Result = FALSE;
				while (!Result)
				{
					if (!Buffer)
						Buffer = (PBYTE)HeapAlloc(Heap, 0, BufferLength);

					// Check once more
					if (!Buffer)
						break;

					Result = SetupDiGetDeviceRegistryPropertyW(hDevInfo, &InfoData, SPDRP_HARDWAREID, &PropertyValueType, 
						Buffer, BufferLength, &RequiredBufferLength);

					if (!Result)
					{
						if (BufferLength >= RequiredBufferLength)
							break;

						// Insufficient buffer length
						HeapFree(Heap, 0, Buffer);
						Buffer = NULL;

						BufferLength = RequiredBufferLength;
					}
				}

				if (Result)
				{
					if (DioLookupStringInMultistring((PWSTR)Buffer, RequiredBufferLength / sizeof(WCHAR), DeviceHwid, FALSE))
						break;
				}
			}

			DeviceIndex++;
		}
	} while (FALSE);

	if (Buffer)
		HeapFree(Heap, 0, Buffer);

	if (Result)
	{
		*DevInfoHandle = hDevInfo;
		*DevInfo = InfoData;
	}
	else
	{
		if (hDevInfo != INVALID_HANDLE_VALUE)
			SetupDiDestroyDeviceInfoList(hDevInfo);
	}

	return Result;
}

BOOL DiopChangeDeviceState(HDEVINFO DevInfoHandle, SP_DEVINFO_DATA *InfoData, DWORD State)
{
	SP_PROPCHANGE_PARAMS PropertyChangeParams;

	PropertyChangeParams.ClassInstallHeader.cbSize = sizeof(PropertyChangeParams.ClassInstallHeader);
	PropertyChangeParams.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
	PropertyChangeParams.Scope = DICS_FLAG_CONFIGSPECIFIC; // DICS_FLAG_GLOBAL;
	PropertyChangeParams.StateChange = State; // DICS_XXX (DICS_ENABLE, DICS_DISABLE, ...)
	PropertyChangeParams.HwProfile = 0;

	if (SetupDiSetClassInstallParamsW(DevInfoHandle, InfoData, &PropertyChangeParams.ClassInstallHeader, sizeof(PropertyChangeParams)) && 
		SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, DevInfoHandle, InfoData))
	{
		return TRUE;
	}

	if (GetLastError() == ERROR_IN_WOW64)
	{
		// 64-bit call is required.
	}

	return FALSE;
}

BOOL DiopLookupAndChangeDeviceState(GUID *ClassGuid, PWSTR DeviceHwid, DWORD State)
{
	HDEVINFO hDevInfo;
	SP_DEVINFO_DATA InfoData;
	BOOL Result;

	if (!DiopLookupDevice(ClassGuid, DeviceHwid, &hDevInfo, &InfoData))
		return FALSE;

	Result = DiopChangeDeviceState(hDevInfo, &InfoData, State);

	SetupDiDestroyDeviceInfoList(hDevInfo);

	return Result;
}


BOOL DioEnableDevice(GUID *ClassGuid, PWSTR DeviceHwid)
{
	return DiopLookupAndChangeDeviceState(ClassGuid, DeviceHwid, DICS_ENABLE);
}

BOOL DioDisableDevice(GUID *ClassGuid, PWSTR DeviceHwid)
{
	return DiopLookupAndChangeDeviceState(ClassGuid, DeviceHwid, DICS_DISABLE);
}


int wmain(int argc, wchar_t **wargv, wchar_t **wenvp)
{
	BOOL Result;
	int i;
	ULONG Flag = 0;

	// pnpdevreqtest [on | off | rand]

	for (i = 1; i < argc; i++)
	{
		if (!_wcsicmp(L"on", wargv[i]))
			Flag |= 0x01;
		else if (!_wcsicmp(L"off", wargv[i]))
			Flag |= 0x02;
		else if (!_wcsicmp(L"rand", wargv[i]))
			Flag |= 0x04;
	}

	if (!Flag)
	{
		printf("%ws [on | off | rand]\n", wargv[0]);
		return -1;
	}

	srand(GetTickCount());

	switch (Flag & 0x07)
	{
	case 0x01:
		Result = DioEnableDevice((GUID *)&DiopGuidDeviceClass, L"Root\\DIOPort");
		printf("Enable result %d\n", Result);
		break;

	case 0x02:
		Result = DioDisableDevice((GUID *)&DiopGuidDeviceClass, L"Root\\DIOPort");
		printf("Disable result %d\n", Result);
		break;

	case 0x04:
		for (;;)
		{
			if (rand() & 1)
			{
				Result = DioEnableDevice((GUID *)&DiopGuidDeviceClass, L"Root\\DIOPort");
				printf("Enable result %d\n", Result);
			}
			else
			{
				Result = DioDisableDevice((GUID *)&DiopGuidDeviceClass, L"Root\\DIOPort");
				printf("Disable result %d\n", Result);
			}
		}
		break;

	default:
		printf("on, off, rand option cannot be mixed\n");
		return -2;
	}

	return 0;
}

