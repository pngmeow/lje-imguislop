#include "loader.hpp"

#include <windows.h>

#include <delayimp.h>

#include <mutex>

#include "../log.hpp"
#include "paths.hpp"

#ifndef LJE_CEF_MODULE_NAME
#error "LJE_CEF_MODULE_NAME must be defined by the build (see cmake/cef.cmake)"
#endif

namespace monaco::loader {
namespace {

HMODULE g_libcef = nullptr;

// Our import table still names "libcef.dll" - that is what libcef.lib records,
// and only the file on disk was renamed. The delay load helper would resolve it
// by base name, which in a host process that ships its own Chromium (Garry's
// Mod does) hands us a completely unrelated, incompatible module. Pin the
// resolution to the exact handle we opened from our payload directory.
FARPROC WINAPI delay_load_hook(unsigned notify, PDelayLoadInfo info) {
  if (notify == dliNotePreLoadLibrary && g_libcef && info && info->szDll &&
      _stricmp(info->szDll, "libcef.dll") == 0) {
    return reinterpret_cast<FARPROC>(g_libcef);
  }
  return nullptr;
}

void load_from(const std::wstring &dir) {
  if (dir.empty()) {
    logger::error("Cannot locate our own module directory; CEF will not load");
    return;
  }

  const auto path = dir + L"\\" LJE_CEF_MODULE_NAME;

  // LOAD_WITH_ALTERED_SEARCH_PATH makes the framework's remaining dependencies
  // resolve out of our payload directory rather than the host executable's. It
  // is not what keeps us away from the host's Chromium though - a module that
  // is already loaded wins on base name regardless of search order, which is
  // why the framework and chrome_elf ship under private names.
  g_libcef = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!g_libcef) {
    logger::error("Failed to load the CEF framework (error %lu). Expected it at %S",
                  GetLastError(), path.c_str());
    return;
  }

  logger::info("Loaded the CEF framework from %S", path.c_str());
}

} // namespace
} // namespace monaco::loader

extern "C" const PfnDliHook __pfnDliNotifyHook2 = monaco::loader::delay_load_hook;

namespace monaco::loader {

bool load() {
  static std::once_flag once;
  std::call_once(once, [] { load_from(paths::cef_dir()); });
  return g_libcef != nullptr;
}

bool load_sibling() {
  static std::once_flag once;
  std::call_once(once, [] { load_from(paths::module_dir()); });
  return g_libcef != nullptr;
}

bool loaded() { return g_libcef != nullptr; }

} // namespace monaco::loader
