#include "imgui_api.hpp"

#include "imgui_internal.h"
#include "../globals.hpp"
#include "../overlay.hpp"
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <vector>
#include <cfloat>
#include <cstring>

namespace imgui_api {

// Color name to ImGuiCol mapping
static int get_color_index(const char *name) {
  struct ColorMapping {
    const char *name;
    int index;
  };
  static const ColorMapping mappings[] = {
      {"text", ImGuiCol_Text},
      {"text_disabled", ImGuiCol_TextDisabled},
      {"window_bg", ImGuiCol_WindowBg},
      {"child_bg", ImGuiCol_ChildBg},
      {"popup_bg", ImGuiCol_PopupBg},
      {"border", ImGuiCol_Border},
      {"border_shadow", ImGuiCol_BorderShadow},
      {"frame_bg", ImGuiCol_FrameBg},
      {"frame_bg_hovered", ImGuiCol_FrameBgHovered},
      {"frame_bg_active", ImGuiCol_FrameBgActive},
      {"title_bg", ImGuiCol_TitleBg},
      {"title_bg_active", ImGuiCol_TitleBgActive},
      {"title_bg_collapsed", ImGuiCol_TitleBgCollapsed},
      {"menu_bar_bg", ImGuiCol_MenuBarBg},
      {"scrollbar_bg", ImGuiCol_ScrollbarBg},
      {"scrollbar_grab", ImGuiCol_ScrollbarGrab},
      {"scrollbar_grab_hovered", ImGuiCol_ScrollbarGrabHovered},
      {"scrollbar_grab_active", ImGuiCol_ScrollbarGrabActive},
      {"check_mark", ImGuiCol_CheckMark},
      {"slider_grab", ImGuiCol_SliderGrab},
      {"slider_grab_active", ImGuiCol_SliderGrabActive},
      {"button", ImGuiCol_Button},
      {"button_hovered", ImGuiCol_ButtonHovered},
      {"button_active", ImGuiCol_ButtonActive},
      {"header", ImGuiCol_Header},
      {"header_hovered", ImGuiCol_HeaderHovered},
      {"header_active", ImGuiCol_HeaderActive},
      {"separator", ImGuiCol_Separator},
      {"separator_hovered", ImGuiCol_SeparatorHovered},
      {"separator_active", ImGuiCol_SeparatorActive},
      {"resize_grip", ImGuiCol_ResizeGrip},
      {"resize_grip_hovered", ImGuiCol_ResizeGripHovered},
      {"resize_grip_active", ImGuiCol_ResizeGripActive},
      {"input_text_cursor", ImGuiCol_InputTextCursor},
      {"tab", ImGuiCol_Tab},
      {"tab_hovered", ImGuiCol_TabHovered},
      {"tab_selected", ImGuiCol_TabSelected},
      {"tab_selected_overline", ImGuiCol_TabSelectedOverline},
      {"tab_dimmed", ImGuiCol_TabDimmed},
      {"tab_dimmed_selected", ImGuiCol_TabDimmedSelected},
      {"tab_dimmed_selected_overline", ImGuiCol_TabDimmedSelectedOverline},
      {"plot_lines", ImGuiCol_PlotLines},
      {"plot_lines_hovered", ImGuiCol_PlotLinesHovered},
      {"plot_histogram", ImGuiCol_PlotHistogram},
      {"plot_histogram_hovered", ImGuiCol_PlotHistogramHovered},
      {"table_header_bg", ImGuiCol_TableHeaderBg},
      {"table_border_strong", ImGuiCol_TableBorderStrong},
      {"table_border_light", ImGuiCol_TableBorderLight},
      {"table_row_bg", ImGuiCol_TableRowBg},
      {"table_row_bg_alt", ImGuiCol_TableRowBgAlt},
      {"text_link", ImGuiCol_TextLink},
      {"text_selected_bg", ImGuiCol_TextSelectedBg},
      {"tree_lines", ImGuiCol_TreeLines},
      {"drag_drop_target", ImGuiCol_DragDropTarget},
      {"drag_drop_target_bg", ImGuiCol_DragDropTargetBg},
      {"unsaved_marker", ImGuiCol_UnsavedMarker},
      {"nav_cursor", ImGuiCol_NavCursor},
      {"nav_windowing_highlight", ImGuiCol_NavWindowingHighlight},
      {"nav_windowing_dim_bg", ImGuiCol_NavWindowingDimBg},
      {"modal_window_dim_bg", ImGuiCol_ModalWindowDimBg},
  };

  for (const auto &mapping : mappings) {
    if (strcmp(name, mapping.name) == 0) {
      return mapping.index;
    }
  }
  return -1;
}

// Helper to read a color (table with r,g,b,a or [1],[2],[3],[4]) from Lua stack
static bool read_color_from_table(lua_State *L, int table_idx, ImVec4 &color) {
  auto lua = g_api->lua;

  // Try named fields first (r, g, b, a)
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
    color.w = lua->isnil(L, -1) ? 1.0f : static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    return true;
  }
  lua->pop(L, 1);

  // Try array-style [1], [2], [3], [4]
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
    color.w = lua->isnil(L, -1) ? 1.0f : static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    return true;
  }
  lua->pop(L, 1);

  return false;
}

// Helper to read an ImVec2 from table (x,y or [1],[2])
static bool read_vec2_from_table(lua_State *L, int table_idx, ImVec2 &vec) {
  auto lua = g_api->lua;

  // Try named fields first (x, y)
  lua->getfield(L, table_idx, "x");
  if (!lua->isnil(L, -1)) {
    vec.x = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->getfield(L, table_idx, "y");
    vec.y = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    return true;
  }
  lua->pop(L, 1);

  // Try array-style [1], [2]
  lua->rawgeti(L, table_idx, 1);
  if (!lua->isnil(L, -1)) {
    vec.x = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->rawgeti(L, table_idx, 2);
    vec.y = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    return true;
  }
  lua->pop(L, 1);

  return false;
}

// Child Window
static int begin_child(lua_State *L) {
  auto lua = g_api->lua;
  const char *id = lua->tolstring(L, 1, nullptr);
  float w = 0, h = 0;
  int child_flags = 0;
  int window_flags = 0;

  int nargs = lua->gettop(L);
  if (nargs >= 2)
    w = static_cast<float>(lua->tonumber(L, 2));
  if (nargs >= 3)
    h = static_cast<float>(lua->tonumber(L, 3));
  if (nargs >= 4)
    child_flags = static_cast<int>(lua->tonumber(L, 4));
  if (nargs >= 5)
    window_flags = static_cast<int>(lua->tonumber(L, 5));
  lua->pop(L, nargs);

  if (!id || id[0] == '\0') {
    lua->pushboolean(L, false);
    return 1;
  }

  bool visible = ImGui::BeginChild(id, ImVec2(w, h), child_flags, window_flags);
  lua->pushboolean(L, visible);
  return 1;
}

static int end_child(lua_State *L) {
  (void)L;
  ImGui::EndChild();
  return 0;
}

// Scrolling
static int get_scroll_x(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushnumber(L, ImGui::GetScrollX());
  return 1;
}

static int get_scroll_y(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushnumber(L, ImGui::GetScrollY());
  return 1;
}

static int set_scroll_x(lua_State *L) {
  auto lua = g_api->lua;
  float scroll_x = static_cast<float>(lua->tonumber(L, 1));
  lua->pop(L, 1);
  ImGui::SetScrollX(scroll_x);
  return 0;
}

static int set_scroll_y(lua_State *L) {
  auto lua = g_api->lua;
  float scroll_y = static_cast<float>(lua->tonumber(L, 1));
  lua->pop(L, 1);
  ImGui::SetScrollY(scroll_y);
  return 0;
}

static int get_scroll_max_x(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushnumber(L, ImGui::GetScrollMaxX());
  return 1;
}

static int get_scroll_max_y(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushnumber(L, ImGui::GetScrollMaxY());
  return 1;
}

static int get_content_region_avail(lua_State *L) {
  auto lua = g_api->lua;
  ImVec2 v = ImGui::GetContentRegionAvail();
  lua->pushnumber(L, v.x);
  lua->pushnumber(L, v.y);
  return 2;
}

