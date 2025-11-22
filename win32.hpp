#ifndef _WIN32_HPP_
#define _WIN32_HPP_

#ifdef _WIN32

#include <string>

#define WIN32_LEAN_AND_MEAN
//#define WIN32_EXTRA_LEAN
//#define UNICODE

#include <windows.h>
#include <io.h>
#include <wchar.h>

namespace tocfix
{

typedef std::basic_string_view<TCHAR> tstring;

bool readRegString(const HKEY hRoot, const tstring &sRegPath, const tstring &sRegKey, tstring &sOutput);

}

#endif // _WIN32

#endif // _WIN32_HPP_
