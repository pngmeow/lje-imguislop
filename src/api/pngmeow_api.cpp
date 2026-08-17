#include "pngmeow_api.hpp"

#include "imgui_internal.h"
#include "../globals.hpp"
#include <imgui.h>

namespace pngmeow_api {
namespace {

// Channels may be given as floats (0..1) or as bytes (0..255); anything larger
// than 1 means the caller is thinking in bytes. A missing alpha is opaque in
// whichever of the two scales the rest of the color used.
void finish_color(ImVec4 &color, bool has_alpha) {
  bool bytes = color.x > 1.0f || color.y > 1.0f || color.z > 1.0f || (has_alpha && color.w > 1.0f);
  if (!has_alpha)
    color.w = bytes ? 255.0f : 1.0f;
  if (bytes) {
    color.x /= 255.0f;
    color.y /= 255.0f;
    color.z /= 255.0f;
    color.w /= 255.0f;
  }
}

// {r, g, b, a} or {[1], [2], [3], [4]}, alpha optional.
bool read_color_table(lua_State *L, int table_idx, ImVec4 &color) {
  auto lua = g_api->lua;

  lua->getfield(L, table_idx, "r");
  if (!lua->isnil(L, -1)) {
    color.x = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->getfield(L, table_idx, "g");
    color.y = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->getfield(L, table_idx, "b");
    color.z = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->getfield(L, table_idx, "a");
    bool has_alpha = !lua->isnil(L, -1);
    if (has_alpha)
      color.w = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    finish_color(color, has_alpha);
    return true;
  }
  lua->pop(L, 1);

  lua->rawgeti(L, table_idx, 1);
  if (!lua->isnil(L, -1)) {
    color.x = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->rawgeti(L, table_idx, 2);
    color.y = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->rawgeti(L, table_idx, 3);
    color.z = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->rawgeti(L, table_idx, 4);
    bool has_alpha = !lua->isnil(L, -1);
    if (has_alpha)
      color.w = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    finish_color(color, has_alpha);
    return true;
  }
  lua->pop(L, 1);

  return false;
}

// 0xRRGGBB (opaque) or 0xRRGGBBAA.
ImVec4 color_from_packed(double packed) {
  auto value = static_cast<unsigned int>(packed);
  if (value <= 0xFFFFFFu)
    value = (value << 8) | 0xFFu;

  return ImVec4(static_cast<float>((value >> 24) & 0xFF) / 255.0f,
                static_cast<float>((value >> 16) & 0xFF) / 255.0f,
                static_cast<float>((value >> 8) & 0xFF) / 255.0f,
                static_cast<float>(value & 0xFF) / 255.0f);
}

// Reads a color out of `opts[key]`, which may be a table or a packed number.
bool read_color_field(lua_State *L, int table_idx, const char *key, ImVec4 &color) {
  auto lua = g_api->lua;

  lua->getfield(L, table_idx, key);
  bool found = false;
  if (lua->type(L, -1) == LUA_TTABLE) {
    found = read_color_table(L, lua->gettop(L), color);
  } else if (lua->type(L, -1) == LUA_TNUMBER) {
    color = color_from_packed(lua->tonumber(L, -1));
    found = true;
  }
  lua->pop(L, 1);
  return found;
}

bool read_number_field(lua_State *L, int table_idx, const char *key, float &out) {
  auto lua = g_api->lua;

  lua->getfield(L, table_idx, key);
  bool found = !lua->isnil(L, -1);
  if (found)
    out = static_cast<float>(lua->tonumber(L, -1));
  lua->pop(L, 1);
  return found;
}

bool read_bool_field(lua_State *L, int table_idx, const char *key, bool &out) {
  auto lua = g_api->lua;

  lua->getfield(L, table_idx, key);
  bool found = !lua->isnil(L, -1);
  if (found)
    out = lua->toboolean(L, -1);
  lua->pop(L, 1);
  return found;
}

// draw_rectangle(width, height [, color] [, opts])
// draw_rectangle(width, height, r, g, b [, a] [, opts])
//
// The rectangle is drawn at the current cursor position, so set_cursor_pos
// moves it. `color` is a table ({r,g,b,a} or {1,2,3,4}, in 0..1 or 0..255) or a
// packed 0xRRGGBB / 0xRRGGBBAA number. `opts` accepts color, rounding, filled,
// thickness, border_color, border_thickness and advance.
//
// Returns the top-left screen position of the rectangle.
int draw_rectangle(lua_State *L) {
  auto lua = g_api->lua;

  int nargs = lua->gettop(L);
  float width = static_cast<float>(lua->tonumber(L, 1));
  float height = static_cast<float>(lua->tonumber(L, 2));

  ImVec4 color(1.0f, 1.0f, 1.0f, 1.0f);
  ImVec4 border_color(0.0f, 0.0f, 0.0f, 0.0f);
  bool has_border = false;
  float rounding = 0.0f;
  float thickness = 1.0f;
  float border_thickness = 1.0f;
  bool filled = true;
  bool advance = true;

  // Colors can come in as a table, as a packed number, or as loose r,g,b(,a)
  // arguments; whatever is left over is the options table.
  int opts_idx = 3;
  if (nargs >= 3 && lua->type(L, 3) == LUA_TTABLE) {
    if (read_color_table(L, 3, color))
      opts_idx = 4;
  } else if (nargs >= 3 && lua->type(L, 3) == LUA_TNUMBER) {
    if (nargs >= 5 && lua->type(L, 4) == LUA_TNUMBER && lua->type(L, 5) == LUA_TNUMBER) {
      color.x = static_cast<float>(lua->tonumber(L, 3));
      color.y = static_cast<float>(lua->tonumber(L, 4));
      color.z = static_cast<float>(lua->tonumber(L, 5));
      opts_idx = 6;
      bool has_alpha = nargs >= 6 && lua->type(L, 6) == LUA_TNUMBER;
      if (has_alpha) {
        color.w = static_cast<float>(lua->tonumber(L, 6));
        opts_idx = 7;
      }
      finish_color(color, has_alpha);
    } else {
      color = color_from_packed(lua->tonumber(L, 3));
      opts_idx = 4;
    }
  }

  if (nargs >= opts_idx && lua->type(L, opts_idx) == LUA_TTABLE) {
    read_color_field(L, opts_idx, "color", color);
    has_border = read_color_field(L, opts_idx, "border_color", border_color);
    read_number_field(L, opts_idx, "rounding", rounding);
    read_number_field(L, opts_idx, "thickness", thickness);
    if (!read_number_field(L, opts_idx, "border_thickness", border_thickness))
      border_thickness = thickness;
    read_bool_field(L, opts_idx, "filled", filled);
    read_bool_field(L, opts_idx, "advance", advance);
  }

  lua->pop(L, nargs);

  // Drawing needs a window to draw into and a cursor to draw at.
  ImGuiContext *ctx = ImGui::GetCurrentContext();
  ImGuiWindow *window = ctx ? ctx->CurrentWindow : nullptr;
  if (!window || window->SkipItems)
    return 0;

  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 size(width, height);
  ImVec2 max(pos.x + size.x, pos.y + size.y);
  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  if (filled) {
    draw_list->AddRectFilled(pos, max, ImGui::GetColorU32(color), rounding);
    if (has_border && border_thickness > 0.0f) {
      draw_list->AddRect(pos, max, ImGui::GetColorU32(border_color), rounding, ImDrawFlags_None,
                         border_thickness);
    }
  } else if (thickness > 0.0f) {
    draw_list->AddRect(pos, max, ImGui::GetColorU32(color), rounding, ImDrawFlags_None, thickness);
  }

  // Reserve the space so following widgets lay out below the rectangle, unless
  // the caller wants to keep drawing over the same spot.
  if (advance)
    ImGui::Dummy(size);

  lua->pushnumber(L, pos.x);
  lua->pushnumber(L, pos.y);
  return 2;
}

void set_function(lua_State *L, int (*fn)(lua_State *), const char *name) {
  auto lua = g_api->lua;
  lua->pushcclosure(L, reinterpret_cast<void *>(fn), 0);
  lua->setfield(L, -2, name);
}

} // namespace

// These live on the `imgui` table, so imgui_api::register_all has to have run
// first to create it.
void register_all(lua_State *L) {
  auto lua = g_api->lua;

  lua->pushljeenv(L);
  lua->getfield(L, -1, "imgui");
  if (lua->type(L, -1) != LUA_TTABLE) {
    lua->pop(L, 2); // Pop the missing field and ljeenv
    return;
  }

  set_function(L, draw_rectangle, "draw_rectangle");

  lua->pop(L, 2); // Pop imgui and ljeenv
}

} // namespace pngmeow_api
