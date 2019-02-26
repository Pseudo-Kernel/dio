#pragma once

#include <Windows.h>

class CDriverService
{
public:
	CDriverService(LPWSTR ServiceName, LPWSTR DriverFileName, BOOL AssumeFullPath);
	~CDriverService(void);

	BOOL Install(BOOL ForceInstall);
	BOOL Start();
	BOOL Stop();
	BOOL Uninstall(BOOL ForceStop);

private:
	BOOL Install(LPWSTR ServiceName, LPWSTR DriverPath, BOOL ForceInstall);
	BOOL Start(LPWSTR ServiceName);
	BOOL Stop(LPWSTR ServiceName);
	BOOL Uninstall(LPWSTR ServiceName, BOOL ForceStop);

	WCHAR m_ServiceName[256];
	WCHAR m_DriverPath[MAX_PATH];
};

