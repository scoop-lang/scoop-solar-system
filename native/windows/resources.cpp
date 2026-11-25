#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "solar_ephemeris.h"
#include <string>
#include <windows.h>

// scoop bundle installs resources in <program directory>/resources.
static const wchar_t bundled[] = L"\\resources\\solar";

static bool directory(const std::wstring &path) {
  DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static bool ascii(const std::wstring &path) {
  for (wchar_t c : path)
    if (c >= 0x80)
      return false;
  return true;
}

// CSPICE opens kernels with the ANSI fopen while the Scoop side and WIC use
// UTF-8, so prefer an ASCII spelling that both read the same way.
static std::wstring portable(const std::wstring &path) {
  if (ascii(path))
    return path;
  DWORD size = GetShortPathNameW(path.c_str(), nullptr, 0);
  if (size == 0)
    return path;
  std::wstring result(size, L'\0');
  size = GetShortPathNameW(path.c_str(), result.data(), size);
  result.resize(size);
  return size && ascii(result) ? result : path;
}

static std::wstring full(const std::wstring &path) {
  DWORD size = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
  if (size == 0)
    return path;
  std::wstring result(size, L'\0');
  size = GetFullPathNameW(path.c_str(), size, result.data(), nullptr);
  result.resize(size);
  return result;
}

static std::wstring locate() {
  wchar_t value[32768];
  DWORD size = GetEnvironmentVariableW(L"SCOOP_SOLAR_RESOURCES", value,
                                       sizeof(value) / sizeof(value[0]));
  if (size > 0 && size < sizeof(value) / sizeof(value[0]))
    return value;
  std::wstring executable(32768, L'\0');
  size =
      GetModuleFileNameW(nullptr, executable.data(), (DWORD)executable.size());
  if (size > 0 && size < executable.size()) {
    executable.resize(size);
    size_t slash = executable.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
      std::wstring candidate = executable.substr(0, slash) + bundled;
      if (directory(candidate))
        return candidate;
    }
  }
  return full(L"assets\\prepared");
}

const char *solar_resource_root(void) {
  static std::string root;
  if (!root.empty())
    return root.c_str();
  std::wstring path = portable(locate());
  int count = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0,
                                  nullptr, nullptr);
  if (count <= 1) {
    root = "assets/prepared";
    return root.c_str();
  }
  root.assign((size_t)count, '\0');
  WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, root.data(), count, nullptr,
                      nullptr);
  root.resize((size_t)count - 1);
  return root.c_str();
}
