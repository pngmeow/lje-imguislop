#include "color_picker.hpp"

#include "imgui_internal.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace widgets {
namespace {

constexpr float kSwatchAspect = 1.6f; // swatch width as a multiple of the row height
constexpr float kSlotsWidth = 190.0f; // the four value slots together
constexpr float kPopupWidth = 200.0f;
constexpr float kPreviewHeight = 14.0f;

// One entry per widget that has ever been drawn without the caller holding the
// settings. Widget ids are a fixed set in practice, so this settles rather than
// grows; nothing here is worth a per-frame sweep.
std::unordered_map<ImGuiID, ColorAnimState> g_anim;

const char *const kModeNames[] = {"none", "pulse", "rainbow"};

// The pulse and rainbow previews are both a single cycle sampled across the bar,
// which is what makes the two tell themselves apart at a glance.
void draw_cycle_preview(const ImVec4 &base, const ColorAnimState &anim, float width) {
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();

  constexpr int kSteps = 48;
  const float step_w = width / kSteps;

  for (int i = 0; i < kSteps; ++i) {
    // A cycle is 1/speed seconds long, so sampling t over exactly that span draws
    // one period no matter what the speed is set to.
    double t = anim.speed > 0.0f ? (static_cast<double>(i) / kSteps) / anim.speed : 0.0;
    ImVec4 sampled = eval_color_anim(base, anim, t);

    ImVec2 min(pos.x + i * step_w, pos.y);
    // The +1 closes the seam rounding would otherwise leave between the slices.
    ImVec2 max(min.x + step_w + 1.0f, pos.y + kPreviewHeight);
    draw_list->AddRectFilled(min, max, ImGui::GetColorU32(sampled));
  }

  draw_list->AddRect(pos, ImVec2(pos.x + width, pos.y + kPreviewHeight),
                     ImGui::GetColorU32(ImGuiCol_Border));
  ImGui::Dummy(ImVec2(width, kPreviewHeight));
}

bool draw_animation_tab(const ImVec4 &color, ColorAnimState &anim) {
  bool changed = false;

  int mode = static_cast<int>(anim.mode);
  changed |= ImGui::RadioButton("Off", &mode, static_cast<int>(ColorAnim::None));
  ImGui::SameLine();
  changed |= ImGui::RadioButton("Pulse", &mode, static_cast<int>(ColorAnim::Pulse));
  ImGui::SameLine();
  changed |= ImGui::RadioButton("Rainbow", &mode, static_cast<int>(ColorAnim::Rainbow));
  anim.mode = static_cast<ColorAnim>(mode);

  const bool off = anim.mode == ColorAnim::None;

  ImGui::BeginDisabled(off);
  ImGui::SetNextItemWidth(kPopupWidth - ImGui::CalcTextSize("Speed").x -
                          ImGui::GetStyle().ItemInnerSpacing.x);
  changed |= ImGui::SliderFloat("Speed", &anim.speed, 0.05f, 5.0f, "%.2f/s");
  ImGui::EndDisabled();

  ImGui::Spacing();

  // Off still gets a bar, just a flat one: an empty gap under the radio buttons
  // reads as something failing to draw.
  draw_cycle_preview(color, anim, kPopupWidth);

  ImGui::TextDisabled("%s", off ? "no animation" : "one full cycle");

  return changed;
}

} // namespace

const char *color_anim_name(ColorAnim mode) {
  int index = static_cast<int>(mode);
  if (index < 0 || index >= IM_ARRAYSIZE(kModeNames))
    return kModeNames[0];

  return kModeNames[index];
}

ColorAnim color_anim_from_name(const char *name, bool *ok) {
  if (name) {
    for (int i = 0; i < IM_ARRAYSIZE(kModeNames); ++i) {
      if (std::strcmp(name, kModeNames[i]) == 0) {
        if (ok)
          *ok = true;
        return static_cast<ColorAnim>(i);
      }
    }
  }

  if (ok)
    *ok = false;
  return ColorAnim::None;
}

double anim_clock() {
  using clock = std::chrono::steady_clock;
  static const clock::time_point start = clock::now();

  return std::chrono::duration<double>(clock::now() - start).count();
}

