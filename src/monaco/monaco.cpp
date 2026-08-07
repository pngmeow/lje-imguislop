#include "monaco.hpp"

#include <windows.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <vector>

#include <imgui.h>

#include "../log.hpp"
#include "runtime.hpp"
#include "view.hpp"

namespace monaco {
namespace {

struct Registry {
  std::mutex mutex;
  std::map<EditorId, CefRefPtr<View>> views;
  // Editors waiting for their browser to finish closing. Their textures belong
  // to the render thread, so they cannot simply be dropped here.
  std::vector<CefRefPtr<View>> closing;
  EditorId next_id = 1;
};

Registry &registry() {
  static Registry instance;
  return instance;
}

CefRefPtr<View> find(EditorId id) {
  auto &reg = registry();
  std::lock_guard lock(reg.mutex);
  auto it = reg.views.find(id);
  return it == reg.views.end() ? nullptr : it->second;
}

} // namespace

bool initialize() { return runtime::start(); }

bool available() { return runtime::running(); }

std::string last_error() { return runtime::error(); }

EditorId create(const EditorOptions &options) {
  if (!runtime::start()) {
    logger::error("monaco: cannot create editor, CEF is not running (%s)",
                  runtime::error().c_str());
    return kInvalidEditor;
  }

  auto &reg = registry();
  EditorId id;
  CefRefPtr<View> view;
  {
    std::lock_guard lock(reg.mutex);
    id = reg.next_id++;
    view = new View(id, options);
    reg.views.emplace(id, view);
  }

  view->create_browser();
  logger::info("monaco: created editor %d (%dx%d, %s)", id, options.width, options.height,
               options.language.c_str());
  return id;
}

bool destroy(EditorId id) {
  auto &reg = registry();
  CefRefPtr<View> view;
  {
    std::lock_guard lock(reg.mutex);
    auto it = reg.views.find(id);
    if (it == reg.views.end())
      return false;
    view = it->second;
    reg.views.erase(it);
    reg.closing.push_back(view);
  }

  view->close();
  return true;
}

void destroy_all() {
  auto &reg = registry();
  std::vector<CefRefPtr<View>> views;
  {
    std::lock_guard lock(reg.mutex);
    for (auto &[id, view] : reg.views)
      views.push_back(view);
    reg.closing.insert(reg.closing.end(), views.begin(), views.end());
    reg.views.clear();
  }

  for (auto &view : views)
    view->close();
}

bool exists(EditorId id) { return find(id) != nullptr; }

bool ready(EditorId id) {
  auto view = find(id);
  return view && view->ready();
}

int count() {
  auto &reg = registry();
  std::lock_guard lock(reg.mutex);
  return static_cast<int>(reg.views.size());
}

void draw(EditorId id, float width, float height) {
  if (auto view = find(id))
    view->draw(width, height);
}

std::string text(EditorId id) {
  auto view = find(id);
  return view ? view->document().text() : std::string();
}

void set_text(EditorId id, const std::string &value) {
  if (auto view = find(id))
    view->set_text(value);
}

bool consume_changed(EditorId id) {
  auto view = find(id);
  return view && view->document().consume_changed();
}

void set_language(EditorId id, const std::string &language) {
  if (auto view = find(id))
    view->set_language(language);
}

void set_theme(EditorId id, const std::string &theme) {
  if (auto view = find(id))
    view->set_theme(theme);
}

void set_read_only(EditorId id, bool read_only) {
  if (auto view = find(id))
    view->set_read_only(read_only);
}

void set_minimap(EditorId id, bool minimap) {
  if (auto view = find(id))
    view->set_minimap(minimap);
}

void set_word_wrap(EditorId id, bool word_wrap) {
  if (auto view = find(id))
    view->set_word_wrap(word_wrap);
}

void set_font_size(EditorId id, int size) {
  if (auto view = find(id))
    view->set_font_size(size);
}

void set_focus(EditorId id, bool focus) {
  if (auto view = find(id))
    view->set_focus(focus);
}

bool focused(EditorId id) {
  auto view = find(id);
  return view && view->focused();
}

bool any_focused() {
  auto &reg = registry();
  std::lock_guard lock(reg.mutex);
  for (auto &[id, view] : reg.views) {
    if (view->focused())
      return true;
  }
  return false;
}

void update_imgui_nav() {
  if (!ImGui::GetCurrentContext())
    return;

  auto &io = ImGui::GetIO();
  io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
}

void execute_js(EditorId id, const std::string &code) {
  if (auto view = find(id))
    view->execute_js(code);
}

void reload(EditorId id) {
  if (auto view = find(id))
    view->reload();
}

void upload_textures(IDirect3DDevice9 *device) {
  if (!device)
    return;

  auto &reg = registry();
  std::vector<CefRefPtr<View>> live;
  std::vector<CefRefPtr<View>> closing;
  {
    std::lock_guard lock(reg.mutex);
    if (reg.views.empty() && reg.closing.empty())
      return;

    live.reserve(reg.views.size());
    for (auto &[id, view] : reg.views)
      live.push_back(view);

    // Editors whose browser has gone away can finally release their D3D
    // resources, which is only legal here on the render thread.
    for (auto it = reg.closing.begin(); it != reg.closing.end();) {
      closing.push_back(*it);
      it = (*it)->closed() ? reg.closing.erase(it) : it + 1;
    }
  }

  for (auto &view : live)
    view->upload(device);
  for (auto &view : closing)
    view->release_texture();
}

void release_textures() {
  auto &reg = registry();
  std::vector<CefRefPtr<View>> views;
  {
    std::lock_guard lock(reg.mutex);
    for (auto &[id, view] : reg.views)
      views.push_back(view);
    views.insert(views.end(), reg.closing.begin(), reg.closing.end());
  }

  for (auto &view : views)
    view->release_texture();
}

void shutdown() {
  if (!runtime::running()) {
    // Nothing was ever started, but make sure a half-created registry does not
    // outlive us.
    destroy_all();
    return;
  }

  logger::info("monaco: shutting down %d editor(s)", count());
  destroy_all();

  // Give the browsers a moment to actually go away: CefShutdown is only valid
  // once they have.
  auto &reg = registry();
  for (int attempt = 0; attempt < 300; ++attempt) {
    bool pending = false;
    {
      std::lock_guard lock(reg.mutex);
      for (auto &view : reg.closing) {
        if (!view->closed()) {
          pending = true;
          break;
        }
      }
    }
    if (!pending)
      break;
    Sleep(10);
  }

  release_textures();
  {
    std::lock_guard lock(reg.mutex);
    reg.closing.clear();
  }

  runtime::stop();
}

} // namespace monaco