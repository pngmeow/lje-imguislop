#include "imanim_api.hpp"

#include "../globals.hpp"
#include <im_anim.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace imanim_api {

// ----------------------------------------------------------------------------
// Argument helpers
// ----------------------------------------------------------------------------

// ImAnim keys everything on ImGuiID. Scripts pass readable strings ("alpha",
// "my_button") which we hash; numbers pass through so an id captured from a
// previous call can be reused verbatim.
static ImGuiID to_id(lua_State *L, int idx) {
  auto lua = g_api->lua;
  if (lua->type(L, idx) == LUA_TSTRING) {
    const char *s = lua->tolstring(L, idx, nullptr);
    return s ? ImHashStr(s) : 0u;
  }
  return static_cast<ImGuiID>(lua->tonumber(L, idx));
}

static bool has_arg(lua_State *L, int idx) {
  auto lua = g_api->lua;
  return idx <= lua->gettop(L) && !lua->isnil(L, idx);
}

static float opt_number(lua_State *L, int idx, float fallback) {
  auto lua = g_api->lua;
  return has_arg(L, idx) ? static_cast<float>(lua->tonumber(L, idx)) : fallback;
}

static int opt_int(lua_State *L, int idx, int fallback) {
  auto lua = g_api->lua;
  return has_arg(L, idx) ? static_cast<int>(lua->tonumber(L, idx)) : fallback;
}

// Every animation call needs a delta time. Defaulting to ImGui's own means
// scripts never have to thread it through.
static float opt_dt(lua_State *L, int idx) {
  return opt_number(L, idx, ImGui::GetIO().DeltaTime);
}

// Accepts {x=,y=} or {[1],[2]}.
static ImVec2 read_vec2(lua_State *L, int idx, ImVec2 fallback = ImVec2(0, 0)) {
  auto lua = g_api->lua;
  if (!has_arg(L, idx))
    return fallback;
  if (lua->type(L, idx) != LUA_TTABLE) {
    // A bare number fills both components, handy for uniform amplitudes.
    const float v = static_cast<float>(lua->tonumber(L, idx));
    return ImVec2(v, v);
  }
  ImVec2 out = fallback;
  lua->getfield(L, idx, "x");
  if (!lua->isnil(L, -1)) {
    out.x = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->getfield(L, idx, "y");
    out.y = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    return out;
  }
  lua->pop(L, 1);

  lua->rawgeti(L, idx, 1);
  out.x = static_cast<float>(lua->tonumber(L, -1));
  lua->pop(L, 1);
  lua->rawgeti(L, idx, 2);
  out.y = static_cast<float>(lua->tonumber(L, -1));
  lua->pop(L, 1);
  return out;
}

// Accepts {x=,y=,z=,w=}, {r=,g=,b=,a=} or {[1]..[4]}. Colors and plain vec4s
// share this so a color can be written either way.
static ImVec4 read_vec4(lua_State *L, int idx, ImVec4 fallback = ImVec4(0, 0, 0, 0)) {
  auto lua = g_api->lua;
  if (!has_arg(L, idx))
    return fallback;
  if (lua->type(L, idx) != LUA_TTABLE) {
    const float v = static_cast<float>(lua->tonumber(L, idx));
    return ImVec4(v, v, v, v);
  }
  ImVec4 out = fallback;

  lua->getfield(L, idx, "x");
  if (!lua->isnil(L, -1)) {
    out.x = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->getfield(L, idx, "y");
    out.y = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->getfield(L, idx, "z");
    out.z = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->getfield(L, idx, "w");
    if (!lua->isnil(L, -1))
      out.w = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    return out;
  }
  lua->pop(L, 1);

  lua->getfield(L, idx, "r");
  if (!lua->isnil(L, -1)) {
    out.x = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->getfield(L, idx, "g");
    out.y = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->getfield(L, idx, "b");
    out.z = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->getfield(L, idx, "a");
    out.w = lua->isnil(L, -1) ? 1.0f : static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    return out;
  }
  lua->pop(L, 1);

  lua->rawgeti(L, idx, 1);
  out.x = static_cast<float>(lua->tonumber(L, -1));
  lua->pop(L, 1);
  lua->rawgeti(L, idx, 2);
  out.y = static_cast<float>(lua->tonumber(L, -1));
  lua->pop(L, 1);
  lua->rawgeti(L, idx, 3);
  out.z = static_cast<float>(lua->tonumber(L, -1));
  lua->pop(L, 1);
  lua->rawgeti(L, idx, 4);
  out.w = lua->isnil(L, -1) ? 1.0f : static_cast<float>(lua->tonumber(L, -1));
  lua->pop(L, 1);
  return out;
}

