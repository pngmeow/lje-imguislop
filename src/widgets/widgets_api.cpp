#include "widgets_api.hpp"

#include "../globals.hpp"
#include "../log.hpp"
#include "color_picker.hpp"

#include "imgui_internal.h"
#include <imgui.h>

namespace widgets_api {
namespace {

// Channels cross the boundary as 0..1 floats, matching color_edit4 and
// color_picker4 rather than the 0..255 the slots happen to display.
float channel(lua_State *L, int idx) {
  return static_cast<float>(g_api->lua->tonumber(L, idx));
}

void push_color(lua_State *L, const ImVec4 &color) {
  auto lua = g_api->lua;
  lua->pushnumber(L, color.x);
  lua->pushnumber(L, color.y);
  lua->pushnumber(L, color.z);
  lua->pushnumber(L, color.w);
}

// An unknown mode name is a typo in the script, not a reason to stop drawing, so
// it falls back to no animation and says so once per call.
widgets::ColorAnim read_mode(const char *name) {
  bool ok = false;
  widgets::ColorAnim mode = widgets::color_anim_from_name(name, &ok);
  if (!ok && name)
    logger::warn("unknown color animation '%s', expected none/pulse/rainbow", name);

  return mode;
}

// color_picker4_ex(label, r, g, b, a [, mode] [, speed])
//   -> changed, r, g, b, a, mode, speed
//
// A swatch, four value slots and a popup with a color tab and an animation tab.
// `mode` is "none", "pulse" or "rainbow" and `speed` is in cycles per second;
// both are optional, and both come back out so a caller can store them.
//
// The returned color is the one that was *picked* - the animation is not baked
// into it. Pass it through color_anim() at draw time to get the animated color,
// which is what lets the picker live in one state and the drawing in another.
int color_picker4_ex(lua_State *L) {
  auto lua = g_api->lua;
  const int nargs = lua->gettop(L);

  const char *label = lua->tolstring(L, 1, nullptr);
  ImVec4 color(channel(L, 2), channel(L, 3), channel(L, 4), channel(L, 5));

  const bool has_mode = nargs >= 6 && !lua->isnil(L, 6);
  const bool has_speed = nargs >= 7 && !lua->isnil(L, 7);

  widgets::ColorAnim mode = widgets::ColorAnim::None;
  if (has_mode)
    mode = read_mode(lua->tolstring(L, 6, nullptr));

  const float speed = has_speed ? static_cast<float>(lua->tonumber(L, 7)) : 0.0f;

  lua->pop(L, nargs);

  if (!label)
    label = "";

  // Drawing needs a window; ImGui::GetID below reads the id stack of one.
  ImGuiContext *ctx = ImGui::GetCurrentContext();
  if (!ctx || !ctx->CurrentWindow) {
    lua->pushboolean(L, false);
    push_color(L, color);
    lua->pushstring(L, widgets::color_anim_name(mode));
    lua->pushnumber(L, has_speed ? speed : 1.0);
    return 7;
  }

  // Held per widget id, so a caller that ignores the mode and speed returns still
  // gets a popup that remembers. Anything passed in wins over what is held.
  widgets::ColorAnimState &anim = widgets::stored_anim(ImGui::GetID(label));
  if (has_mode)
    anim.mode = mode;
  if (has_speed)
    anim.speed = speed;

  const bool changed = widgets::ColorPicker4(label, color, anim);

  lua->pushboolean(L, changed);
  push_color(L, color);
  lua->pushstring(L, widgets::color_anim_name(anim.mode));
  lua->pushnumber(L, anim.speed);
  return 7;
}

// color_anim(r, g, b, a, mode [, speed] [, time]) -> r, g, b, a
//
// The animated color for right now. Needs no window and no frame in progress, so
// a draw loop can call it whether or not it touches ImGui at all.
int color_anim(lua_State *L) {
  auto lua = g_api->lua;
  const int nargs = lua->gettop(L);

  ImVec4 base(channel(L, 1), channel(L, 2), channel(L, 3), channel(L, 4));

  widgets::ColorAnimState anim;
  anim.mode = read_mode(lua->tolstring(L, 5, nullptr));
  if (nargs >= 6 && !lua->isnil(L, 6))
    anim.speed = static_cast<float>(lua->tonumber(L, 6));

  const bool has_time = nargs >= 7 && !lua->isnil(L, 7);
  const double time = has_time ? lua->tonumber(L, 7) : widgets::anim_clock();

  lua->pop(L, nargs);

  push_color(L, widgets::eval_color_anim(base, anim, time));
  return 4;
}

// color_anim_clock() -> seconds
//
// The clock the animations run on, for a caller that wants two colors to agree on
// a phase or to drive one by hand.
int color_anim_clock(lua_State *L) {
  g_api->lua->pushnumber(L, widgets::anim_clock());
  return 1;
}

void set_function(lua_State *L, int (*fn)(lua_State *), const char *name) {
  auto lua = g_api->lua;
  lua->pushcclosure(L, reinterpret_cast<void *>(fn), 0);
  lua->setfield(L, -2, name);
}

void set_string(lua_State *L, const char *value, const char *name) {
  auto lua = g_api->lua;
  lua->pushstring(L, value);
  lua->setfield(L, -2, name);
}

} // namespace

void register_all(lua_State *L) {
  auto lua = g_api->lua;

  lua->pushljeenv(L);
  lua->getfield(L, -1, "imgui");
  if (lua->type(L, -1) != LUA_TTABLE) {
    lua->pop(L, 2); // Pop the missing field and ljeenv
    return;
  }

  set_function(L, color_picker4_ex, "color_picker4_ex");
  set_function(L, color_anim, "color_anim");
  set_function(L, color_anim_clock, "color_anim_clock");

  // The mode names, so a script can spell them from the table instead of by hand.
  set_string(L, widgets::color_anim_name(widgets::ColorAnim::None), "ColorAnim_None");
  set_string(L, widgets::color_anim_name(widgets::ColorAnim::Pulse), "ColorAnim_Pulse");
  set_string(L, widgets::color_anim_name(widgets::ColorAnim::Rainbow), "ColorAnim_Rainbow");

  lua->pop(L, 2); // Pop imgui and ljeenv
}

} // namespace widgets_api