static int get_content_region_max(lua_State *L) {
  auto lua = g_api->lua;
  ImVec2 v = ImGui::GetContentRegionMax();
  lua->pushnumber(L, v.x);
  lua->pushnumber(L, v.y);
  return 2;
}

static int get_window_size(lua_State *L) {
  auto lua = g_api->lua;
  ImVec2 v = ImGui::GetWindowSize();
  lua->pushnumber(L, v.x);
  lua->pushnumber(L, v.y);
  return 2;
}

// Size of the whole render target, i.e. the host window's client area.
static int get_display_size(lua_State *L) {
  auto lua = g_api->lua;
  ImVec2 v = ImGui::GetIO().DisplaySize;
  lua->pushnumber(L, v.x);
  lua->pushnumber(L, v.y);
  return 2;
}

// Full resolution of the monitor the host window sits on, taskbar included.
static int get_monitor_size(lua_State *L) {
  auto lua = g_api->lua;
  HWND hwnd = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
  HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi = {};
  mi.cbSize = sizeof(mi);
  if (mon && GetMonitorInfo(mon, &mi)) {
    lua->pushnumber(L, static_cast<double>(mi.rcMonitor.right - mi.rcMonitor.left));
    lua->pushnumber(L, static_cast<double>(mi.rcMonitor.bottom - mi.rcMonitor.top));
  } else {
    lua->pushnumber(L, GetSystemMetrics(SM_CXSCREEN));
    lua->pushnumber(L, GetSystemMetrics(SM_CYSCREEN));
  }
  return 2;
}

// Same monitor, minus the taskbar and other appbars.
static int get_monitor_work_size(lua_State *L) {
  auto lua = g_api->lua;
  HWND hwnd = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
  HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi = {};
  mi.cbSize = sizeof(mi);
  if (mon && GetMonitorInfo(mon, &mi)) {
    lua->pushnumber(L, static_cast<double>(mi.rcWork.right - mi.rcWork.left));
    lua->pushnumber(L, static_cast<double>(mi.rcWork.bottom - mi.rcWork.top));
  } else {
    lua->pushnumber(L, GetSystemMetrics(SM_CXSCREEN));
    lua->pushnumber(L, GetSystemMetrics(SM_CYSCREEN));
  }
  return 2;
}

static int get_window_pos(lua_State *L) {
  auto lua = g_api->lua;
  ImVec2 v = ImGui::GetWindowPos();
  lua->pushnumber(L, v.x);
  lua->pushnumber(L, v.y);
  return 2;
}

static int get_cursor_pos(lua_State *L) {
  auto lua = g_api->lua;
  ImVec2 v = ImGui::GetCursorPos();
  lua->pushnumber(L, v.x);
  lua->pushnumber(L, v.y);
  return 2;
}

static int set_cursor_pos(lua_State *L) {
  auto lua = g_api->lua;
  float x = static_cast<float>(lua->tonumber(L, 1));
  float y = static_cast<float>(lua->tonumber(L, 2));
  lua->pop(L, 2);
  ImGui::SetCursorPos(ImVec2(x, y));
  return 0;
}

static int set_scroll_here_x(lua_State *L) {
  auto lua = g_api->lua;
  float center_x_ratio = 0.5f;
  int nargs = lua->gettop(L);
  if (nargs >= 1)
    center_x_ratio = static_cast<float>(lua->tonumber(L, 1));
  lua->pop(L, nargs);
  ImGui::SetScrollHereX(center_x_ratio);
  return 0;
}

static int set_scroll_here_y(lua_State *L) {
  auto lua = g_api->lua;
  float center_y_ratio = 0.5f;
  int nargs = lua->gettop(L);
  if (nargs >= 1)
    center_y_ratio = static_cast<float>(lua->tonumber(L, 1));
  lua->pop(L, nargs);
  ImGui::SetScrollHereY(center_y_ratio);
  return 0;
}

// Window
static int begin_window(lua_State *L) {
  auto lua = g_api->lua;
  const char *name = lua->tolstring(L, 1, nullptr);
  bool open = true;
  bool has_close_button = true;
  int flags = 0;

  int nargs = lua->gettop(L);
  if (nargs >= 2 && !lua->isnil(L, 2)) {
    open = lua->toboolean(L, 2);
  } else if (nargs >= 2 && lua->isnil(L, 2)) {
    has_close_button = false;
  }
  if (nargs >= 3) {
    flags = static_cast<int>(lua->tonumber(L, 3));
  }
  lua->pop(L, nargs);

  if (!name || name[0] == '\0') {
    lua->pushboolean(L, false);
    lua->pushboolean(L, false);
    return 2;
  }

  bool visible = ImGui::Begin(name, has_close_button ? &open : nullptr, flags);
  lua->pushboolean(L, visible);
  lua->pushboolean(L, open);
  return 2;
}

static int end_window(lua_State *L) {
  ImGui::End();
  return 0;
}

// Text
static int text(lua_State *L) {
  auto lua = g_api->lua;
  const char *str = lua->tolstring(L, 1, nullptr);
  lua->pop(L, 1);
  ImGui::Text("%s", str);
  return 0;
}

static int text_colored(lua_State *L) {
  auto lua = g_api->lua;
  float r = static_cast<float>(lua->tonumber(L, 1));
  float g = static_cast<float>(lua->tonumber(L, 2));
  float b = static_cast<float>(lua->tonumber(L, 3));
  float a = static_cast<float>(lua->tonumber(L, 4));
  const char *str = lua->tolstring(L, 5, nullptr);
  lua->pop(L, 5);
  ImGui::TextColored(ImVec4(r, g, b, a), "%s", str);
  return 0;
}

static int text_wrapped(lua_State *L) {
  auto lua = g_api->lua;
  const char *str = lua->tolstring(L, 1, nullptr);
  lua->pop(L, 1);
  ImGui::TextWrapped("%s", str);
  return 0;
}

static int push_color_style(lua_State *L) {
  auto lua = g_api->lua;

  int color_index = -1;
  if (lua->type(L, 1) == LUA_TNUMBER) {
    color_index = static_cast<int>(lua->tonumber(L, 1));
  } else {
    const char *name = lua->tolstring(L, 1, nullptr);
    color_index = get_color_index(name);
  }

  ImVec4 color(1.0f, 1.0f, 1.0f, 1.0f);
  if (lua->type(L, 2) == LUA_TTABLE) {
    read_color_from_table(L, 2, color);
  }

  lua->pop(L, 2);

  if (color_index >= 0) {
    ImGui::PushStyleColor(color_index, color);
  }

  return 0;
}

static int pop_color_style(lua_State *L) {
  auto lua = g_api->lua;
  int count = 1;

  int nargs = lua->gettop(L);
  if (nargs >= 1) {
    count = static_cast<int>(lua->tonumber(L, 1));
  }
  lua->pop(L, nargs);

  ImGui::PopStyleColor(count);
  return 0;
}

// Buttons
static int button(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  float w = 0, h = 0;
  int nargs = lua->gettop(L);
  if (nargs >= 2)
    w = static_cast<float>(lua->tonumber(L, 2));
  if (nargs >= 3)
    h = static_cast<float>(lua->tonumber(L, 3));
  lua->pop(L, nargs);
  lua->pushboolean(L, ImGui::Button(label, ImVec2(w, h)));
  return 1;
}

static int small_button(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  lua->pop(L, 1);
  lua->pushboolean(L, ImGui::SmallButton(label));
  return 1;
}

static int checkbox(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  bool value = lua->toboolean(L, 2);
  lua->pop(L, 2);
  bool changed = ImGui::Checkbox(label, &value);
  lua->pushboolean(L, changed);
  lua->pushboolean(L, value);
  return 2;
}