// An easing is either a preset number (imgui.Ease_*) or a table carrying the
// parametric arguments, e.g. {type = imgui.Ease_Spring, p0 = 1, p1 = 120, p2 = 14}.
static iam_ease_desc read_ease(lua_State *L, int idx) {
  auto lua = g_api->lua;
  if (!has_arg(L, idx))
    return iam_ease_preset(iam_ease_out_cubic);
  if (lua->type(L, idx) != LUA_TTABLE)
    return iam_ease_preset(static_cast<int>(lua->tonumber(L, idx)));

  iam_ease_desc e = {iam_ease_linear, 0.0f, 0.0f, 0.0f, 0.0f};
  lua->getfield(L, idx, "type");
  e.type = static_cast<int>(lua->tonumber(L, -1));
  lua->pop(L, 1);
  lua->getfield(L, idx, "p0");
  e.p0 = static_cast<float>(lua->tonumber(L, -1));
  lua->pop(L, 1);
  lua->getfield(L, idx, "p1");
  e.p1 = static_cast<float>(lua->tonumber(L, -1));
  lua->pop(L, 1);
  lua->getfield(L, idx, "p2");
  e.p2 = static_cast<float>(lua->tonumber(L, -1));
  lua->pop(L, 1);
  lua->getfield(L, idx, "p3");
  e.p3 = static_cast<float>(lua->tonumber(L, -1));
  lua->pop(L, 1);
  return e;
}

static void push_vec2(lua_State *L, ImVec2 v) {
  auto lua = g_api->lua;
  lua->pushnumber(L, v.x);
  lua->pushnumber(L, v.y);
}

static void push_vec4(lua_State *L, ImVec4 v) {
  auto lua = g_api->lua;
  lua->pushnumber(L, v.x);
  lua->pushnumber(L, v.y);
  lua->pushnumber(L, v.z);
  lua->pushnumber(L, v.w);
}

// ----------------------------------------------------------------------------
// System / frame
// ----------------------------------------------------------------------------

// iam_update_begin_frame() and iam_clip_update() are driven by Overlay so that
// scripts cannot forget them; this only exposes the tuning knobs.

static int anim_gc(lua_State *L) {
  iam_gc(static_cast<unsigned int>(opt_int(L, 1, 600)));
  return 0;
}

static int anim_clip_gc(lua_State *L) {
  iam_clip_gc(static_cast<unsigned int>(opt_int(L, 1, 600)));
  return 0;
}

static int anim_pool_clear(lua_State *L) {
  (void)L;
  iam_pool_clear();
  return 0;
}

static int anim_reserve(lua_State *L) {
  iam_reserve(opt_int(L, 1, 0), opt_int(L, 2, 0), opt_int(L, 3, 0),
              opt_int(L, 4, 0), opt_int(L, 5, 0));
  return 0;
}

static int anim_set_time_scale(lua_State *L) {
  iam_set_global_time_scale(opt_number(L, 1, 1.0f));
  return 0;
}

static int anim_get_time_scale(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushnumber(L, iam_get_global_time_scale());
  return 1;
}

static int anim_set_lazy_init(lua_State *L) {
  auto lua = g_api->lua;
  iam_set_lazy_init(lua->toboolean(L, 1) != 0);
  return 0;
}

static int anim_is_lazy_init(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushboolean(L, iam_is_lazy_init_enabled() ? 1 : 0);
  return 1;
}

static int anim_set_ease_lut_samples(lua_State *L) {
  iam_set_ease_lut_samples(opt_int(L, 1, 256));
  return 0;
}

static int anim_eval_ease(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushnumber(L, iam_eval_preset(opt_int(L, 1, iam_ease_linear), opt_number(L, 2, 0.0f)));
  return 1;
}

static int anim_anchor_size(lua_State *L) {
  push_vec2(L, iam_anchor_size(opt_int(L, 1, iam_anchor_window_content)));
  return 2;
}

static int anim_show_inspector(lua_State *L) {
  auto lua = g_api->lua;
  bool open = has_arg(L, 1) ? (lua->toboolean(L, 1) != 0) : true;
  iam_show_unified_inspector(&open);
  lua->pushboolean(L, open ? 1 : 0);
  return 1;
}

static int anim_blend_color(lua_State *L) {
  push_vec4(L, iam_get_blended_color(read_vec4(L, 1), read_vec4(L, 2),
                                     opt_number(L, 3, 0.0f),
                                     opt_int(L, 4, iam_col_oklab)));
  return 4;
}

// ----------------------------------------------------------------------------
// Tweens
// ----------------------------------------------------------------------------

