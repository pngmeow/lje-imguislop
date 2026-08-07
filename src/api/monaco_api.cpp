#include "monaco_api.hpp"

#include <string>

#include "../globals.hpp"
#include "../monaco/monaco.hpp"

namespace monaco_api {
namespace {

std::string to_string(lua_State *L, int index, const char *fallback = "") {
  auto lua = g_api->lua;
  size_t length = 0;
  const char *value = lua->tolstring(L, index, &length);
  return value ? std::string(value, length) : std::string(fallback);
}

bool to_bool(lua_State *L, int index, bool fallback) {
  auto lua = g_api->lua;
  return lua->isnil(L, index) ? fallback : lua->toboolean(L, index) != 0;
}

// Reads an editor options table, leaving defaults in place for absent fields.
monaco::EditorOptions read_options(lua_State *L, int index) {
  auto lua = g_api->lua;
  monaco::EditorOptions options;

  if (lua->type(L, index) != LUA_TTABLE)
    return options;

  lua->getfield(L, index, "text");
  if (!lua->isnil(L, -1))
    options.text = to_string(L, -1);
  lua->pop(L, 1);

  lua->getfield(L, index, "language");
  if (!lua->isnil(L, -1))
    options.language = to_string(L, -1, "lua");
  lua->pop(L, 1);

  lua->getfield(L, index, "theme");
  if (!lua->isnil(L, -1))
    options.theme = to_string(L, -1, "vs-dark");
  lua->pop(L, 1);

  lua->getfield(L, index, "font_size");
  if (!lua->isnil(L, -1))
    options.font_size = static_cast<int>(lua->tonumber(L, -1));
  lua->pop(L, 1);

  lua->getfield(L, index, "width");
  if (!lua->isnil(L, -1))
    options.width = static_cast<int>(lua->tonumber(L, -1));
  lua->pop(L, 1);

  lua->getfield(L, index, "height");
  if (!lua->isnil(L, -1))
    options.height = static_cast<int>(lua->tonumber(L, -1));
  lua->pop(L, 1);

  lua->getfield(L, index, "minimap");
  options.minimap = to_bool(L, -1, options.minimap);
  lua->pop(L, 1);

  lua->getfield(L, index, "word_wrap");
  options.word_wrap = to_bool(L, -1, options.word_wrap);
  lua->pop(L, 1);

  lua->getfield(L, index, "read_only");
  options.read_only = to_bool(L, -1, options.read_only);
  lua->pop(L, 1);

  lua->getfield(L, index, "line_numbers");
  options.line_numbers = to_bool(L, -1, options.line_numbers);
  lua->pop(L, 1);

  return options;
}

int init(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushboolean(L, monaco::initialize() ? 1 : 0);
  return 1;
}

int is_available(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushboolean(L, monaco::available() ? 1 : 0);
  return 1;
}

int last_error(lua_State *L) {
  auto lua = g_api->lua;
  const auto message = monaco::last_error();
  lua->pushstring(L, message.c_str());
  return 1;
}

int create(lua_State *L) {
  auto lua = g_api->lua;
  const auto options = read_options(L, 1);
  const auto id = monaco::create(options);
  if (id == monaco::kInvalidEditor)
    lua->pushnil(L);
  else
    lua->pushnumber(L, id);
  return 1;
}

int destroy(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushboolean(L, monaco::destroy(static_cast<monaco::EditorId>(lua->tonumber(L, 1))) ? 1 : 0);
  return 1;
}

int destroy_all(lua_State *L) {
  (void)L;
  monaco::destroy_all();
  return 0;
}

int exists(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushboolean(L, monaco::exists(static_cast<monaco::EditorId>(lua->tonumber(L, 1))) ? 1 : 0);
  return 1;
}

int is_ready(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushboolean(L, monaco::ready(static_cast<monaco::EditorId>(lua->tonumber(L, 1))) ? 1 : 0);
  return 1;
}

int count(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushnumber(L, monaco::count());
  return 1;
}

int draw(lua_State *L) {
  auto lua = g_api->lua;
  const auto id = static_cast<monaco::EditorId>(lua->tonumber(L, 1));
  const auto width = static_cast<float>(lua->tonumber(L, 2));
  const auto height = static_cast<float>(lua->tonumber(L, 3));
  monaco::draw(id, width, height);
  return 0;
}

int get_text(lua_State *L) {
  auto lua = g_api->lua;
  const auto value = monaco::text(static_cast<monaco::EditorId>(lua->tonumber(L, 1)));
  lua->pushstring(L, value.c_str());
  return 1;
}

int set_text(lua_State *L) {
  auto lua = g_api->lua;
  monaco::set_text(static_cast<monaco::EditorId>(lua->tonumber(L, 1)), to_string(L, 2));
  return 0;
}

int consume_changed(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushboolean(
      L, monaco::consume_changed(static_cast<monaco::EditorId>(lua->tonumber(L, 1))) ? 1 : 0);
  return 1;
}

int set_language(lua_State *L) {
  auto lua = g_api->lua;
  monaco::set_language(static_cast<monaco::EditorId>(lua->tonumber(L, 1)), to_string(L, 2, "lua"));
  return 0;
}

int set_theme(lua_State *L) {
  auto lua = g_api->lua;
  monaco::set_theme(static_cast<monaco::EditorId>(lua->tonumber(L, 1)), to_string(L, 2, "vs-dark"));
  return 0;
}

int set_read_only(lua_State *L) {
  auto lua = g_api->lua;
  monaco::set_read_only(static_cast<monaco::EditorId>(lua->tonumber(L, 1)), to_bool(L, 2, false));
  return 0;
}

int set_minimap(lua_State *L) {
  auto lua = g_api->lua;
  monaco::set_minimap(static_cast<monaco::EditorId>(lua->tonumber(L, 1)), to_bool(L, 2, true));
  return 0;
}

int set_word_wrap(lua_State *L) {
  auto lua = g_api->lua;
  monaco::set_word_wrap(static_cast<monaco::EditorId>(lua->tonumber(L, 1)), to_bool(L, 2, false));
  return 0;
}

int set_font_size(lua_State *L) {
  auto lua = g_api->lua;
  monaco::set_font_size(static_cast<monaco::EditorId>(lua->tonumber(L, 1)),
                        static_cast<int>(lua->tonumber(L, 2)));
  return 0;
}

int set_focus(lua_State *L) {
  auto lua = g_api->lua;
  monaco::set_focus(static_cast<monaco::EditorId>(lua->tonumber(L, 1)), to_bool(L, 2, true));
  return 0;
}

int is_focused(lua_State *L) {
  auto lua = g_api->lua;
  lua->pushboolean(L, monaco::focused(static_cast<monaco::EditorId>(lua->tonumber(L, 1))) ? 1 : 0);
  return 1;
}

int execute_js(lua_State *L) {
  auto lua = g_api->lua;
  monaco::execute_js(static_cast<monaco::EditorId>(lua->tonumber(L, 1)), to_string(L, 2));
  return 0;
}

int reload(lua_State *L) {
  auto lua = g_api->lua;
  monaco::reload(static_cast<monaco::EditorId>(lua->tonumber(L, 1)));
  return 0;
}

void set_function(lua_State *L, int (*fn)(lua_State *), const char *name) {
  auto lua = g_api->lua;
  lua->pushcclosure(L, reinterpret_cast<void *>(fn), 0);
  lua->setfield(L, -2, name);
}

} // namespace

void register_all(lua_State *L) {
  auto lua = g_api->lua;

  lua->pushljeenv(L);
  lua->createtable(L, 0, 0);

  set_function(L, init, "init");
  set_function(L, is_available, "is_available");
  set_function(L, last_error, "last_error");

  set_function(L, create, "create");
  set_function(L, destroy, "destroy");
  set_function(L, destroy_all, "destroy_all");
  set_function(L, exists, "exists");
  set_function(L, is_ready, "is_ready");
  set_function(L, count, "count");

  set_function(L, draw, "draw");

  set_function(L, get_text, "get_text");
  set_function(L, set_text, "set_text");
  set_function(L, consume_changed, "consume_changed");

  set_function(L, set_language, "set_language");
  set_function(L, set_theme, "set_theme");
  set_function(L, set_read_only, "set_read_only");
  set_function(L, set_minimap, "set_minimap");
  set_function(L, set_word_wrap, "set_word_wrap");
  set_function(L, set_font_size, "set_font_size");

  set_function(L, set_focus, "set_focus");
  set_function(L, is_focused, "is_focused");

  set_function(L, execute_js, "execute_js");
  set_function(L, reload, "reload");

  lua->setfield(L, -2, "monaco");
  lua->pop(L, 1); // Pop ljeenv
}

} // namespace monaco_api
