#include "input.hpp"

#include <windows.h>

#include <imgui.h>

#include "include/internal/cef_types.h"

namespace monaco::input {
namespace {

struct KeyMapping {
  ImGuiKey key;
  int vk;
  bool extended;
};

// Chromium keys off Windows virtual key codes even in windowless mode, so the
// ImGui key set has to be mapped back. |extended| reproduces bit 24 of the
// WM_KEYDOWN lparam, which is what tells Chromium apart e.g. the numpad Enter
// from the main one and right Alt from left.
constexpr KeyMapping kKeyMap[] = {
    {ImGuiKey_Tab, VK_TAB, false},
    {ImGuiKey_LeftArrow, VK_LEFT, true},
    {ImGuiKey_RightArrow, VK_RIGHT, true},
    {ImGuiKey_UpArrow, VK_UP, true},
    {ImGuiKey_DownArrow, VK_DOWN, true},
    {ImGuiKey_PageUp, VK_PRIOR, true},
    {ImGuiKey_PageDown, VK_NEXT, true},
    {ImGuiKey_Home, VK_HOME, true},
    {ImGuiKey_End, VK_END, true},
    {ImGuiKey_Insert, VK_INSERT, true},
    {ImGuiKey_Delete, VK_DELETE, true},
    {ImGuiKey_Backspace, VK_BACK, false},
    {ImGuiKey_Space, VK_SPACE, false},
    {ImGuiKey_Enter, VK_RETURN, false},
    {ImGuiKey_Escape, VK_ESCAPE, false},
    {ImGuiKey_LeftCtrl, VK_CONTROL, false},
    {ImGuiKey_LeftShift, VK_SHIFT, false},
    {ImGuiKey_LeftAlt, VK_MENU, false},
    {ImGuiKey_LeftSuper, VK_LWIN, true},
    {ImGuiKey_RightCtrl, VK_CONTROL, true},
    {ImGuiKey_RightShift, VK_SHIFT, false},
    {ImGuiKey_RightAlt, VK_MENU, true},
    {ImGuiKey_RightSuper, VK_RWIN, true},
    {ImGuiKey_Menu, VK_APPS, true},
    {ImGuiKey_0, '0', false},
    {ImGuiKey_1, '1', false},
    {ImGuiKey_2, '2', false},
    {ImGuiKey_3, '3', false},
    {ImGuiKey_4, '4', false},
    {ImGuiKey_5, '5', false},
    {ImGuiKey_6, '6', false},
    {ImGuiKey_7, '7', false},
    {ImGuiKey_8, '8', false},
    {ImGuiKey_9, '9', false},
    {ImGuiKey_A, 'A', false},
    {ImGuiKey_B, 'B', false},
    {ImGuiKey_C, 'C', false},
    {ImGuiKey_D, 'D', false},
    {ImGuiKey_E, 'E', false},
    {ImGuiKey_F, 'F', false},
    {ImGuiKey_G, 'G', false},
    {ImGuiKey_H, 'H', false},
    {ImGuiKey_I, 'I', false},
    {ImGuiKey_J, 'J', false},
    {ImGuiKey_K, 'K', false},
    {ImGuiKey_L, 'L', false},
    {ImGuiKey_M, 'M', false},
    {ImGuiKey_N, 'N', false},
    {ImGuiKey_O, 'O', false},
    {ImGuiKey_P, 'P', false},
    {ImGuiKey_Q, 'Q', false},
    {ImGuiKey_R, 'R', false},
    {ImGuiKey_S, 'S', false},
    {ImGuiKey_T, 'T', false},
    {ImGuiKey_U, 'U', false},
    {ImGuiKey_V, 'V', false},
    {ImGuiKey_W, 'W', false},
    {ImGuiKey_X, 'X', false},
    {ImGuiKey_Y, 'Y', false},
    {ImGuiKey_Z, 'Z', false},
    {ImGuiKey_F1, VK_F1, false},
    {ImGuiKey_F2, VK_F2, false},
    {ImGuiKey_F3, VK_F3, false},
    {ImGuiKey_F4, VK_F4, false},
    {ImGuiKey_F5, VK_F5, false},
    {ImGuiKey_F6, VK_F6, false},
    {ImGuiKey_F7, VK_F7, false},
    {ImGuiKey_F8, VK_F8, false},
    {ImGuiKey_F9, VK_F9, false},
    {ImGuiKey_F10, VK_F10, false},
    {ImGuiKey_F11, VK_F11, false},
    {ImGuiKey_F12, VK_F12, false},
    {ImGuiKey_F13, VK_F13, false},
    {ImGuiKey_F14, VK_F14, false},
    {ImGuiKey_F15, VK_F15, false},
    {ImGuiKey_F16, VK_F16, false},
    {ImGuiKey_F17, VK_F17, false},
    {ImGuiKey_F18, VK_F18, false},
    {ImGuiKey_F19, VK_F19, false},
    {ImGuiKey_F20, VK_F20, false},
    {ImGuiKey_F21, VK_F21, false},
    {ImGuiKey_F22, VK_F22, false},
    {ImGuiKey_F23, VK_F23, false},
    {ImGuiKey_F24, VK_F24, false},
    {ImGuiKey_Apostrophe, VK_OEM_7, false},
    {ImGuiKey_Comma, VK_OEM_COMMA, false},
    {ImGuiKey_Minus, VK_OEM_MINUS, false},
    {ImGuiKey_Period, VK_OEM_PERIOD, false},
    {ImGuiKey_Slash, VK_OEM_2, false},
    {ImGuiKey_Semicolon, VK_OEM_1, false},
    {ImGuiKey_Equal, VK_OEM_PLUS, false},
    {ImGuiKey_LeftBracket, VK_OEM_4, false},
    {ImGuiKey_Backslash, VK_OEM_5, false},
    {ImGuiKey_RightBracket, VK_OEM_6, false},
    {ImGuiKey_GraveAccent, VK_OEM_3, false},
    {ImGuiKey_CapsLock, VK_CAPITAL, false},
    {ImGuiKey_ScrollLock, VK_SCROLL, false},
    {ImGuiKey_NumLock, VK_NUMLOCK, true},
    {ImGuiKey_PrintScreen, VK_SNAPSHOT, true},
    {ImGuiKey_Pause, VK_PAUSE, false},
    {ImGuiKey_Keypad0, VK_NUMPAD0, false},
    {ImGuiKey_Keypad1, VK_NUMPAD1, false},
    {ImGuiKey_Keypad2, VK_NUMPAD2, false},
    {ImGuiKey_Keypad3, VK_NUMPAD3, false},
    {ImGuiKey_Keypad4, VK_NUMPAD4, false},
    {ImGuiKey_Keypad5, VK_NUMPAD5, false},
    {ImGuiKey_Keypad6, VK_NUMPAD6, false},
    {ImGuiKey_Keypad7, VK_NUMPAD7, false},
    {ImGuiKey_Keypad8, VK_NUMPAD8, false},
    {ImGuiKey_Keypad9, VK_NUMPAD9, false},
    {ImGuiKey_KeypadDecimal, VK_DECIMAL, false},
    {ImGuiKey_KeypadDivide, VK_DIVIDE, true},
    {ImGuiKey_KeypadMultiply, VK_MULTIPLY, false},
    {ImGuiKey_KeypadSubtract, VK_SUBTRACT, false},
    {ImGuiKey_KeypadAdd, VK_ADD, false},
    {ImGuiKey_KeypadEnter, VK_RETURN, true},
    {ImGuiKey_Oem102, VK_OEM_102, false},
};

bool is_keypad(ImGuiKey key) {
  return (key >= ImGuiKey_Keypad0 && key <= ImGuiKey_Keypad9) || key == ImGuiKey_KeypadDecimal ||
         key == ImGuiKey_KeypadDivide || key == ImGuiKey_KeypadMultiply ||
         key == ImGuiKey_KeypadSubtract || key == ImGuiKey_KeypadAdd ||
         key == ImGuiKey_KeypadEnter || key == ImGuiKey_KeypadEqual;
}

bool is_left_side(ImGuiKey key) {
  return key == ImGuiKey_LeftCtrl || key == ImGuiKey_LeftShift || key == ImGuiKey_LeftAlt ||
         key == ImGuiKey_LeftSuper;
}

bool is_right_side(ImGuiKey key) {
  return key == ImGuiKey_RightCtrl || key == ImGuiKey_RightShift || key == ImGuiKey_RightAlt ||
         key == ImGuiKey_RightSuper;
}

// Reconstructs the lparam a real WM_KEYDOWN/WM_KEYUP would have carried.
int native_code(const KeyMapping &mapping, bool key_up) {
  UINT scan = MapVirtualKeyW(static_cast<UINT>(mapping.vk), MAPVK_VK_TO_VSC);
  int lparam = 1 | static_cast<int>((scan & 0xFF) << 16);
  if (mapping.extended)
    lparam |= 1 << 24;
  if (key_up)
    lparam |= (1 << 30) | (1 << 31);
  return lparam;
}

void fill_common(CefKeyEvent &event, const KeyMapping &mapping, uint32_t base_modifiers) {
  event.windows_key_code = mapping.vk;
  event.modifiers = base_modifiers;
  if (is_keypad(mapping.key))
    event.modifiers |= EVENTFLAG_IS_KEY_PAD;
  if (is_left_side(mapping.key))
    event.modifiers |= EVENTFLAG_IS_LEFT;
  if (is_right_side(mapping.key) || mapping.extended)
    event.modifiers |= EVENTFLAG_IS_RIGHT;
  // Alt chords arrive as WM_SYSKEY* natively; Chromium uses this to keep them
  // out of text input.
  event.is_system_key =
      (base_modifiers & EVENTFLAG_ALT_DOWN) != 0 && (base_modifiers & EVENTFLAG_CONTROL_DOWN) == 0;
}

} // namespace

uint32_t modifiers() {
  const ImGuiIO &io = ImGui::GetIO();
  uint32_t result = 0;

  if (io.KeyCtrl)
    result |= EVENTFLAG_CONTROL_DOWN;
  if (io.KeyShift)
    result |= EVENTFLAG_SHIFT_DOWN;
  if (io.KeyAlt)
    result |= EVENTFLAG_ALT_DOWN;
  if (io.KeySuper)
    result |= EVENTFLAG_COMMAND_DOWN;

  if (GetKeyState(VK_CAPITAL) & 1)
    result |= EVENTFLAG_CAPS_LOCK_ON;
  if (GetKeyState(VK_NUMLOCK) & 1)
    result |= EVENTFLAG_NUM_LOCK_ON;

  if (io.MouseDown[0])
    result |= EVENTFLAG_LEFT_MOUSE_BUTTON;
  if (io.MouseDown[1])
    result |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
  if (io.MouseDown[2])
    result |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;

  return result;
}

void collect_key_events(std::vector<CefKeyEvent> &out) {
  ImGuiIO &io = ImGui::GetIO();
  const uint32_t base = modifiers();

  for (const auto &mapping : kKeyMap) {
    // Repeat is enabled: Monaco relies on it for held arrows and backspace.
    if (ImGui::IsKeyPressed(mapping.key, true)) {
      CefKeyEvent event;
      fill_common(event, mapping, base);
      event.type = KEYEVENT_RAWKEYDOWN;
      event.native_key_code = native_code(mapping, false);
      out.push_back(event);
    }
  }

  // Text produced by this frame. The overlay's WndProc feeds WM_CHAR into
  // ImGui, so this already accounts for keyboard layout, dead keys and IME
  // composition results.
  for (ImWchar character : io.InputQueueCharacters) {
    if (character == 0)
      continue;
    CefKeyEvent event;
    event.type = KEYEVENT_CHAR;
    event.modifiers = base;
    event.windows_key_code = static_cast<int>(character);
    event.native_key_code = 0;
    event.character = static_cast<char16_t>(character);
    event.unmodified_character = static_cast<char16_t>(character);
    out.push_back(event);
  }

  for (const auto &mapping : kKeyMap) {
    if (ImGui::IsKeyReleased(mapping.key)) {
      CefKeyEvent event;
      fill_common(event, mapping, base);
      event.type = KEYEVENT_KEYUP;
      event.native_key_code = native_code(mapping, true);
      out.push_back(event);
    }
  }
}

int to_imgui_cursor(int cef_cursor_type) {
  switch (cef_cursor_type) {
  case CT_IBEAM:
  case CT_VERTICALTEXT:
    return ImGuiMouseCursor_TextInput;
  case CT_HAND:
  case CT_GRAB:
  case CT_GRABBING:
    return ImGuiMouseCursor_Hand;
  case CT_MOVE:
    return ImGuiMouseCursor_ResizeAll;
  case CT_EASTWESTRESIZE:
  case CT_EASTRESIZE:
  case CT_WESTRESIZE:
  case CT_COLUMNRESIZE:
    return ImGuiMouseCursor_ResizeEW;
  case CT_NORTHSOUTHRESIZE:
  case CT_NORTHRESIZE:
  case CT_SOUTHRESIZE:
  case CT_ROWRESIZE:
    return ImGuiMouseCursor_ResizeNS;
  case CT_NORTHEASTSOUTHWESTRESIZE:
  case CT_NORTHEASTRESIZE:
  case CT_SOUTHWESTRESIZE:
    return ImGuiMouseCursor_ResizeNESW;
  case CT_NORTHWESTSOUTHEASTRESIZE:
  case CT_NORTHWESTRESIZE:
  case CT_SOUTHEASTRESIZE:
    return ImGuiMouseCursor_ResizeNWSE;
  case CT_NOTALLOWED:
  case CT_NODROP:
    return ImGuiMouseCursor_NotAllowed;
  case CT_WAIT:
    return ImGuiMouseCursor_Wait;
  case CT_PROGRESS:
    return ImGuiMouseCursor_Progress;
  default:
    return ImGuiMouseCursor_Arrow;
  }
}

} // namespace monaco::input