// Shared signature: (id, channel, target, duration, [ease], [policy], [dt], [init])

static int anim_tween_float(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushnumber(L, iam_tween_float(to_id(L, 1), to_id(L, 2),
                                     opt_number(L, 3, 0.0f), opt_number(L, 4, 0.2f),
                                     read_ease(L, 5), opt_int(L, 6, iam_policy_crossfade),
                                     opt_dt(L, 7), opt_number(L, 8, 0.0f)));
  return 1;
}

static int anim_tween_int(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushnumber(L, iam_tween_int(to_id(L, 1), to_id(L, 2),
                                   opt_int(L, 3, 0), opt_number(L, 4, 0.2f),
                                   read_ease(L, 5), opt_int(L, 6, iam_policy_crossfade),
                                   opt_dt(L, 7), opt_int(L, 8, 0)));
  return 1;
}

static int anim_tween_vec2(lua_State *L) {
  push_vec2(L, iam_tween_vec2(to_id(L, 1), to_id(L, 2), read_vec2(L, 3),
                              opt_number(L, 4, 0.2f), read_ease(L, 5),
                              opt_int(L, 6, iam_policy_crossfade), opt_dt(L, 7),
                              read_vec2(L, 8)));
  return 2;
}

static int anim_tween_vec4(lua_State *L) {
  push_vec4(L, iam_tween_vec4(to_id(L, 1), to_id(L, 2), read_vec4(L, 3),
                              opt_number(L, 4, 0.2f), read_ease(L, 5),
                              opt_int(L, 6, iam_policy_crossfade), opt_dt(L, 7),
                              read_vec4(L, 8)));
  return 4;
}

// Color takes an extra color_space before dt, matching iam_tween_color.
static int anim_tween_color(lua_State *L) {
  push_vec4(L, iam_tween_color(to_id(L, 1), to_id(L, 2), read_vec4(L, 3, ImVec4(1, 1, 1, 1)),
                               opt_number(L, 4, 0.2f), read_ease(L, 5),
                               opt_int(L, 6, iam_policy_crossfade),
                               opt_int(L, 7, iam_col_oklab), opt_dt(L, 8),
                               read_vec4(L, 9, ImVec4(1, 1, 1, 1))));
  return 4;
}

// ----------------------------------------------------------------------------
// Rebase - redirect an in-flight tween without restarting it
// ----------------------------------------------------------------------------

static int anim_rebase_float(lua_State *L) {
  iam_rebase_float(to_id(L, 1), to_id(L, 2), opt_number(L, 3, 0.0f), opt_dt(L, 4));
  return 0;
}

static int anim_rebase_int(lua_State *L) {
  iam_rebase_int(to_id(L, 1), to_id(L, 2), opt_int(L, 3, 0), opt_dt(L, 4));
  return 0;
}

static int anim_rebase_vec2(lua_State *L) {
  iam_rebase_vec2(to_id(L, 1), to_id(L, 2), read_vec2(L, 3), opt_dt(L, 4));
  return 0;
}

static int anim_rebase_vec4(lua_State *L) {
  iam_rebase_vec4(to_id(L, 1), to_id(L, 2), read_vec4(L, 3), opt_dt(L, 4));
  return 0;
}

static int anim_rebase_color(lua_State *L) {
  iam_rebase_color(to_id(L, 1), to_id(L, 2), read_vec4(L, 3, ImVec4(1, 1, 1, 1)), opt_dt(L, 4));
  return 0;
}

// ----------------------------------------------------------------------------
// Oscillators, shake, wiggle, noise
// ----------------------------------------------------------------------------

static int anim_oscillate(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushnumber(L, iam_oscillate(to_id(L, 1), opt_number(L, 2, 1.0f),
                                   opt_number(L, 3, 1.0f), opt_int(L, 4, iam_wave_sine),
                                   opt_number(L, 5, 0.0f), opt_dt(L, 6)));
  return 1;
}

static int anim_oscillate_vec2(lua_State *L) {
  push_vec2(L, iam_oscillate_vec2(to_id(L, 1), read_vec2(L, 2, ImVec2(1, 1)),
                                  read_vec2(L, 3, ImVec2(1, 1)),
                                  opt_int(L, 4, iam_wave_sine),
                                  read_vec2(L, 5), opt_dt(L, 6)));
  return 2;
}

static int anim_shake(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushnumber(L, iam_shake(to_id(L, 1), opt_number(L, 2, 1.0f),
                               opt_number(L, 3, 20.0f), opt_number(L, 4, 0.5f),
                               opt_dt(L, 5)));
  return 1;
}

