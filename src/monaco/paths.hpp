#pragma once
#include <string>

// Runtime layout, all relative to lje-imgui.dll so that installing the module
// stays a matter of dropping the DLL plus its payload folder into
// %USERPROFILE%\.lje_modules:
//
//   lje-imgui.dll
//   lje-imgui/
//     cef/      the framework under its private name (see cmake/cef.cmake),
//               resources, locales, lje-imgui-cef.exe
//     monaco/   index.html + the official monaco-editor bundle in vs/
//
// The Chromium profile lives in %LOCALAPPDATA% instead: the modules directory
// is not guaranteed to be writable and a cache next to the binaries would be a
// surprise on uninstall.
namespace monaco::paths {

const std::wstring &module_dir();
const std::wstring &payload_dir();
const std::wstring &cef_dir();
const std::wstring &assets_dir();
const std::wstring &cache_dir();

std::string to_utf8(const std::wstring &value);
std::wstring from_utf8(const std::string &value);

bool ensure_directory(const std::wstring &path);

} // namespace monaco::paths