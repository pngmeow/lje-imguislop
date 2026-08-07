#include "paths.hpp"

#include <windows.h>

#include <shlobj.h>

namespace monaco::paths {
namespace {

std::wstring compute_module_dir() {
  HMODULE self = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&compute_module_dir), &self)) {
    return {};
  }

  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    auto length = GetModuleFileNameW(self, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0)
      return {};
    if (length < buffer.size()) {
      buffer.resize(length);
      break;
    }
    buffer.resize(buffer.size() * 2);
  }

  auto slash = buffer.find_last_of(L"\\/");
  if (slash == std::wstring::npos)
    return {};
  buffer.resize(slash);
  return buffer;
}

std::wstring compute_cache_dir() {
  PWSTR local_app_data = nullptr;
  std::wstring root;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local_app_data))) {
    root = local_app_data;
    CoTaskMemFree(local_app_data);
  }
  if (root.empty())
    root = payload_dir();
  else
    root += L"\\lje-imgui";
  return root + L"\\cef";
}

} // namespace

const std::wstring &module_dir() {
  static const std::wstring value = compute_module_dir();
  return value;
}

const std::wstring &payload_dir() {
  static const std::wstring value = module_dir() + L"\\lje-imgui";
  return value;
}

const std::wstring &cef_dir() {
  static const std::wstring value = payload_dir() + L"\\cef";
  return value;
}

const std::wstring &assets_dir() {
  static const std::wstring value = payload_dir() + L"\\monaco";
  return value;
}

const std::wstring &cache_dir() {
  static const std::wstring value = compute_cache_dir();
  return value;
}

std::string to_utf8(const std::wstring &value) {
  if (value.empty())
    return {};
  auto size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr,
                                  0, nullptr, nullptr);
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size,
                      nullptr, nullptr);
  return result;
}

std::wstring from_utf8(const std::string &value) {
  if (value.empty())
    return {};
  auto size =
      MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(),
                      size);
  return result;
}

bool ensure_directory(const std::wstring &path) {
  if (path.empty())
    return false;
  auto attributes = GetFileAttributesW(path.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES)
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

  auto slash = path.find_last_of(L"\\/");
  if (slash != std::wstring::npos && slash > 2)
    ensure_directory(path.substr(0, slash));

  return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

} // namespace monaco::paths