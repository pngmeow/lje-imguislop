#pragma once
#include <imgui.h>

namespace widgets {

// How a picked color is animated when it is used.
//
// Editing and evaluating are deliberately separate: the widget is drawn wherever
// the menu lives, but the animated color is usually wanted somewhere else
// entirely - another Lua state, a draw loop with no ImGui window open - so
// eval_color_anim() stands on its own and needs nothing but the stored settings.
enum class ColorAnim {
  None = 0,
  Pulse,   // the picked color down to black and back
  Rainbow, // hue sweeps, the picked brightness and alpha are kept
};

struct ColorAnimState {
  ColorAnim mode = ColorAnim::None;
  float speed = 1.0f; // cycles per second
};

// "none" / "pulse" / "rainbow". from_name falls back to None and reports through
// `ok` so a caller can tell a typo from a deliberate "none".
const char *color_anim_name(ColorAnim mode);
ColorAnim color_anim_from_name(const char *name, bool *ok = nullptr);

// Seconds since the module loaded. Both the widget preview and eval_color_anim
// read this rather than ImGui's frame timer, so an animation stays in phase for a
// caller that never draws a frame of its own.
double anim_clock();

// The animated color at `time` seconds. Alpha is carried through untouched: the
// animations are about hue and brightness, and fading a color out is the caller's
// decision to make.
ImVec4 eval_color_anim(const ImVec4 &base, const ColorAnimState &anim, double time);

// A swatch, four value slots and the label, on one row. Clicking the swatch opens
// a popup with a color editor and an animation tab. The swatch previews the
// animation live, so a pulsing or cycling color is visible without opening it.
//
// `color` and `anim` are read and written in place. Returns true when either
// changed, so a caller can persist on the test it already makes.
bool ColorPicker4(const char *label, ImVec4 &color, ColorAnimState &anim,
                  ImGuiColorEditFlags flags = 0);

// Same widget, with the animation settings kept per widget id instead of by the
// caller. Convenient, but only for as long as the process lives - anything that
// has to survive a restart wants the overload above.
bool ColorPicker4(const char *label, ImVec4 &color, ImGuiColorEditFlags flags = 0);

// The internally kept settings for a widget id, created at defaults on first ask.
// Exposed so a caller that only sometimes supplies them can seed the same slot the
// convenience overload reads.
ColorAnimState &stored_anim(ImGuiID id);

} // namespace widgets