static int anim_shake_vec2(lua_State *L) {
  push_vec2(L, iam_shake_vec2(to_id(L, 1), read_vec2(L, 2, ImVec2(1, 1)),
                              opt_number(L, 3, 20.0f), opt_number(L, 4, 0.5f),
                              opt_dt(L, 5)));
  return 2;
}

static int anim_trigger_shake(lua_State *L) {
  iam_trigger_shake(to_id(L, 1));
  return 0;
}

static int anim_wiggle(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushnumber(L, iam_wiggle(to_id(L, 1), opt_number(L, 2, 1.0f),
                                opt_number(L, 3, 1.0f), opt_dt(L, 4)));
  return 1;
}

static int anim_wiggle_vec2(lua_State *L) {
  push_vec2(L, iam_wiggle_vec2(to_id(L, 1), read_vec2(L, 2, ImVec2(1, 1)),
                               opt_number(L, 3, 1.0f), opt_dt(L, 4)));
  return 2;
}

static int anim_smooth_noise(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushnumber(L, iam_smooth_noise_float(to_id(L, 1), opt_number(L, 2, 1.0f),
                                            opt_number(L, 3, 1.0f), opt_dt(L, 4)));
  return 1;
}

static int anim_smooth_noise_vec2(lua_State *L) {
  push_vec2(L, iam_smooth_noise_vec2(to_id(L, 1), read_vec2(L, 2, ImVec2(1, 1)),
                                     opt_number(L, 3, 1.0f), opt_dt(L, 4)));
  return 2;
}

// ----------------------------------------------------------------------------
// Scroll
// ----------------------------------------------------------------------------

static int anim_scroll_to_x(lua_State *L) {
  iam_scroll_to_x(opt_number(L, 1, 0.0f), opt_number(L, 2, 0.3f), read_ease(L, 3));
  return 0;
}

static int anim_scroll_to_y(lua_State *L) {
  iam_scroll_to_y(opt_number(L, 1, 0.0f), opt_number(L, 2, 0.3f), read_ease(L, 3));
  return 0;
}

static int anim_scroll_to_top(lua_State *L) {
  iam_scroll_to_top(opt_number(L, 1, 0.3f), read_ease(L, 2));
  return 0;
}

static int anim_scroll_to_bottom(lua_State *L) {
  iam_scroll_to_bottom(opt_number(L, 1, 0.3f), read_ease(L, 2));
  return 0;
}

// ----------------------------------------------------------------------------
// Clips
// ----------------------------------------------------------------------------

// Channel type for a keyframe: explicit via key.type, else inferred from the
// shape of key.value (number -> float, 2 entries -> vec2, 4 entries -> vec4).
enum class KeyKind { Float, Int, Vec2, Vec4, Color };

static KeyKind key_kind(lua_State *L, int key_idx, int value_idx) {
  auto lua = g_api->lua;
  lua->getfield(L, key_idx, "type");
  if (!lua->isnil(L, -1)) {
    const char *t = lua->tolstring(L, -1, nullptr);
    KeyKind kind = KeyKind::Float;
    if (t) {
      if (ImStricmp(t, "int") == 0)
        kind = KeyKind::Int;
      else if (ImStricmp(t, "vec2") == 0)
        kind = KeyKind::Vec2;
      else if (ImStricmp(t, "vec4") == 0)
        kind = KeyKind::Vec4;
      else if (ImStricmp(t, "color") == 0)
        kind = KeyKind::Color;
    }
    lua->pop(L, 1);
    return kind;
  }
  lua->pop(L, 1);

  if (lua->type(L, value_idx) != LUA_TTABLE)
    return KeyKind::Float;
  return lua->objlen(L, value_idx) == 2 ? KeyKind::Vec2 : KeyKind::Vec4;
}

