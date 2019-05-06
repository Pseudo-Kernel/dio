
#include "DriverService.h"
#include <Shlwapi.h>

#pragma comment(lib, "shlwapi.lib")


CDriverService::CDriverService(LPWSTR ServiceName, LPWSTR DriverFileName, BOOL AssumeFullPath)
/**
 *	@brief	Class constructor.
 *	
 *	@param	[in] ServiceName			Service name of the driver. Maximum length is 256 including null character.
 *	@param	[in] DriverFileName			File name of the driver. Maximum length is MAX_PATH including null character.
 *	@param	[in] AssumeFullPath			If this value is non-zero, DriverFileName is treated as full path.
 *	@return								None.
 *	
 */
{
#if _MSC_VER >= 1300
	wcscpy_s(m_ServiceName, _countof(m_ServiceName), ServiceName);
#else
	wcscpy(m_ServiceName, ServiceName);
#endif

	if (AssumeFullPath)
	{
		// Assume the DriverFileName has a full path.
#if _MSC_VER >= 1300
		wcscpy_s(m_DriverPath, _countof(m_DriverPath), DriverFileName);
#else
		wcscpy(m_DriverPath, DriverFileName);
#endif
	}
	else
	{
		// DriverFileName has only filename + ext part.
		// Build full path from current directory.

		DWORD CurDirLength = GetCurrentDirectoryW(_countof(m_DriverPath), m_DriverPath);
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
				DebugBreak();
		}
	}
}

CDriverService::~CDriverService(void)
/**
 *	@brief	Class destructor.
 *	
 *	@return	None.
 *	
 */
{
}


BOOL CDriverService::Install(LPWSTR ServiceName, LPWSTR DriverPath, BOOL ForceInstall)
/**
 *	@brief	Installs the driver service.
 *	
 *	The type of service start is always SERVICE_DEMAND_START.
 *	This method is reserved for internal use.
 *
 *	@param	[in] ServiceName			Service name of the driver.
 *	@param	[in] DriverPath				Full path of the driver.
 *	@param	[in] ForceInstall			If this value is non-zero and same service exists,\n
 *                                      Install() will try stop and delete service before installation.
 *	@return								Non-zero if successful.
 *	
 */
{
	SC_HANDLE hSCManager;
	SC_HANDLE hService;

	hSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (!hSCManager)
		return FALSE;

	hService = OpenServiceW(hSCManager, ServiceName, SERVICE_ALL_ACCESS);
	if (hService)
	{
		SERVICE_STATUS Status;

		if (!ForceInstall)
		{
			CloseServiceHandle(hService);
			CloseServiceHandle(hSCManager);

			return FALSE;
		}

		ControlService(hService, SERVICE_CONTROL_STOP, &Status);

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
/**
 *	@brief	Starts the driver service.
 *	
 *	This method is reserved for internal use.
 *
 *	@param	[in] ServiceName			Service name of the driver.
 *	@return								Non-zero if successful.
 *	
 */
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

	if (!StartServiceW(hService, 0, NULL))
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
/**
 *	@brief	Stops the driver service.
 *	
 *	Make sure that all instances of driver is closed before stop.\n
 *	If not, the driver will be never stopped.\n
 *	This method is reserved for internal use.
 *
 *	@param	[in] ServiceName			Service name of the driver.
 *	@return								Non-zero if successful.
 *	
 */
{
	SC_HANDLE hSCManager;
	SC_HANDLE hService;
	SERVICE_STATUS Status;

	hSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (!hSCManager)
		return FALSE;

	hService = OpenServiceW(hSCManager, ServiceName, SERVICE_ALL_ACCESS);
	if (!hService)
	{
		CloseServiceHandle(hSCManager);
		return FALSE;
	}

	if (!ControlService(hService, SERVICE_CONTROL_STOP, &Status))
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
/**
 *	@brief	Uninstalls the driver service.
 *	
 *	This method is reserved for internal use.
 *
 *	@param	[in] ServiceName			Service name of the driver.
 *	@param	[in] ForceStop				If this value is non-zero, Uninstall() will try to stop and\n
 *                                      delete service.
 *	@return								Non-zero if successful.
 *	
 */
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
		SERVICE_STATUS Status;
		ControlService(hService, SERVICE_CONTROL_STOP, &Status);
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
/**
 *	@brief	Installs the driver service.
 *	
 *	The type of service start is always SERVICE_DEMAND_START.
 *
 *	@param	[in] ForceInstall			If this value is non-zero and same service exists,\n
 *                                      Install() will try stop and delete service before installation.
 *	@return								Non-zero if successful.
 *	
 */
{
	return Install(m_ServiceName, m_DriverPath, ForceInstall);
}

BOOL CDriverService::Start()
/**
 *	@brief	Starts the driver service.
 *	
 *	@return								Non-zero if successful.
 *	
 */
{
	return Start(m_ServiceName);
}

BOOL CDriverService::Stop()
/**
 *	@brief	Stops the driver service.
 *	
 *	Make sure that all instances of driver is closed before stop.\n
 *	If not, the driver will be never stopped.\n
 *
 *	@return								Non-zero if successful.
 *	
 */
{
	return Stop(m_ServiceName);
}

BOOL CDriverService::Uninstall(BOOL ForceStop)
/**
 *	@brief	Uninstalls the driver service.
 *	
 *	@param	[in] ForceStop				If this value is non-zero, Uninstall() will try to stop and\n
 *                                      delete service.
 *	@return								Non-zero if successful.
 *	
 */
{
	return Uninstall(m_ServiceName, ForceStop);
}

