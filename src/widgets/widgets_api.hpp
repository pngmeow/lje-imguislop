#pragma once
#include <lje_sdk.h>

namespace widgets_api {

// Adds the src/widgets controls to the existing "imgui" table, so it has to run
// after imgui_api::register_all() has created it.
void register_all(lua_State *L);

} // namespace widgets_api