// imgui.anim_clip_define(clip_id, {
//   loop = true, direction = imgui.Dir_Alternate, loop_count = -1,
//   delay = 0.0, stagger = { count = 5, each_delay = 0.05, from_center = 0.0 },
//   keys = {
//     { channel = "scale", time = 0.0, value = 0.0, ease = imgui.Ease_OutElastic },
//     { channel = "tint",  time = 0.5, value = {1,0,0,1}, type = "color" },
//   },
// })
static int anim_clip_define(lua_State *L) {
  auto lua = g_api->lua;
  if (lua->type(L, 2) != LUA_TTABLE) {
    lua->pushboolean(L, 0);
    return 1;
  }

  iam_clip clip = iam_clip::begin(to_id(L, 1));

  lua->getfield(L, 2, "keys");
  if (lua->type(L, -1) == LUA_TTABLE) {
    const int keys_idx = lua->gettop(L);
    const int count = static_cast<int>(lua->objlen(L, keys_idx));
    for (int i = 1; i <= count; i++) {
      lua->rawgeti(L, keys_idx, i);
      const int key_idx = lua->gettop(L);
      if (lua->type(L, key_idx) != LUA_TTABLE) {
        lua->pop(L, 1);
        continue;
      }

      lua->getfield(L, key_idx, "channel");
      const ImGuiID channel = to_id(L, lua->gettop(L));
      lua->pop(L, 1);

      lua->getfield(L, key_idx, "time");
      const float time = static_cast<float>(lua->tonumber(L, -1));
      lua->pop(L, 1);

      lua->getfield(L, key_idx, "ease");
      const int ease = lua->isnil(L, -1) ? iam_ease_linear
                                         : static_cast<int>(lua->tonumber(L, -1));
      lua->pop(L, 1);

      lua->getfield(L, key_idx, "space");
      const int space = lua->isnil(L, -1) ? iam_col_oklab
                                          : static_cast<int>(lua->tonumber(L, -1));
      lua->pop(L, 1);

      lua->getfield(L, key_idx, "value");
      const int value_idx = lua->gettop(L);
      switch (key_kind(L, key_idx, value_idx)) {
      case KeyKind::Int:
        clip.key_int(channel, time, static_cast<int>(lua->tonumber(L, value_idx)), ease);
        break;
      case KeyKind::Vec2:
        clip.key_vec2(channel, time, read_vec2(L, value_idx), ease);
        break;
      case KeyKind::Vec4:
        clip.key_vec4(channel, time, read_vec4(L, value_idx), ease);
        break;
      case KeyKind::Color:
        clip.key_color(channel, time, read_vec4(L, value_idx, ImVec4(1, 1, 1, 1)), space, ease);
        break;
      case KeyKind::Float:
      default:
        clip.key_float(channel, time, static_cast<float>(lua->tonumber(L, value_idx)), ease);
        break;
      }
      lua->pop(L, 1); // value
      lua->pop(L, 1); // key table
    }
  }
  lua->pop(L, 1); // keys

  lua->getfield(L, 2, "delay");
  if (!lua->isnil(L, -1))
    clip.set_delay(static_cast<float>(lua->tonumber(L, -1)));
  lua->pop(L, 1);

  lua->getfield(L, 2, "loop");
  if (!lua->isnil(L, -1)) {
    const bool loop = lua->toboolean(L, -1) != 0;
    lua->pop(L, 1);
    lua->getfield(L, 2, "direction");
    const int dir = lua->isnil(L, -1) ? iam_dir_normal : static_cast<int>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->getfield(L, 2, "loop_count");
    const int loop_count = lua->isnil(L, -1) ? -1 : static_cast<int>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    clip.set_loop(loop, dir, loop_count);
  } else {
    lua->pop(L, 1);
  }

  lua->getfield(L, 2, "stagger");
  if (lua->type(L, -1) == LUA_TTABLE) {
    const int st = lua->gettop(L);
    lua->getfield(L, st, "count");
    const int count = static_cast<int>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->getfield(L, st, "each_delay");
    const float each = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    lua->getfield(L, st, "from_center");
    const float bias = static_cast<float>(lua->tonumber(L, -1));
    lua->pop(L, 1);
    clip.set_stagger(count, each, bias);
  }
  lua->pop(L, 1);

  clip.end();
  lua->pushboolean(L, 1);
  return 1;
}

static int anim_clip_exists(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushboolean(L, iam_clip_exists(to_id(L, 1)) ? 1 : 0);
  return 1;
}

static int anim_clip_duration(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushnumber(L, iam_clip_duration(to_id(L, 1)));
  return 1;
}

// Returns the instance id, which is what every anim_* instance call takes.
static int anim_play(lua_State *L) {
  auto lua = g_api->lua;
  iam_instance inst = iam_play(to_id(L, 1), to_id(L, 2));
  if (!inst.valid()) {
    lua->pushnil(L);
    return 1;
  }
  lua->pushnumber(L, static_cast<double>(inst.id()));
  return 1;
}

static int anim_play_stagger(lua_State *L) {
  auto lua = g_api->lua;
  iam_instance inst = iam_play_stagger(to_id(L, 1), to_id(L, 2), opt_int(L, 3, 0));
  if (!inst.valid()) {
    lua->pushnil(L);
    return 1;
  }
  lua->pushnumber(L, static_cast<double>(inst.id()));
  return 1;
}

static int anim_stagger_delay(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushnumber(L, iam_stagger_delay(to_id(L, 1), opt_int(L, 2, 0)));
  return 1;
}