ImVec4 eval_color_anim(const ImVec4 &base, const ColorAnimState &anim, double time) {
  const float speed = anim.speed > 0.0f ? anim.speed : 0.0f;

  switch (anim.mode) {
  case ColorAnim::Pulse: {
    // cos starts at 1, so the first frame sits on the color that was picked
    // rather than somewhere mid fade.
    const float phase = static_cast<float>(time) * speed * 2.0f * IM_PI;
    const float t = (std::cos(phase) + 1.0f) * 0.5f;
    return ImVec4(base.x * t, base.y * t, base.z * t, base.w);
  }

  case ColorAnim::Rainbow: {
    // Saturation is forced rather than taken from the base: white and grey have no
    // hue to sweep, and those are exactly the defaults people leave a picker on.
    float h = 0.0f, s = 0.0f, v = 0.0f;
    ImGui::ColorConvertRGBtoHSV(base.x, base.y, base.z, h, s, v);

    float hue = static_cast<float>(std::fmod(time * speed, 1.0));
    if (hue < 0.0f)
      hue += 1.0f;

    float r = 0.0f, g = 0.0f, b = 0.0f;
    ImGui::ColorConvertHSVtoRGB(hue, 1.0f, v > 0.0f ? v : 1.0f, r, g, b);
    return ImVec4(r, g, b, base.w);
  }

  default:
    return base;
  }
}

ColorAnimState &stored_anim(ImGuiID id) { return g_anim[id]; }

bool ColorPicker4(const char *label, ImVec4 &color, ColorAnimState &anim,
                  ImGuiColorEditFlags flags) {
  ImGuiContext *ctx = ImGui::GetCurrentContext();
  ImGuiWindow *window = ctx ? ctx->CurrentWindow : nullptr;
  if (!window || window->SkipItems)
    return false;

  if (!label)
    label = "";

  const ImGuiStyle &style = ImGui::GetStyle();
  const float height = ImGui::GetFrameHeight();
  bool changed = false;

  ImGui::PushID(label);

  // The swatch leads the row and previews the animation, so the popup is not the
  // only place the setting is visible.
  const ImVec4 preview = eval_color_anim(color, anim, anim_clock());
  if (ImGui::ColorButton("##swatch", preview, ImGuiColorEditFlags_NoTooltip,
                         ImVec2(height * kSwatchAspect, height)))
    ImGui::OpenPopup("##picker");

  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s\nclick to edit", color_anim_name(anim.mode));

  ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);

  // ColorEdit4 with the preview suppressed is exactly the four R/G/B/A slots, and
  // it brings the drag, the keyboard entry and the 0..255 display with it.
  ImGui::SetNextItemWidth(kSlotsWidth);
  changed |= ImGui::ColorEdit4("##slots", &color.x,
                               (flags & ~ImGuiColorEditFlags_NoLabel) |
                                   ImGuiColorEditFlags_NoSmallPreview |
                                   ImGuiColorEditFlags_NoLabel |
                                   ImGuiColorEditFlags_Uint8 |
                                   ImGuiColorEditFlags_DisplayRGB);

  const char *label_end = ImGui::FindRenderedTextEnd(label);
  if (label != label_end && !(flags & ImGuiColorEditFlags_NoLabel)) {
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::TextEx(label, label_end);
  }

  if (ImGui::BeginPopup("##picker")) {
    if (ImGui::BeginTabBar("##tabs")) {
      if (ImGui::BeginTabItem("Color")) {
        ImGui::SetNextItemWidth(kPopupWidth);
        // No side preview: the row's own swatch is right behind the popup, and the
        // big one would push the popup past "small".
        changed |= ImGui::ColorPicker4("##editor", &color.x,
                                       ImGuiColorEditFlags_AlphaBar |
                                           ImGuiColorEditFlags_NoSidePreview |
                                           ImGuiColorEditFlags_NoSmallPreview |
                                           ImGuiColorEditFlags_NoLabel);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Animation")) {
        changed |= draw_animation_tab(color, anim);
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }

    ImGui::EndPopup();
  }

  ImGui::PopID();

  return changed;
}

bool ColorPicker4(const char *label, ImVec4 &color, ImGuiColorEditFlags flags) {
  ImGuiContext *ctx = ImGui::GetCurrentContext();
  if (!ctx || !ctx->CurrentWindow)
    return false;

  return ColorPicker4(label, color, stored_anim(ImGui::GetID(label ? label : "")), flags);
}

} // namespace widgets