// Input
static int input_text(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  const char *current = lua->tolstring(L, 2, nullptr);
  int max_size = 256;

  int nargs = lua->gettop(L);
  if (nargs >= 3)
    max_size = static_cast<int>(lua->tonumber(L, 3));
  lua->pop(L, nargs);

  // Clamp to reasonable bounds
  if (max_size < 1)
    max_size = 1;
  if (max_size > 16384)
    max_size = 16384;

  static thread_local std::vector<char> buf;
  buf.resize(max_size);
  strncpy_s(buf.data(), buf.size(), current ? current : "", buf.size() - 1);

  bool changed = ImGui::InputText(label, buf.data(), buf.size());
  lua->pushboolean(L, changed);
  lua->pushstring(L, buf.data());
  return 2;
}

static int input_text_multiline(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  const char *current = lua->tolstring(L, 2, nullptr);
  int max_size = 4096;
  float width = 0;
  float height = 0;
  int flags = 0;

  int nargs = lua->gettop(L);
  if (nargs >= 3)
    max_size = static_cast<int>(lua->tonumber(L, 3));
  if (nargs >= 4)
    width = static_cast<float>(lua->tonumber(L, 4));
  if (nargs >= 5)
    height = static_cast<float>(lua->tonumber(L, 5));
  if (nargs >= 6)
    flags = static_cast<int>(lua->tonumber(L, 6));
  lua->pop(L, nargs);

  // Clamp to reasonable bounds
  if (max_size < 1)
    max_size = 1;
  if (max_size > 1048576)
    max_size = 1048576; // 1MB max for multiline

  static thread_local std::vector<char> buf;
  buf.resize(max_size);
  strncpy_s(buf.data(), buf.size(), current ? current : "", buf.size() - 1);

  bool changed = ImGui::InputTextMultiline(label, buf.data(), buf.size(),
                                           ImVec2(width, height), flags);
  lua->pushboolean(L, changed);
  lua->pushstring(L, buf.data());
  return 2;
}

static int input_float(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  float value = static_cast<float>(lua->tonumber(L, 2));
  lua->pop(L, 2);
  bool changed = ImGui::InputFloat(label, &value);
  lua->pushboolean(L, changed);
  lua->pushnumber(L, value);
  return 2;
}

static int input_int(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  int value = static_cast<int>(lua->tonumber(L, 2));
  lua->pop(L, 2);
  bool changed = ImGui::InputInt(label, &value);
  lua->pushboolean(L, changed);
  lua->pushnumber(L, value);
  return 2;
}

// Sliders
static int slider_float(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  float value = static_cast<float>(lua->tonumber(L, 2));
  float min = static_cast<float>(lua->tonumber(L, 3));
  float max = static_cast<float>(lua->tonumber(L, 4));
  lua->pop(L, 4);
  bool changed = ImGui::SliderFloat(label, &value, min, max);
  lua->pushboolean(L, changed);
  lua->pushnumber(L, value);
  return 2;
}

static int slider_int(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  int value = static_cast<int>(lua->tonumber(L, 2));
  int min = static_cast<int>(lua->tonumber(L, 3));
  int max = static_cast<int>(lua->tonumber(L, 4));
  lua->pop(L, 4);
  bool changed = ImGui::SliderInt(label, &value, min, max);
  lua->pushboolean(L, changed);
  lua->pushnumber(L, value);
  return 2;
}

// Layout
static int same_line(lua_State *L) {
  auto lua = g_api->lua;
  float offset = 0, spacing = -1;
  int nargs = lua->gettop(L);
  if (nargs >= 1)
    offset = static_cast<float>(lua->tonumber(L, 1));
  if (nargs >= 2)
    spacing = static_cast<float>(lua->tonumber(L, 2));
  lua->pop(L, nargs);
  ImGui::SameLine(offset, spacing);
  return 0;
}

static int separator(lua_State *L) {
  ImGui::Separator();
  return 0;
}

static int spacing(lua_State *L) {
  ImGui::Spacing();
  return 0;
}

static int new_line(lua_State *L) {
  ImGui::NewLine();
  return 0;
}

static int indent(lua_State *L) {
  auto lua = g_api->lua;
  float indent_w = 0.0f;
  int nargs = lua->gettop(L);
  if (nargs >= 1)
    indent_w = static_cast<float>(lua->tonumber(L, 1));
  lua->pop(L, nargs);
  ImGui::Indent(indent_w);
  return 0;
}

static int set_next_item_width(lua_State *L) {
  auto lua = g_api->lua;
  float width = static_cast<float>(lua->tonumber(L, 1));
  lua->pop(L, 1);
  ImGui::SetNextItemWidth(width);
  return 0;
}

static int set_next_window_size(lua_State *L) {
  auto lua = g_api->lua;
  float width = static_cast<float>(lua->tonumber(L, 1));
  float height = static_cast<float>(lua->tonumber(L, 2));
  ImGuiCond cond = lua->gettop(L) >= 3 ? static_cast<ImGuiCond>(lua->tonumber(L, 3)) : 0;
  lua->pop(L, lua->gettop(L));
  ImGui::SetNextWindowSize(ImVec2(width, height), cond);
  return 0;
}

static int set_next_window_pos(lua_State *L) {
  auto lua = g_api->lua;
  float x = static_cast<float>(lua->tonumber(L, 1));
  float y = static_cast<float>(lua->tonumber(L, 2));
  ImGuiCond cond = lua->gettop(L) >= 3 ? static_cast<ImGuiCond>(lua->tonumber(L, 3)) : 0;
  lua->pop(L, lua->gettop(L));
  ImGui::SetNextWindowPos(ImVec2(x, y), cond);
  return 0;
}

static int set_next_window_focus(lua_State *L) {
  ImGui::SetNextWindowFocus();
  return 0;
}

static int set_keyboard_focus_here(lua_State *L) {
  auto lua = g_api->lua;
  int offset = 0;
  int nargs = lua->gettop(L);
  if (nargs >= 1)
    offset = static_cast<int>(lua->tonumber(L, 1));
  lua->pop(L, nargs);
  ImGui::SetKeyboardFocusHere(offset);
  return 0;
}

static int bring_current_window_to_front(lua_State *L) {
  ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
  return 0;
}

static int push_id(lua_State *L) {
  auto lua = g_api->lua;
  int nargs = lua->gettop(L);
  if (nargs >= 1) {
    int type = lua->type(L, 1);
    if (type == 3) {
      // LUA_TNUMBER
      int id = static_cast<int>(lua->tonumber(L, 1));
      ImGui::PushID(id);
    } else {
      const char *str_id = lua->tolstring(L, 1, nullptr);
      ImGui::PushID(str_id);
    }
  }
  lua->pop(L, nargs);
  return 0;
}

static int pop_id(lua_State *L) {
  (void)L;
  ImGui::PopID();
  return 0;
}

static int unindent(lua_State *L) {
  auto lua = g_api->lua;
  float indent_w = 0.0f;
  int nargs = lua->gettop(L);
  if (nargs >= 1)
    indent_w = static_cast<float>(lua->tonumber(L, 1));
  lua->pop(L, nargs);
  ImGui::Unindent(indent_w);
  return 0;
}

// Collapsing/Tree
static int collapsing_header(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  lua->pop(L, 1);
  lua->pushboolean(L, ImGui::CollapsingHeader(label));
  return 1;
}

static int tree_node(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  lua->pop(L, 1);
  lua->pushboolean(L, ImGui::TreeNode(label));
  return 1;
}

static int tree_pop(lua_State *L) {
  ImGui::TreePop();
  return 0;
}

// Combo
static int begin_combo(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  const char *preview = lua->tolstring(L, 2, nullptr);
  lua->pop(L, 2);
  lua->pushboolean(L, ImGui::BeginCombo(label, preview));
  return 1;
}

static int end_combo(lua_State *L) {
  ImGui::EndCombo();
  return 0;
}

static int selectable(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  bool selected = false;
  int nargs = lua->gettop(L);
  if (nargs >= 2)
    selected = lua->toboolean(L, 2);
  lua->pop(L, nargs);
  lua->pushboolean(L, ImGui::Selectable(label, selected));
  return 1;
}

