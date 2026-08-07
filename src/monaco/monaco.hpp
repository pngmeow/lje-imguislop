#pragma once
#include <string>

struct IDirect3DDevice9;

// Public surface of the Monaco integration. Deliberately free of CEF and ImGui
// types so the Lua bindings and the overlay can use it without pulling
// Chromium headers into their translation units.
namespace monaco {

using EditorId = int;
inline constexpr EditorId kInvalidEditor = 0;

struct EditorOptions {
  std::string text;
  std::string language = "lua";
  std::string theme = "vs-dark";
  int font_size = 14;
  int width = 800;
  int height = 600;
  bool minimap = true;
  bool word_wrap = false;
  bool read_only = false;
  bool line_numbers = true;
};

// Starts the embedded Chromium. Blocks until initialization settles (a second
// or two on first run) and is safe to call repeatedly; create() calls it
// implicitly.
bool initialize();
void shutdown();
bool available();
std::string last_error();

EditorId create(const EditorOptions &options);
bool destroy(EditorId id);
void destroy_all();
bool exists(EditorId id);
bool ready(EditorId id);
int count();

// Draws the editor as an ImGui widget at the current cursor position. Must be
// called between imgui.new_frame() and imgui.render().
void draw(EditorId id, float width, float height);

std::string text(EditorId id);
void set_text(EditorId id, const std::string &value);
bool consume_changed(EditorId id);

void set_language(EditorId id, const std::string &language);
void set_theme(EditorId id, const std::string &theme);
void set_read_only(EditorId id, bool read_only);
void set_minimap(EditorId id, bool minimap);
void set_word_wrap(EditorId id, bool word_wrap);
void set_font_size(EditorId id, int size);

void set_focus(EditorId id, bool focus);
bool focused(EditorId id);

// True while any editor holds the keyboard.
bool any_focused();

// Turns ImGui's keyboard navigation off for as long as an editor is focused,
// and back on afterwards. Call it immediately before ImGui::NewFrame().
//
// Space and Enter are ImGui's nav "activate" keys, and the overlay has to keep
// feeding keys to ImGui even while an editor owns them, because Monaco reads
// its own input back out of ImGui's IO. Without this, typing in the editor
// walks nav focus onto the surrounding window's widgets and the next Space
// presses one - closing the window through its own title bar button, most
// visibly of all.
void update_imgui_nav();

void execute_js(EditorId id, const std::string &code);
void reload(EditorId id);

// Called from the D3D9 render thread inside the EndScene hook, before ImGui
// draw data is submitted: this is the only point at which touching the device
// is safe.
void upload_textures(IDirect3DDevice9 *device);
void release_textures();

} // namespace monaco