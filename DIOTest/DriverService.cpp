
#include "DriverService.h"
#include <Shlwapi.h>

#pragma comment(lib, "shlwapi.lib")


CDriverService::CDriverService(LPWSTR ServiceName, LPWSTR DriverFileName, BOOL AssumeFullPath)
{
	wcscpy_s(m_ServiceName, _countof(m_ServiceName), ServiceName);

	if (AssumeFullPath)
	{
		// Assume the DriverFileName has a full path.
		wcscpy_s(m_DriverPath, _countof(m_DriverPath), DriverFileName);
	}
	else
	{
		// DriverFileName has only filename + ext part.
		// Build full path from current directory.

		DWORD CurDirLength = GetCurrentDirectory(_countof(m_DriverPath), m_DriverPath);
		DWORD FileNameLength = wcslen(DriverFileName);

		if(CurDirLength + FileNameLength + 1 < _countof(m_DriverPath))
		{
			PathAppendW(m_DriverPath, DriverFileName);
		}
		else
		{
			// Assertion failed!
			m_DriverPath[0] = 0;
			if (IsDebuggerPresent())
				__debugbreak();
		}
	}
}

CDriverService::~CDriverService(void)
{
}


BOOL CDriverService::Install(LPWSTR ServiceName, LPWSTR DriverPath, BOOL ForceInstall)
{
	SC_HANDLE hSCManager;
	SC_HANDLE hService;

	hSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (!hSCManager)
		return FALSE;

	hService = OpenServiceW(hSCManager, ServiceName, SERVICE_ALL_ACCESS);
	if (hService)
	{
		if (!ForceInstall)
		{
			CloseServiceHandle(hService);
			CloseServiceHandle(hSCManager);

			return FALSE;
		}

		ControlService(hService, SERVICE_CONTROL_STOP, NULL);

		if (!DeleteService(hService))
		{
			CloseServiceHandle(hService);
			CloseServiceHandle(hSCManager);
			return FALSE;
		}

		CloseServiceHandle(hService);
	}

	hService = CreateServiceW(hSCManager, ServiceName, ServiceName, SERVICE_ALL_ACCESS, 
		SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, 
		DriverPath, NULL, NULL, NULL, NULL, NULL);

	if (!hService)
	{
		CloseServiceHandle(hSCManager);
		return FALSE;
	}

	CloseServiceHandle(hService);
	CloseServiceHandle(hSCManager);

	return TRUE;
}

BOOL CDriverService::Start(LPWSTR ServiceName)
{
	SC_HANDLE hSCManager;
	SC_HANDLE hService;

	hSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (!hSCManager)
		return FALSE;

	hService = OpenServiceW(hSCManager, ServiceName, SERVICE_ALL_ACCESS);
	if (!hService)
	{
		CloseServiceHandle(hSCManager);
		return FALSE;
	}

	if (!StartService(hService, 0, NULL))
	{
		CloseServiceHandle(hService);
		CloseServiceHandle(hSCManager);
		return FALSE;
	}

	CloseServiceHandle(hService);
	CloseServiceHandle(hSCManager);

	return TRUE;
}

BOOL CDriverService::Stop(LPWSTR ServiceName)
{
	SC_HANDLE hSCManager;
	SC_HANDLE hService;

	hSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (!hSCManager)
		return FALSE;

	hService = OpenServiceW(hSCManager, ServiceName, SERVICE_ALL_ACCESS);
	if (!hService)
	{
		CloseServiceHandle(hSCManager);
		return FALSE;
	}

	if (!ControlService(hService, SERVICE_CONTROL_STOP, NULL))
	{
		CloseServiceHandle(hSCManager);
		CloseServiceHandle(hService);
		return FALSE;
	}

	CloseServiceHandle(hSCManager);
	CloseServiceHandle(hService);

	return TRUE;
}

BOOL CDriverService::Uninstall(LPWSTR ServiceName, BOOL ForceStop)
{
	SC_HANDLE hSCManager;
	SC_HANDLE hService;

	hSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (!hSCManager)
		return FALSE;

	hService = OpenServiceW(hSCManager, ServiceName, SERVICE_ALL_ACCESS);
	if (!hService)
	{
		CloseServiceHandle(hSCManager);
		return FALSE;
	}

	if (ForceStop)
	{
		ControlService(hService, SERVICE_CONTROL_STOP, NULL);
	}

	if (!DeleteService(hService))
	{
		CloseServiceHandle(hService);
		CloseServiceHandle(hSCManager);
		return FALSE;
	}

	CloseServiceHandle(hService);
	CloseServiceHandle(hSCManager);

	return TRUE;
}



BOOL CDriverService::Install(BOOL ForceInstall)
{
	return Install(m_ServiceName, m_DriverPath, ForceInstall);
}

BOOL CDriverService::Start()
{
	return Start(m_ServiceName);
}

BOOL CDriverService::Stop()
{
	return Stop(m_ServiceName);
}

BOOL CDriverService::Uninstall(BOOL ForceStop)
{
	return Uninstall(m_ServiceName, ForceStop);
}