// Color
static int color_edit4(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  float r = static_cast<float>(lua->tonumber(L, 2));
  float g = static_cast<float>(lua->tonumber(L, 3));
  float b = static_cast<float>(lua->tonumber(L, 4));
  float a = static_cast<float>(lua->tonumber(L, 5));
  lua->pop(L, 5);

  float col[4] = {r, g, b, a};
  bool changed = ImGui::ColorEdit4(label, col);

  lua->pushboolean(L, changed);
  lua->pushnumber(L, col[0]);
  lua->pushnumber(L, col[1]);
  lua->pushnumber(L, col[2]);
  lua->pushnumber(L, col[3]);
  return 5;
}

static int color_picker4(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  float r = static_cast<float>(lua->tonumber(L, 2));
  float g = static_cast<float>(lua->tonumber(L, 3));
  float b = static_cast<float>(lua->tonumber(L, 4));
  float a = static_cast<float>(lua->tonumber(L, 5));
  lua->pop(L, 5);

  float col[4] = {r, g, b, a};
  bool changed = ImGui::ColorPicker4(label, col);

  lua->pushboolean(L, changed);
  lua->pushnumber(L, col[0]);
  lua->pushnumber(L, col[1]);
  lua->pushnumber(L, col[2]);
  lua->pushnumber(L, col[3]);
  return 5;
}

// Tooltips
static int set_tooltip(lua_State *L) {
  auto lua = g_api->lua;
  const char *text = lua->tolstring(L, 1, nullptr);
  lua->pop(L, 1);
  ImGui::SetTooltip("%s", text);
  return 0;
}

static int begin_tooltip(lua_State *L) {
  lua_State *Ls = L;
  (void)Ls;
  ImGui::BeginTooltip();
  return 0;
}

static int end_tooltip(lua_State *L) {
  lua_State *Ls = L;
  (void)Ls;
  ImGui::EndTooltip();
  return 0;
}

static int is_item_hovered(lua_State *L) {
  auto lua = g_api->lua;
  int flags = static_cast<int>(lua->tonumber(L, 1)); // 0 if not provided, which is fine.
  lua->pushboolean(L, ImGui::IsItemHovered(flags));
  return 1;
}

static int is_item_clicked(lua_State *L) {
  auto lua = g_api->lua;
  int flags = static_cast<int>(lua->tonumber(L, 1)); // 0 if not provided, which is fine.
  lua->pushboolean(L, ImGui::IsItemClicked(flags));
  return 1;
}

// Tabs
static int begin_tab_bar(lua_State *L) {
  auto lua = g_api->lua;
  const char *id = lua->tolstring(L, 1, nullptr);
  lua->pop(L, 1);
  lua->pushboolean(L, ImGui::BeginTabBar(id));
  return 1;
}

static int end_tab_bar(lua_State *L) {
  lua_State *Ls = L;
  (void)Ls;
  ImGui::EndTabBar();
  return 0;
}

static int begin_tab_item(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  lua->pop(L, 1);
  lua->pushboolean(L, ImGui::BeginTabItem(label));
  return 1;
}

static int end_tab_item(lua_State *L) {
  lua_State *Ls = L;
  (void)Ls;
  ImGui::EndTabItem();
  return 0;
}

// Progress
static int progress_bar(lua_State *L) {
  auto lua = g_api->lua;
  float fraction = static_cast<float>(lua->tonumber(L, 1));
  int nargs = lua->gettop(L);

  float w = -1, h = 0;
  const char *overlay = nullptr;

  if (nargs >= 2)
    w = static_cast<float>(lua->tonumber(L, 2));
  if (nargs >= 3)
    h = static_cast<float>(lua->tonumber(L, 3));
  if (nargs >= 4)
    overlay = lua->tolstring(L, 4, nullptr);

  lua->pop(L, nargs);
  ImGui::ProgressBar(fraction, ImVec2(w, h), overlay);
  return 0;
}

// Drag
static int drag_float(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  float value = static_cast<float>(lua->tonumber(L, 2));
  float speed = 1.0f, min = 0.0f, max = 0.0f;

  int nargs = lua->gettop(L);
  if (nargs >= 3)
    speed = static_cast<float>(lua->tonumber(L, 3));
  if (nargs >= 4)
    min = static_cast<float>(lua->tonumber(L, 4));
  if (nargs >= 5)
    max = static_cast<float>(lua->tonumber(L, 5));

  lua->pop(L, nargs);
  bool changed = ImGui::DragFloat(label, &value, speed, min, max);
  lua->pushboolean(L, changed);
  lua->pushnumber(L, value);
  return 2;
}

static int drag_int(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);
  int value = static_cast<int>(lua->tonumber(L, 2));
  float speed = 1.0f;
  int min = 0, max = 0;

  int nargs = lua->gettop(L);
  if (nargs >= 3)
    speed = static_cast<float>(lua->tonumber(L, 3));
  if (nargs >= 4)
    min = static_cast<int>(lua->tonumber(L, 4));
  if (nargs >= 5)
    max = static_cast<int>(lua->tonumber(L, 5));

  lua->pop(L, nargs);
  bool changed = ImGui::DragInt(label, &value, speed, min, max);
  lua->pushboolean(L, changed);
  lua->pushnumber(L, value);
  return 2;
}

// Popups/Modals
static int open_popup(lua_State *L) {
  auto lua = g_api->lua;
  const char *id = lua->tolstring(L, 1, nullptr);
  lua->pop(L, 1);
  ImGui::OpenPopup(id);
  return 0;
}

static int begin_popup(lua_State *L) {
  auto lua = g_api->lua;
  const char *id = lua->tolstring(L, 1, nullptr);
  lua->pop(L, 1);
  lua->pushboolean(L, ImGui::BeginPopup(id));
  return 1;
}

static int begin_popup_modal(lua_State *L) {
  auto lua = g_api->lua;
  const char *name = lua->tolstring(L, 1, nullptr);
  bool open = true;

  int nargs = lua->gettop(L);
  if (nargs >= 2 && !lua->isnil(L, 2)) {
    open = lua->toboolean(L, 2);
  }
  lua->pop(L, nargs);

  bool visible = ImGui::BeginPopupModal(name, &open);
  lua->pushboolean(L, visible);
  lua->pushboolean(L, open);
  return 2;
}

static int end_popup(lua_State *L) {
  (void)L;
  ImGui::EndPopup();
  return 0;
}

static int close_current_popup(lua_State *L) {
  (void)L;
  ImGui::CloseCurrentPopup();
  return 0;
}

static int is_popup_open(lua_State *L) {
  auto lua = g_api->lua;
  const char *id = lua->tolstring(L, 1, nullptr);
  lua->pop(L, 1);
  lua->pushboolean(L, ImGui::IsPopupOpen(id));
  return 1;
}

// Plotting
static int plot_lines(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);

  int nargs = lua->gettop(L);

  // Get table length (arg 2 is the table)
  int count = static_cast<int>(lua->objlen(L, 2));

  // Build float array from table
  std::vector<float> values;
  values.reserve(count);

  for (int i = 1; i <= count; i++) {
    lua->rawgeti(L, 2, i);
    values.push_back(static_cast<float>(lua->tonumber(L, -1)));
    lua->pop(L, 1);
  }

  // Optional parameters
  const char *overlay_text = nullptr;
  float scale_min = FLT_MAX;
  float scale_max = FLT_MAX;
  float width = 0;
  float height = 0;

  if (nargs >= 3 && !lua->isnil(L, 3))
    overlay_text = lua->tolstring(L, 3, nullptr);
  if (nargs >= 4)
    scale_min = static_cast<float>(lua->tonumber(L, 4));
  if (nargs >= 5)
    scale_max = static_cast<float>(lua->tonumber(L, 5));
  if (nargs >= 6)
    width = static_cast<float>(lua->tonumber(L, 6));
  if (nargs >= 7)
    height = static_cast<float>(lua->tonumber(L, 7));

  lua->pop(L, nargs);

  ImGui::PlotLines(label, values.data(), count, 0, overlay_text, scale_min, scale_max,
                   ImVec2(width, height));

  return 0;
}

