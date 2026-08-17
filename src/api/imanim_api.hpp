#pragma once
#include <lje_sdk.h>

namespace imanim_api {

// Adds the anim_* functions and Ease_/Policy_/ColorSpace_/Wave_/Anchor_/Dir_
// constants to the existing "imgui" table, so it must run after
// imgui_api::register_all() has created that table.
void register_all(lua_State *L);

} // namespace imanim_api
