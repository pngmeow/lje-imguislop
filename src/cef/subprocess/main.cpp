// Sub-process executable for CEF (render / gpu / utility processes).
//
// The browser process here is a game we were injected into, so it cannot host
// CEF's sub-process entry point itself. CefSettings.browser_subprocess_path
// points at this executable instead; it lives next to the framework, which
// ships under a private module name and so has to be loaded explicitly rather
// than left to the normal search order.

#include <windows.h>

#include "include/cef_app.h"

#include "../../monaco/loader.hpp"
#include "../app.hpp"

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  if (!monaco::loader::load_sibling())
    return 1;

  CefMainArgs main_args(instance);
  CefRefPtr<CefApp> app(new MonacoCefApp());
  return CefExecuteProcess(main_args, app, nullptr);
}