static int plot_histogram(lua_State *L) {
  auto lua = g_api->lua;
  const char *label = lua->tolstring(L, 1, nullptr);

  int nargs = lua->gettop(L);

  // Get table length (arg 2 is the table)
  int count = static_cast<int>(lua->objlen(L, 2));

  // Build float array from table
  std::vector<float> values;
  values.reserve(count);

  for (int i = 1; i <= count; i++) {
    lua->rawgeti(L, 2, i);
    values.push_back(static_cast<float>(lua->tonumber(L, -1)));
    lua->pop(L, 1);
  }

  // Optional parameters
  const char *overlay_text = nullptr;
  float scale_min = FLT_MAX;
  float scale_max = FLT_MAX;
  float width = 0;
  float height = 0;

  if (nargs >= 3 && !lua->isnil(L, 3))
    overlay_text = lua->tolstring(L, 3, nullptr);
  if (nargs >= 4)
    scale_min = static_cast<float>(lua->tonumber(L, 4));
  if (nargs >= 5)
    scale_max = static_cast<float>(lua->tonumber(L, 5));
  if (nargs >= 6)
    width = static_cast<float>(lua->tonumber(L, 6));
  if (nargs >= 7)
    height = static_cast<float>(lua->tonumber(L, 7));

  lua->pop(L, nargs);

  ImGui::PlotHistogram(label, values.data(), count, 0, overlay_text, scale_min, scale_max,
                       ImVec2(width, height));

  return 0;
}

// Frame control
static int new_frame(lua_State *L) {
  auto overlay = Overlay::get();
  if (overlay)
    overlay->new_frame();
  return 0;
}

static int render(lua_State *L) {
  auto overlay = Overlay::get();
  if (overlay)
    overlay->render();
  return 0;
}

// Visibility
static int set_visible(lua_State *L) {
  auto lua = g_api->lua;
  bool visible = lua->toboolean(L, 1);
  lua->pop(L, 1);
  auto overlay = Overlay::get();
  if (overlay)
    overlay->set_visible(visible);
  return 0;
}

static int is_visible(lua_State *L) {
  auto lua = g_api->lua;
  auto overlay = Overlay::get();
  lua->pushboolean(L, overlay && overlay->is_visible());
  return 1;
}

// Input capture queries
static int want_capture_mouse(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushboolean(L, ImGui::GetIO().WantCaptureMouse);
  return 1;
}

static int want_capture_keyboard(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushboolean(L, ImGui::GetIO().WantCaptureKeyboard);
  return 1;
}

// Fonts
static int load_font(lua_State *L) {
  auto lua = g_api->lua;
  const char *path = lua->tolstring(L, 1, nullptr);
  float size = 16.0f;

  int nargs = lua->gettop(L);
  if (nargs >= 2)
    size = static_cast<float>(lua->tonumber(L, 2));
  lua->pop(L, nargs);

  if (!path || path[0] == '\0') {
    lua->pushlightuserdata(L, nullptr);
    return 1;
  }

  ImGuiIO &io = ImGui::GetIO();
  ImFont *font = io.Fonts->AddFontFromFileTTF(path, size);

  if (font) {
    // Mark atlas as needing rebuild
    io.Fonts->Build();
    // Invalidate DX9 font texture so it gets recreated
    ImGui_ImplDX9_InvalidateDeviceObjects();
  }

  lua->pushlightuserdata(L, font);
  return 1;
}

static int push_font(lua_State *L) {
  auto lua = g_api->lua;
  void *font_ptr = lua->tolightuserdata(L, 1);
  lua->pop(L, 1);

  ImFont *font = static_cast<ImFont *>(font_ptr);
  if (font) {
    ImGui::PushFont(font);
  }
  return 0;
}

static int pop_font(lua_State *L) {
  (void)L;
  ImGui::PopFont();
  return 0;
}

static int set_default_font(lua_State *L) {
  auto lua = g_api->lua;
  void *font_ptr = lua->tolightuserdata(L, 1);
  lua->pop(L, 1);

  ImFont *font = static_cast<ImFont *>(font_ptr);
  if (font) {
    ImGuiIO &io = ImGui::GetIO();
    io.FontDefault = font;
  }
  return 0;
}

static int get_default_font(lua_State *L) {
  auto lua = g_api->lua;
  ImGuiIO &io = ImGui::GetIO();
  lua->pushlightuserdata(L, io.FontDefault ? io.FontDefault : io.Fonts->Fonts[0]);
  return 1;
}

// Style
static int set_style(lua_State *L) {
  auto lua = g_api->lua;
  ImGuiStyle &style = ImGui::GetStyle();

  // Argument 1 should be a table
  if (lua->type(L, 1) != 5) {
    // LUA_TTABLE = 5
    lua->pop(L, lua->gettop(L));
    return 0;
  }

  // Process colors subtable
  lua->getfield(L, 1, "colors");
  if (!lua->isnil(L, -1)) {
    // Iterate through the colors table using rawgeti won't work for string keys
    // We need to use lua_next style iteration, but LJE SDK doesn't expose that
    // Instead, we'll check for each known color name
    static const char *color_names[] = {
        "text",
        "text_disabled",
        "window_bg",
        "child_bg",
        "popup_bg",
        "border",
        "border_shadow",
        "frame_bg",
        "frame_bg_hovered",
        "frame_bg_active",
        "title_bg",
        "title_bg_active",
        "title_bg_collapsed",
        "menu_bar_bg",
        "scrollbar_bg",
        "scrollbar_grab",
        "scrollbar_grab_hovered",
        "scrollbar_grab_active",
        "check_mark",
        "slider_grab",
        "slider_grab_active",
        "button",
        "button_hovered",
        "button_active",
        "header",
        "header_hovered",
        "header_active",
        "separator",
        "separator_hovered",
        "separator_active",
        "resize_grip",
        "resize_grip_hovered",
        "resize_grip_active",
        "input_text_cursor",
        "tab",
        "tab_hovered",
        "tab_selected",
        "tab_selected_overline",
        "tab_dimmed",
        "tab_dimmed_selected",
        "tab_dimmed_selected_overline",
        "plot_lines",
        "plot_lines_hovered",
        "plot_histogram",
        "plot_histogram_hovered",
        "table_header_bg",
        "table_border_strong",
        "table_border_light",
        "table_row_bg",
        "table_row_bg_alt",
        "text_link",
        "text_selected_bg",
        "tree_lines",
        "drag_drop_target",
        "drag_drop_target_bg",
        "unsaved_marker",
        "nav_cursor",
        "nav_windowing_highlight",
        "nav_windowing_dim_bg",
        "modal_window_dim_bg",
        nullptr};

    for (int i = 0; color_names[i] != nullptr; i++) {
      lua->getfield(L, -1, color_names[i]);
      if (!lua->isnil(L, -1)) {
        ImVec4 color;
        if (read_color_from_table(L, lua->gettop(L), color)) {
          int idx = get_color_index(color_names[i]);
          if (idx >= 0) {
            style.Colors[idx] = color;
          }
        }
      }
      lua->pop(L, 1);
    }
  }
  lua->pop(L, 1); // pop colors table

  // Process rounding subtable
  lua->getfield(L, 1, "rounding");
  if (!lua->isnil(L, -1)) {
    lua->getfield(L, -1, "window");
    if (!lua->isnil(L, -1))
      style.WindowRounding = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);

    lua->getfield(L, -1, "child");
    if (!lua->isnil(L, -1))
      style.ChildRounding = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);

    lua->getfield(L, -1, "popup");
    if (!lua->isnil(L, -1))
      style.PopupRounding = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);

    lua->getfield(L, -1, "frame");
    if (!lua->isnil(L, -1))
      style.FrameRounding = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);

    lua->getfield(L, -1, "scrollbar");
    if (!lua->isnil(L, -1))
      style.ScrollbarRounding = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);

    lua->getfield(L, -1, "grab");
    if (!lua->isnil(L, -1))
      style.GrabRounding = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);

    lua->getfield(L, -1, "tab");
    if (!lua->isnil(L, -1))
      style.TabRounding = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
  }
  lua->pop(L, 1); // pop rounding table

  // Process border subtable. 0 removes a border, 1 is the usual thickness.
  lua->getfield(L, 1, "border");
  if (!lua->isnil(L, -1)) {
    lua->getfield(L, -1, "window");
    if (!lua->isnil(L, -1))
      style.WindowBorderSize = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);

    lua->getfield(L, -1, "child");
    if (!lua->isnil(L, -1))
      style.ChildBorderSize = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);

    lua->getfield(L, -1, "popup");
    if (!lua->isnil(L, -1))
      style.PopupBorderSize = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);

    lua->getfield(L, -1, "frame");
    if (!lua->isnil(L, -1))
      style.FrameBorderSize = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);

    lua->getfield(L, -1, "tab");
    if (!lua->isnil(L, -1))
      style.TabBorderSize = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);

    lua->getfield(L, -1, "tab_bar");
    if (!lua->isnil(L, -1))
      style.TabBarBorderSize = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
  }
  lua->pop(L, 1); // pop border table

  // Process padding subtable
  lua->getfield(L, 1, "padding");
  if (!lua->isnil(L, -1)) {
    lua->getfield(L, -1, "window");
    if (!lua->isnil(L, -1)) {
      ImVec2 vec;
      if (read_vec2_from_table(L, lua->gettop(L), vec))
        style.WindowPadding = vec;
    }
    lua->pop(L, 1);

    lua->getfield(L, -1, "frame");
    if (!lua->isnil(L, -1)) {
      ImVec2 vec;
      if (read_vec2_from_table(L, lua->gettop(L), vec))
        style.FramePadding = vec;
    }
    lua->pop(L, 1);

    lua->getfield(L, -1, "cell");
    if (!lua->isnil(L, -1)) {
      ImVec2 vec;
      if (read_vec2_from_table(L, lua->gettop(L), vec))
        style.CellPadding = vec;
    }
    lua->pop(L, 1);

    lua->getfield(L, -1, "item_spacing");
    if (!lua->isnil(L, -1)) {
      ImVec2 vec;
      if (read_vec2_from_table(L, lua->gettop(L), vec))
        style.ItemSpacing = vec;
    }
    lua->pop(L, 1);

    lua->getfield(L, -1, "item_inner_spacing");
    if (!lua->isnil(L, -1)) {
      ImVec2 vec;
      if (read_vec2_from_table(L, lua->gettop(L), vec))
        style.ItemInnerSpacing = vec;
    }
    lua->pop(L, 1);

    lua->getfield(L, -1, "touch_extra");
    if (!lua->isnil(L, -1)) {
      ImVec2 vec;
      if (read_vec2_from_table(L, lua->gettop(L), vec))
        style.TouchExtraPadding = vec;
    }
    lua->pop(L, 1);

    lua->getfield(L, -1, "scrollbar");
    if (!lua->isnil(L, -1))
      style.ScrollbarPadding = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
  }
  lua->pop(L, 1); // pop padding table

  lua->pop(L, 1); // pop argument table
  return 0;
}

