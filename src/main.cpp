#define LJE_SDK_IMPLEMENTATION
#include <lje_sdk.h>
#include "globals.hpp"
#include "log.hpp"
#include "overlay.hpp"
#include "api/imgui_api.hpp"
#include "api/imnodes_api.hpp"
#include "api/monaco_api.hpp"
#include "monaco/monaco.hpp"
#include "version.h"

LjeApi *g_api = nullptr;

LJE_MODULE_INIT() {
  logger::info("Initializing lje-imgui v%s...", LJE_IMGUI_VERSION);
  g_api = api;

  Overlay::create();
  Overlay::get()->init_async();

  return LJE_RESULT_OK;
}

LJE_MODULE_PREINIT() {
  logger::info("Registering API...");

  imgui_api::register_all(L);
  imnodes_api::register_all(L);
  monaco_api::register_all(L);

  return LJE_RESULT_OK;
}

LJE_MODULE_SHUTDOWN() {
  logger::info("Shutting down lje-imgui...");
  // Chromium first: it owns background processes and D3D textures that must go
  // away while the overlay's device is still around.
  monaco::shutdown();
  Overlay::destroy();
  return LJE_RESULT_OK;
}
