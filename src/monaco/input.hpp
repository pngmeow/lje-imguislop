#pragma once
#include <cstdint>
#include <vector>

#include "include/internal/cef_types_wrappers.h"

// Translation from the ImGui input state into the Windows-flavoured events
// Chromium's off-screen path expects. ImGui is the only input source here: the
// overlay already routes the host window's messages into it, so reading them a
// second time would double up every keystroke.
namespace monaco::input {

// EVENTFLAG_* mask for the current frame (modifier keys, lock keys and held
// mouse buttons).
uint32_t modifiers();

// Key events produced by this ImGui frame, in the order Chromium expects
// (RAWKEYDOWN -> CHAR -> KEYUP).
void collect_key_events(std::vector<CefKeyEvent> &out);

// Maps a cef_cursor_type_t onto the closest ImGuiMouseCursor.
int to_imgui_cursor(int cef_cursor_type);

} // namespace monaco::input