static int set_overlay_bind(lua_State *L) {
  auto lua = g_api->lua;
  const auto overlay = Overlay::get();
  if (!overlay) {
    return 0;
  }

  const auto vkKeyCode = static_cast<int>(lua->tonumber(L, 1));
  overlay->set_toggle_bind(vkKeyCode);

  return 0;
}

void register_all(lua_State *L) {
  auto lua = g_api->lua;

  lua->pushljeenv(L);

  // Create imgui table
  lua->createtable(L, 0, 0);

  // Window
  lua->pushcclosure(L, begin_window, 0);
  lua->setfield(L, -2, "begin_window");
  lua->pushcclosure(L, end_window, 0);
  lua->setfield(L, -2, "end_window");

  // Child Window
  lua->pushcclosure(L, begin_child, 0);
  lua->setfield(L, -2, "begin_child");
  lua->pushcclosure(L, end_child, 0);
  lua->setfield(L, -2, "end_child");

  // Child Flags
  lua->pushnumber(L, ImGuiChildFlags_None);
  lua->setfield(L, -2, "ChildFlags_None");
  lua->pushnumber(L, ImGuiChildFlags_Borders);
  lua->setfield(L, -2, "ChildFlags_Borders");
  lua->pushnumber(L, ImGuiChildFlags_AlwaysUseWindowPadding);
  lua->setfield(L, -2, "ChildFlags_AlwaysUseWindowPadding");
  lua->pushnumber(L, ImGuiChildFlags_ResizeX);
  lua->setfield(L, -2, "ChildFlags_ResizeX");
  lua->pushnumber(L, ImGuiChildFlags_ResizeY);
  lua->setfield(L, -2, "ChildFlags_ResizeY");
  lua->pushnumber(L, ImGuiChildFlags_AutoResizeX);
  lua->setfield(L, -2, "ChildFlags_AutoResizeX");
  lua->pushnumber(L, ImGuiChildFlags_AutoResizeY);
  lua->setfield(L, -2, "ChildFlags_AutoResizeY");
  lua->pushnumber(L, ImGuiChildFlags_AlwaysAutoResize);
  lua->setfield(L, -2, "ChildFlags_AlwaysAutoResize");
  lua->pushnumber(L, ImGuiChildFlags_FrameStyle);
  lua->setfield(L, -2, "ChildFlags_FrameStyle");
  lua->pushnumber(L, ImGuiChildFlags_NavFlattened);
  lua->setfield(L, -2, "ChildFlags_NavFlattened");

  // Window Flags
  lua->pushnumber(L, ImGuiWindowFlags_None);
  lua->setfield(L, -2, "WindowFlags_None");
  lua->pushnumber(L, ImGuiWindowFlags_NoTitleBar);
  lua->setfield(L, -2, "WindowFlags_NoTitleBar");
  lua->pushnumber(L, ImGuiWindowFlags_NoResize);
  lua->setfield(L, -2, "WindowFlags_NoResize");
  lua->pushnumber(L, ImGuiWindowFlags_NoMove);
  lua->setfield(L, -2, "WindowFlags_NoMove");
  lua->pushnumber(L, ImGuiWindowFlags_NoScrollbar);
  lua->setfield(L, -2, "WindowFlags_NoScrollbar");
  lua->pushnumber(L, ImGuiWindowFlags_NoScrollWithMouse);
  lua->setfield(L, -2, "WindowFlags_NoScrollWithMouse");
  lua->pushnumber(L, ImGuiWindowFlags_NoCollapse);
  lua->setfield(L, -2, "WindowFlags_NoCollapse");
  lua->pushnumber(L, ImGuiWindowFlags_AlwaysAutoResize);
  lua->setfield(L, -2, "WindowFlags_AlwaysAutoResize");
  lua->pushnumber(L, ImGuiWindowFlags_NoBackground);
  lua->setfield(L, -2, "WindowFlags_NoBackground");
  lua->pushnumber(L, ImGuiWindowFlags_NoSavedSettings);
  lua->setfield(L, -2, "WindowFlags_NoSavedSettings");
  lua->pushnumber(L, ImGuiWindowFlags_NoMouseInputs);
  lua->setfield(L, -2, "WindowFlags_NoMouseInputs");
  lua->pushnumber(L, ImGuiWindowFlags_MenuBar);
  lua->setfield(L, -2, "WindowFlags_MenuBar");
  lua->pushnumber(L, ImGuiWindowFlags_HorizontalScrollbar);
  lua->setfield(L, -2, "WindowFlags_HorizontalScrollbar");
  lua->pushnumber(L, ImGuiWindowFlags_NoFocusOnAppearing);
  lua->setfield(L, -2, "WindowFlags_NoFocusOnAppearing");
  lua->pushnumber(L, ImGuiWindowFlags_NoBringToFrontOnFocus);
  lua->setfield(L, -2, "WindowFlags_NoBringToFrontOnFocus");
  lua->pushnumber(L, ImGuiWindowFlags_AlwaysVerticalScrollbar);
  lua->setfield(L, -2, "WindowFlags_AlwaysVerticalScrollbar");
  lua->pushnumber(L, ImGuiWindowFlags_AlwaysHorizontalScrollbar);
  lua->setfield(L, -2, "WindowFlags_AlwaysHorizontalScrollbar");
  lua->pushnumber(L, ImGuiWindowFlags_NoNavInputs);
  lua->setfield(L, -2, "WindowFlags_NoNavInputs");
  lua->pushnumber(L, ImGuiWindowFlags_NoNavFocus);
  lua->setfield(L, -2, "WindowFlags_NoNavFocus");
  lua->pushnumber(L, ImGuiWindowFlags_NoNav);
  lua->setfield(L, -2, "WindowFlags_NoNav");
  lua->pushnumber(L, ImGuiWindowFlags_NoDecoration);
  lua->setfield(L, -2, "WindowFlags_NoDecoration");
  lua->pushnumber(L, ImGuiWindowFlags_NoInputs);
  lua->setfield(L, -2, "WindowFlags_NoInputs");

  // Cond flags
  lua->pushnumber(L, ImGuiCond_Always);
  lua->setfield(L, -2, "Cond_Always");
  lua->pushnumber(L, ImGuiCond_Once);
  lua->setfield(L, -2, "Cond_Once");
  lua->pushnumber(L, ImGuiCond_FirstUseEver);
  lua->setfield(L, -2, "Cond_FirstUseEver");
  lua->pushnumber(L, ImGuiCond_Appearing);
  lua->setfield(L, -2, "Cond_Appearing");

  // Hovered flags
  lua->pushnumber(L, ImGuiHoveredFlags_None);
  lua->setfield(L, -2, "HoveredFlags_None");
  lua->pushnumber(L, ImGuiHoveredFlags_ChildWindows);
  lua->setfield(L, -2, "HoveredFlags_ChildWindows");
  lua->pushnumber(L, ImGuiHoveredFlags_RootWindow);
  lua->setfield(L, -2, "HoveredFlags_RootWindow");
  lua->pushnumber(L, ImGuiHoveredFlags_AnyWindow);
  lua->setfield(L, -2, "HoveredFlags_AnyWindow");
  lua->pushnumber(L, ImGuiHoveredFlags_AllowWhenBlockedByPopup);
  lua->setfield(L, -2, "HoveredFlags_AllowWhenBlockedByPopup");
  lua->pushnumber(L, ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
  lua->setfield(L, -2, "HoveredFlags_AllowWhenBlockedByActiveItem");
  lua->pushnumber(L, ImGuiHoveredFlags_AllowWhenOverlapped);
  lua->setfield(L, -2, "HoveredFlags_AllowWhenOverlapped");
  lua->pushnumber(L, ImGuiHoveredFlags_AllowWhenDisabled);
  lua->setfield(L, -2, "HoveredFlags_AllowWhenDisabled");
  // Delay parts
  lua->pushnumber(L, ImGuiHoveredFlags_DelayNormal);
  lua->setfield(L, -2, "HoveredFlags_DelayNormal");
  lua->pushnumber(L, ImGuiHoveredFlags_DelayShort);
  lua->setfield(L, -2, "HoveredFlags_DelayShort");
  lua->pushnumber(L, ImGuiHoveredFlags_DelayNone);
  lua->setfield(L, -2, "HoveredFlags_DelayNone");
  lua->pushnumber(L, ImGuiHoveredFlags_Stationary);
  lua->setfield(L, -2, "HoveredFlags_Stationary");

  // Content region / layout
  lua->pushcclosure(L, get_content_region_avail, 0);
  lua->setfield(L, -2, "get_content_region_avail");
  lua->pushcclosure(L, get_content_region_max, 0);
  lua->setfield(L, -2, "get_content_region_max");
  lua->pushcclosure(L, get_window_size, 0);
  lua->setfield(L, -2, "get_window_size");
  lua->pushcclosure(L, get_window_pos, 0);
  lua->setfield(L, -2, "get_window_pos");
  lua->pushcclosure(L, get_display_size, 0);
  lua->setfield(L, -2, "get_display_size");
  lua->pushcclosure(L, get_monitor_size, 0);
  lua->setfield(L, -2, "get_monitor_size");
  lua->pushcclosure(L, get_monitor_work_size, 0);
  lua->setfield(L, -2, "get_monitor_work_size");
  lua->pushcclosure(L, get_cursor_pos, 0);
  lua->setfield(L, -2, "get_cursor_pos");
  lua->pushcclosure(L, set_cursor_pos, 0);
  lua->setfield(L, -2, "set_cursor_pos");

  // Scrolling
  lua->pushcclosure(L, get_scroll_x, 0);
  lua->setfield(L, -2, "get_scroll_x");
  lua->pushcclosure(L, get_scroll_y, 0);
  lua->setfield(L, -2, "get_scroll_y");
  lua->pushcclosure(L, set_scroll_x, 0);
  lua->setfield(L, -2, "set_scroll_x");
  lua->pushcclosure(L, set_scroll_y, 0);
  lua->setfield(L, -2, "set_scroll_y");
  lua->pushcclosure(L, get_scroll_max_x, 0);
  lua->setfield(L, -2, "get_scroll_max_x");
  lua->pushcclosure(L, get_scroll_max_y, 0);
  lua->setfield(L, -2, "get_scroll_max_y");
  lua->pushcclosure(L, set_scroll_here_x, 0);
  lua->setfield(L, -2, "set_scroll_here_x");
  lua->pushcclosure(L, set_scroll_here_y, 0);
  lua->setfield(L, -2, "set_scroll_here_y");

  // Text
  lua->pushcclosure(L, text, 0);
  lua->setfield(L, -2, "text");
  lua->pushcclosure(L, text_colored, 0);
  lua->setfield(L, -2, "text_colored");
  lua->pushcclosure(L, text_wrapped, 0);
  lua->setfield(L, -2, "text_wrapped");

  // Style colors
  lua->pushcclosure(L, push_color_style, 0);
  lua->setfield(L, -2, "push_color_style");
  lua->pushcclosure(L, pop_color_style, 0);
  lua->setfield(L, -2, "pop_color_style");

  // Buttons
  lua->pushcclosure(L, button, 0);
  lua->setfield(L, -2, "button");
  lua->pushcclosure(L, small_button, 0);
  lua->setfield(L, -2, "small_button");
  lua->pushcclosure(L, checkbox, 0);
  lua->setfield(L, -2, "checkbox");

  // Input
  lua->pushcclosure(L, input_text, 0);
  lua->setfield(L, -2, "input_text");
  lua->pushcclosure(L, input_text_multiline, 0);
  lua->setfield(L, -2, "input_text_multiline");
  lua->pushcclosure(L, input_float, 0);
  lua->setfield(L, -2, "input_float");
  lua->pushcclosure(L, input_int, 0);
  lua->setfield(L, -2, "input_int");

  // InputText Flags
  lua->pushnumber(L, ImGuiInputTextFlags_None);
  lua->setfield(L, -2, "InputTextFlags_None");
  lua->pushnumber(L, ImGuiInputTextFlags_CharsDecimal);
  lua->setfield(L, -2, "InputTextFlags_CharsDecimal");
  lua->pushnumber(L, ImGuiInputTextFlags_CharsHexadecimal);
  lua->setfield(L, -2, "InputTextFlags_CharsHexadecimal");
  lua->pushnumber(L, ImGuiInputTextFlags_CharsScientific);
  lua->setfield(L, -2, "InputTextFlags_CharsScientific");
  lua->pushnumber(L, ImGuiInputTextFlags_CharsUppercase);
  lua->setfield(L, -2, "InputTextFlags_CharsUppercase");
  lua->pushnumber(L, ImGuiInputTextFlags_CharsNoBlank);
  lua->setfield(L, -2, "InputTextFlags_CharsNoBlank");
  lua->pushnumber(L, ImGuiInputTextFlags_AllowTabInput);
  lua->setfield(L, -2, "InputTextFlags_AllowTabInput");
  lua->pushnumber(L, ImGuiInputTextFlags_EnterReturnsTrue);
  lua->setfield(L, -2, "InputTextFlags_EnterReturnsTrue");
  lua->pushnumber(L, ImGuiInputTextFlags_EscapeClearsAll);
  lua->setfield(L, -2, "InputTextFlags_EscapeClearsAll");
  lua->pushnumber(L, ImGuiInputTextFlags_CtrlEnterForNewLine);
  lua->setfield(L, -2, "InputTextFlags_CtrlEnterForNewLine");
  lua->pushnumber(L, ImGuiInputTextFlags_ReadOnly);
  lua->setfield(L, -2, "InputTextFlags_ReadOnly");
  lua->pushnumber(L, ImGuiInputTextFlags_Password);
  lua->setfield(L, -2, "InputTextFlags_Password");
  lua->pushnumber(L, ImGuiInputTextFlags_AlwaysOverwrite);
  lua->setfield(L, -2, "InputTextFlags_AlwaysOverwrite");
  lua->pushnumber(L, ImGuiInputTextFlags_AutoSelectAll);
  lua->setfield(L, -2, "InputTextFlags_AutoSelectAll");
  lua->pushnumber(L, ImGuiInputTextFlags_NoHorizontalScroll);
  lua->setfield(L, -2, "InputTextFlags_NoHorizontalScroll");
  lua->pushnumber(L, ImGuiInputTextFlags_NoUndoRedo);
  lua->setfield(L, -2, "InputTextFlags_NoUndoRedo");

  // Sliders
  lua->pushcclosure(L, slider_float, 0);
  lua->setfield(L, -2, "slider_float");
  lua->pushcclosure(L, slider_int, 0);
  lua->setfield(L, -2, "slider_int");

  // Layout
  lua->pushcclosure(L, same_line, 0);
  lua->setfield(L, -2, "same_line");
  lua->pushcclosure(L, separator, 0);
  lua->setfield(L, -2, "separator");
  lua->pushcclosure(L, spacing, 0);
  lua->setfield(L, -2, "spacing");
  lua->pushcclosure(L, new_line, 0);
  lua->setfield(L, -2, "new_line");
  lua->pushcclosure(L, indent, 0);
  lua->setfield(L, -2, "indent");
  lua->pushcclosure(L, unindent, 0);
  lua->setfield(L, -2, "unindent");
  lua->pushcclosure(L, set_next_item_width, 0);
  lua->setfield(L, -2, "set_next_item_width");
  lua->pushcclosure(L, set_next_window_size, 0);
  lua->setfield(L, -2, "set_next_window_size");
  lua->pushcclosure(L, set_next_window_pos, 0);
  lua->setfield(L, -2, "set_next_window_pos");
  lua->pushcclosure(L, set_next_window_focus, 0);
  lua->setfield(L, -2, "set_next_window_focus");
  lua->pushcclosure(L, set_keyboard_focus_here, 0);
  lua->setfield(L, -2, "set_keyboard_focus_here");
  lua->pushcclosure(L, bring_current_window_to_front, 0);
  lua->setfield(L, -2, "bring_current_window_to_front");
  lua->pushcclosure(L, push_id, 0);
  lua->setfield(L, -2, "push_id");
  lua->pushcclosure(L, pop_id, 0);
  lua->setfield(L, -2, "pop_id");

  // Collapsing/Tree
  lua->pushcclosure(L, collapsing_header, 0);
  lua->setfield(L, -2, "collapsing_header");
  lua->pushcclosure(L, tree_node, 0);
  lua->setfield(L, -2, "tree_node");
  lua->pushcclosure(L, tree_pop, 0);
  lua->setfield(L, -2, "tree_pop");

  // Combo
  lua->pushcclosure(L, begin_combo, 0);
  lua->setfield(L, -2, "begin_combo");
  lua->pushcclosure(L, end_combo, 0);
  lua->setfield(L, -2, "end_combo");
  lua->pushcclosure(L, selectable, 0);
  lua->setfield(L, -2, "selectable");

  // Color
  lua->pushcclosure(L, color_edit4, 0);
  lua->setfield(L, -2, "color_edit4");
  lua->pushcclosure(L, color_picker4, 0);
  lua->setfield(L, -2, "color_picker4");

  // Tooltips
  lua->pushcclosure(L, set_tooltip, 0);
  lua->setfield(L, -2, "set_tooltip");
  lua->pushcclosure(L, begin_tooltip, 0);
  lua->setfield(L, -2, "begin_tooltip");
  lua->pushcclosure(L, end_tooltip, 0);
  lua->setfield(L, -2, "end_tooltip");
  lua->pushcclosure(L, is_item_hovered, 0);
  lua->setfield(L, -2, "is_item_hovered");
  lua->pushcclosure(L, is_item_clicked, 0);
  lua->setfield(L, -2, "is_item_clicked");

  // Tabs
  lua->pushcclosure(L, begin_tab_bar, 0);
  lua->setfield(L, -2, "begin_tab_bar");
  lua->pushcclosure(L, end_tab_bar, 0);
  lua->setfield(L, -2, "end_tab_bar");
  lua->pushcclosure(L, begin_tab_item, 0);
  lua->setfield(L, -2, "begin_tab_item");
  lua->pushcclosure(L, end_tab_item, 0);
  lua->setfield(L, -2, "end_tab_item");

  // Progress
  lua->pushcclosure(L, progress_bar, 0);
  lua->setfield(L, -2, "progress_bar");

  // Drag
  lua->pushcclosure(L, drag_float, 0);
  lua->setfield(L, -2, "drag_float");
  lua->pushcclosure(L, drag_int, 0);
  lua->setfield(L, -2, "drag_int");

  // Popups/Modals
  lua->pushcclosure(L, open_popup, 0);
  lua->setfield(L, -2, "open_popup");
  lua->pushcclosure(L, begin_popup, 0);
  lua->setfield(L, -2, "begin_popup");
  lua->pushcclosure(L, begin_popup_modal, 0);
  lua->setfield(L, -2, "begin_popup_modal");
  lua->pushcclosure(L, end_popup, 0);
  lua->setfield(L, -2, "end_popup");
  lua->pushcclosure(L, close_current_popup, 0);
  lua->setfield(L, -2, "close_current_popup");
  lua->pushcclosure(L, is_popup_open, 0);
  lua->setfield(L, -2, "is_popup_open");

  // Plotting
  lua->pushcclosure(L, plot_lines, 0);
  lua->setfield(L, -2, "plot_lines");
  lua->pushcclosure(L, plot_histogram, 0);
  lua->setfield(L, -2, "plot_histogram");

  // Visibility
  lua->pushcclosure(L, set_visible, 0);
  lua->setfield(L, -2, "set_visible");
  lua->pushcclosure(L, is_visible, 0);
  lua->setfield(L, -2, "is_visible");

  // Input capture queries
  lua->pushcclosure(L, want_capture_mouse, 0);
  lua->setfield(L, -2, "want_capture_mouse");
  lua->pushcclosure(L, want_capture_keyboard, 0);
  lua->setfield(L, -2, "want_capture_keyboard");

  // Frame control
  lua->pushcclosure(L, new_frame, 0);
  lua->setfield(L, -2, "new_frame");
  lua->pushcclosure(L, render, 0);
  lua->setfield(L, -2, "render");

  // Fonts
  lua->pushcclosure(L, load_font, 0);
  lua->setfield(L, -2, "load_font");
  lua->pushcclosure(L, push_font, 0);
  lua->setfield(L, -2, "push_font");
  lua->pushcclosure(L, pop_font, 0);
  lua->setfield(L, -2, "pop_font");
  lua->pushcclosure(L, set_default_font, 0);
  lua->setfield(L, -2, "set_default_font");
  lua->pushcclosure(L, get_default_font, 0);
  lua->setfield(L, -2, "get_default_font");

  // Style
  lua->pushcclosure(L, set_style, 0);
  lua->setfield(L, -2, "set_style");

  // Overlay
  lua->pushcclosure(L, set_overlay_bind, 0);
  lua->setfield(L, -2, "set_overlay_bind");

  // Set imgui table in ljeenv
  lua->setfield(L, -2, "imgui");

  lua->pop(L, 1); // Pop ljeenv
}

} // namespace imgui_api
