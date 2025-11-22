#ifdef _WIN32

#include "win32.hpp"

#define WIN32_LEAN_AND_MEAN
//#define WIN32_EXTRA_LEAN
//#define UNICODE

#include <windows.h>

namespace tocfix
{

bool readRegString(const HKEY hRoot, const tstring &sRegPath, const tstring &sRegKey, tstring &sOutput)
{
    HKEY hKey = NULL;
    LSTATUS lRes = 0;

    REGSAM samDesired = KEY_READ;

    // NOTE: We want the 32 bit node key on 64 bit Windows
    #if defined(_WIN64) || defined(__x86_64__)
    samDesired |= KEY_WOW64_32KEY;
    #endif

    lRes = RegOpenKeyEx(hRoot, sRegPath.data(), 0, samDesired, &hKey);
    if (lRes != ERROR_SUCCESS)
    {
        std::fprintf(stderr, "Failed to open registry key\n");

        return false;
    }

    DWORD dwBufferSize = 0;

    // NOTE: Query the size of the value first by setting the buffer to NULL
    lRes = RegQueryValueEx(hKey, sRegKey.data(), 0, NULL, NULL, &dwBufferSize);
    if (lRes != ERROR_SUCCESS)
    {
        std::fprintf(stderr, "Failed to query key size\n");

        return false;
    }

    TCHAR *szBuffer = new TCHAR[dwBufferSize];

    lRes = RegQueryValueEx(hKey, sRegKey.data(), 0, NULL, (LPBYTE)szBuffer, &dwBufferSize);
    if (lRes != ERROR_SUCCESS)
    {
        std::fprintf(stderr, "Failed to read registry key value (%lu)\n", lRes);

        RegCloseKey(hKey);

        delete []szBuffer;

        return false;
    }

    sOutput = szBuffer;

    delete []szBuffer;

    return true;
}

}

#endif // _WIN32
