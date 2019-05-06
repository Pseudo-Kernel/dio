
#include <stdio.h>
#include <Windows.h>

#pragma comment(lib, "crypt32.lib")

int wmain(int argc, wchar_t **wargv, wchar_t ** wenvp)
{
	if (argc < 2)
	{
		printf("\nusage: \"%ls\" [CertStore]\n", wargv[0]);
		return -1;
	}

	if (!CertUnregisterSystemStore(wargv[1], CERT_STORE_DELETE_FLAG | CERT_SYSTEM_STORE_CURRENT_USER))
	{
		printf("CertUnregisterSystemStore failed\n");
		return -2;
	}

	printf("CertUnregisterSystemStore succeeded\n");

	return 0;
}