// Instance lookups go through iam_get_instance so a stale id yields an invalid
// handle instead of touching freed state.
static iam_instance instance_arg(lua_State *L, int idx) {
  return iam_get_instance(to_id(L, idx));
}

static int anim_pause(lua_State *L) {
  iam_instance inst = instance_arg(L, 1);
  if (inst.valid())
    inst.pause();
  return 0;
}

static int anim_resume(lua_State *L) {
  iam_instance inst = instance_arg(L, 1);
  if (inst.valid())
    inst.resume();
  return 0;
}

static int anim_stop(lua_State *L) {
  iam_instance inst = instance_arg(L, 1);
  if (inst.valid())
    inst.stop();
  return 0;
}

static int anim_destroy(lua_State *L) {
  iam_instance inst = instance_arg(L, 1);
  if (inst.valid())
    inst.destroy();
  return 0;
}

static int anim_seek(lua_State *L) {
  iam_instance inst = instance_arg(L, 1);
  if (inst.valid())
    inst.seek(opt_number(L, 2, 0.0f));
  return 0;
}

static int anim_set_instance_time_scale(lua_State *L) {
  iam_instance inst = instance_arg(L, 1);
  if (inst.valid())
    inst.set_time_scale(opt_number(L, 2, 1.0f));
  return 0;
}

static int anim_set_weight(lua_State *L) {
  iam_instance inst = instance_arg(L, 1);
  if (inst.valid())
    inst.set_weight(opt_number(L, 2, 1.0f));
  return 0;
}

static int anim_is_valid(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushboolean(L, instance_arg(L, 1).valid() ? 1 : 0);
  return 1;
}

static int anim_is_playing(lua_State *L) {
  auto lua = g_api->lua;
  iam_instance inst = instance_arg(L, 1);
  lua->pushboolean(L, (inst.valid() && inst.is_playing()) ? 1 : 0);
  return 1;
}

static int anim_is_paused(lua_State *L) {
  auto lua = g_api->lua;
  iam_instance inst = instance_arg(L, 1);
  lua->pushboolean(L, (inst.valid() && inst.is_paused()) ? 1 : 0);
  return 1;
}

static int anim_time(lua_State *L) {
  auto lua = g_api->lua;
  iam_instance inst = instance_arg(L, 1);
  if (!inst.valid()) {
    lua->pushnil(L);
    return 1;
  }
  lua->pushnumber(L, inst.time());
  return 1;
}

static int anim_duration(lua_State *L) {
  auto lua = g_api->lua;
  iam_instance inst = instance_arg(L, 1);
  if (!inst.valid()) {
    lua->pushnil(L);
    return 1;
  }
  lua->pushnumber(L, inst.duration());
  return 1;
}

// Sampling a channel the clip does not define returns nil rather than 0, so
// scripts can tell "no such channel" from "value is zero".
static int anim_get_float(lua_State *L) {
  auto lua = g_api->lua;
  iam_instance inst = instance_arg(L, 1);
  float out = 0.0f;
  if (!inst.valid() || !inst.get_float(to_id(L, 2), &out)) {
    lua->pushnil(L);
    return 1;
  }
  lua->pushnumber(L, out);
  return 1;
}

static int anim_get_int(lua_State *L) {
  auto lua = g_api->lua;
  iam_instance inst = instance_arg(L, 1);
  int out = 0;
  if (!inst.valid() || !inst.get_int(to_id(L, 2), &out)) {
    lua->pushnil(L);
    return 1;
  }
  lua->pushnumber(L, out);
  return 1;
}

static int anim_get_vec2(lua_State *L) {
  auto lua = g_api->lua;
  iam_instance inst = instance_arg(L, 1);
  ImVec2 out(0, 0);
  if (!inst.valid() || !inst.get_vec2(to_id(L, 2), &out)) {
    lua->pushnil(L);
    return 1;
  }
  push_vec2(L, out);
  return 2;
}

static int anim_get_vec4(lua_State *L) {
  auto lua = g_api->lua;
  iam_instance inst = instance_arg(L, 1);
  ImVec4 out(0, 0, 0, 0);
  if (!inst.valid() || !inst.get_vec4(to_id(L, 2), &out)) {
    lua->pushnil(L);
    return 1;
  }
  push_vec4(L, out);
  return 4;
}

static int anim_get_color(lua_State *L) {
  auto lua = g_api->lua;
  iam_instance inst = instance_arg(L, 1);
  ImVec4 out(1, 1, 1, 1);
  if (!inst.valid() || !inst.get_color(to_id(L, 2), &out, opt_int(L, 3, iam_col_oklab))) {
    lua->pushnil(L);
    return 1;
  }
  push_vec4(L, out);
  return 4;
}

// ----------------------------------------------------------------------------
// Registration
// ----------------------------------------------------------------------------

static void set_fn(lua_State *L, const char *name, int (*fn)(lua_State *)) {
  auto lua = g_api->lua;
  lua->pushcclosure(L, reinterpret_cast<void *>(fn), 0);
  lua->setfield(L, -2, name);
}

static void set_int(lua_State *L, const char *name, int value) {
  auto lua = g_api->lua;
  lua->pushnumber(L, value);
  lua->setfield(L, -2, name);
}

void register_all(lua_State *L) {
  auto lua = g_api->lua;

  lua->pushljeenv(L);

  // Extend the table imgui_api::register_all() already created rather than
  // making a second namespace.
  lua->getfield(L, -1, "imgui");
  if (lua->type(L, -1) != LUA_TTABLE) {
    lua->pop(L, 2);
    return;
  }

  // System
  set_fn(L, "anim_gc", anim_gc);
  set_fn(L, "anim_clip_gc", anim_clip_gc);
  set_fn(L, "anim_pool_clear", anim_pool_clear);
  set_fn(L, "anim_reserve", anim_reserve);
  set_fn(L, "anim_set_time_scale", anim_set_time_scale);
  set_fn(L, "anim_get_time_scale", anim_get_time_scale);
  set_fn(L, "anim_set_lazy_init", anim_set_lazy_init);
  set_fn(L, "anim_is_lazy_init", anim_is_lazy_init);
  set_fn(L, "anim_set_ease_lut_samples", anim_set_ease_lut_samples);
  set_fn(L, "anim_eval_ease", anim_eval_ease);
  set_fn(L, "anim_anchor_size", anim_anchor_size);
  set_fn(L, "anim_show_inspector", anim_show_inspector);
  set_fn(L, "anim_blend_color", anim_blend_color);

  // Tweens
  set_fn(L, "anim_tween_float", anim_tween_float);
  set_fn(L, "anim_tween_int", anim_tween_int);
  set_fn(L, "anim_tween_vec2", anim_tween_vec2);
  set_fn(L, "anim_tween_vec4", anim_tween_vec4);
  set_fn(L, "anim_tween_color", anim_tween_color);

  // Rebase
  set_fn(L, "anim_rebase_float", anim_rebase_float);
  set_fn(L, "anim_rebase_int", anim_rebase_int);
  set_fn(L, "anim_rebase_vec2", anim_rebase_vec2);
  set_fn(L, "anim_rebase_vec4", anim_rebase_vec4);
  set_fn(L, "anim_rebase_color", anim_rebase_color);

  // Procedural
  set_fn(L, "anim_oscillate", anim_oscillate);
  set_fn(L, "anim_oscillate_vec2", anim_oscillate_vec2);
  set_fn(L, "anim_shake", anim_shake);
  set_fn(L, "anim_shake_vec2", anim_shake_vec2);
  set_fn(L, "anim_trigger_shake", anim_trigger_shake);
  set_fn(L, "anim_wiggle", anim_wiggle);
  set_fn(L, "anim_wiggle_vec2", anim_wiggle_vec2);
  set_fn(L, "anim_smooth_noise", anim_smooth_noise);
  set_fn(L, "anim_smooth_noise_vec2", anim_smooth_noise_vec2);

  // Scroll
  set_fn(L, "anim_scroll_to_x", anim_scroll_to_x);
  set_fn(L, "anim_scroll_to_y", anim_scroll_to_y);
  set_fn(L, "anim_scroll_to_top", anim_scroll_to_top);
  set_fn(L, "anim_scroll_to_bottom", anim_scroll_to_bottom);

  // Clips
  set_fn(L, "anim_clip_define", anim_clip_define);
  set_fn(L, "anim_clip_exists", anim_clip_exists);
  set_fn(L, "anim_clip_duration", anim_clip_duration);
  set_fn(L, "anim_play", anim_play);
  set_fn(L, "anim_play_stagger", anim_play_stagger);
  set_fn(L, "anim_stagger_delay", anim_stagger_delay);
  set_fn(L, "anim_pause", anim_pause);
  set_fn(L, "anim_resume", anim_resume);
  set_fn(L, "anim_stop", anim_stop);
  set_fn(L, "anim_destroy", anim_destroy);
  set_fn(L, "anim_seek", anim_seek);
  set_fn(L, "anim_set_instance_time_scale", anim_set_instance_time_scale);
  set_fn(L, "anim_set_weight", anim_set_weight);
  set_fn(L, "anim_is_valid", anim_is_valid);
  set_fn(L, "anim_is_playing", anim_is_playing);
  set_fn(L, "anim_is_paused", anim_is_paused);
  set_fn(L, "anim_time", anim_time);
  set_fn(L, "anim_duration", anim_duration);
  set_fn(L, "anim_get_float", anim_get_float);
  set_fn(L, "anim_get_int", anim_get_int);
  set_fn(L, "anim_get_vec2", anim_get_vec2);
  set_fn(L, "anim_get_vec4", anim_get_vec4);
  set_fn(L, "anim_get_color", anim_get_color);

  // Easing presets
  set_int(L, "Ease_Linear", iam_ease_linear);
  set_int(L, "Ease_InQuad", iam_ease_in_quad);
  set_int(L, "Ease_OutQuad", iam_ease_out_quad);
  set_int(L, "Ease_InOutQuad", iam_ease_in_out_quad);
  set_int(L, "Ease_InCubic", iam_ease_in_cubic);
  set_int(L, "Ease_OutCubic", iam_ease_out_cubic);
  set_int(L, "Ease_InOutCubic", iam_ease_in_out_cubic);
  set_int(L, "Ease_InQuart", iam_ease_in_quart);
  set_int(L, "Ease_OutQuart", iam_ease_out_quart);
  set_int(L, "Ease_InOutQuart", iam_ease_in_out_quart);
  set_int(L, "Ease_InQuint", iam_ease_in_quint);
  set_int(L, "Ease_OutQuint", iam_ease_out_quint);
  set_int(L, "Ease_InOutQuint", iam_ease_in_out_quint);
  set_int(L, "Ease_InSine", iam_ease_in_sine);
  set_int(L, "Ease_OutSine", iam_ease_out_sine);
  set_int(L, "Ease_InOutSine", iam_ease_in_out_sine);
  set_int(L, "Ease_InExpo", iam_ease_in_expo);
  set_int(L, "Ease_OutExpo", iam_ease_out_expo);
  set_int(L, "Ease_InOutExpo", iam_ease_in_out_expo);
  set_int(L, "Ease_InCirc", iam_ease_in_circ);
  set_int(L, "Ease_OutCirc", iam_ease_out_circ);
  set_int(L, "Ease_InOutCirc", iam_ease_in_out_circ);
  set_int(L, "Ease_InBack", iam_ease_in_back);
  set_int(L, "Ease_OutBack", iam_ease_out_back);
  set_int(L, "Ease_InOutBack", iam_ease_in_out_back);
  set_int(L, "Ease_InElastic", iam_ease_in_elastic);
  set_int(L, "Ease_OutElastic", iam_ease_out_elastic);
  set_int(L, "Ease_InOutElastic", iam_ease_in_out_elastic);
  set_int(L, "Ease_InBounce", iam_ease_in_bounce);
  set_int(L, "Ease_OutBounce", iam_ease_out_bounce);
  set_int(L, "Ease_InOutBounce", iam_ease_in_out_bounce);
  set_int(L, "Ease_Steps", iam_ease_steps);
  set_int(L, "Ease_CubicBezier", iam_ease_cubic_bezier);
  set_int(L, "Ease_Spring", iam_ease_spring);

  // Retarget policy
  set_int(L, "Policy_Crossfade", iam_policy_crossfade);
  set_int(L, "Policy_Cut", iam_policy_cut);
  set_int(L, "Policy_Queue", iam_policy_queue);

  // Color spaces
  set_int(L, "ColorSpace_Srgb", iam_col_srgb);
  set_int(L, "ColorSpace_SrgbLinear", iam_col_srgb_linear);
  set_int(L, "ColorSpace_Hsv", iam_col_hsv);
  set_int(L, "ColorSpace_Oklab", iam_col_oklab);
  set_int(L, "ColorSpace_Oklch", iam_col_oklch);

  // Oscillator waveforms
  set_int(L, "Wave_Sine", iam_wave_sine);
  set_int(L, "Wave_Triangle", iam_wave_triangle);
  set_int(L, "Wave_Sawtooth", iam_wave_sawtooth);
  set_int(L, "Wave_Square", iam_wave_square);

  // Anchor spaces
  set_int(L, "Anchor_WindowContent", iam_anchor_window_content);
  set_int(L, "Anchor_Window", iam_anchor_window);
  set_int(L, "Anchor_Viewport", iam_anchor_viewport);
  set_int(L, "Anchor_LastItem", iam_anchor_last_item);

  // Loop directions
  set_int(L, "Dir_Normal", iam_dir_normal);
  set_int(L, "Dir_Reverse", iam_dir_reverse);
  set_int(L, "Dir_Alternate", iam_dir_alternate);

  lua->pop(L, 1); // imgui table
  lua->pop(L, 1); // ljeenv
}

} // namespace imanim_api
