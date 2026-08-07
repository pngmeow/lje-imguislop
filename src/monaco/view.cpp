#include "view.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <imgui.h>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_parser.h"

#include "../cef/app.hpp"
#include "../cef/scheme.hpp"
#include "../cef/task.hpp"
#include "../log.hpp"
#include "input.hpp"

namespace monaco {
namespace {

constexpr int kFrameRate = 60;
constexpr uint32_t kBackgroundColor = 0xFF1E1E1E; // Monaco's vs-dark chrome.

// Escapes a UTF-8 string into a JavaScript string literal, including the two
// line separators that are legal in JSON but terminate a JS line.
std::string js_string(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('"');
  for (size_t i = 0; i < value.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(value[i]);
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    default:
      if (c < 0x20 || c == 0x7F) {
        char buffer[7];
        std::snprintf(buffer, sizeof(buffer), "\\u%04X", c);
        out += buffer;
      } else if (c == 0xE2 && i + 2 < value.size() &&
                 static_cast<unsigned char>(value[i + 1]) == 0x80 &&
                 (static_cast<unsigned char>(value[i + 2]) == 0xA8 ||
                  static_cast<unsigned char>(value[i + 2]) == 0xA9)) {
        out += static_cast<unsigned char>(value[i + 2]) == 0xA8 ? "\\u2028" : "\\u2029";
        i += 2;
      } else {
        out.push_back(static_cast<char>(c));
      }
      break;
    }
  }
  out.push_back('"');
  return out;
}

std::string js_bool(bool value) { return value ? "true" : "false"; }

void copy_rect(uint8_t *dst, const uint8_t *src, int stride_pixels, const CefRect &rect) {
  const size_t pitch = static_cast<size_t>(stride_pixels) * 4;
  const size_t row = static_cast<size_t>(rect.width) * 4;
  const size_t offset = static_cast<size_t>(rect.y) * pitch + static_cast<size_t>(rect.x) * 4;
  dst += offset;
  src += offset;
  for (int y = 0; y < rect.height; ++y) {
    std::memcpy(dst, src, row);
    dst += pitch;
    src += pitch;
  }
}

} // namespace

View::View(EditorId id, EditorOptions options)
    : id_(id), widget_id_("##monaco_editor_" + std::to_string(id)), options_(std::move(options)),
      document_(options_.text), width_(std::max(1, options_.width)),
      height_(std::max(1, options_.height)) {
  // The router is only wired up to this handler once the browser exists:
  // AddHandler/RemoveHandler are UI thread only, and everything else the router
  // needs already arrives on that thread.
  router_ = CefMessageRouterBrowserSide::Create(MonacoCefApp::router_config());
}

View::~View() = default;

void View::create_browser() {
  CefWindowInfo window_info;
  window_info.SetAsWindowless(nullptr);
  window_info.shared_texture_enabled = false;

  CefBrowserSettings settings;
  settings.windowless_frame_rate = kFrameRate;
  settings.background_color = kBackgroundColor;

  const std::string url = std::string(cef_scheme::kOrigin) + "/index.html";
  CefBrowserHost::CreateBrowser(window_info, this, url, settings, nullptr, nullptr);
}

void View::close() {
  if (closing_.exchange(true))
    return;

  CefRefPtr<CefBrowser> browser;
  {
    std::lock_guard lock(browser_mutex_);
    browser = browser_;
  }

  if (!browser) {
    // The browser never finished creating; nothing will call OnBeforeClose.
    closed_ = true;
    return;
  }

  cef_task::post_ui([browser] { browser->GetHost()->CloseBrowser(true); });
}

// ---------------------------------------------------------------------------
// CefClient
// ---------------------------------------------------------------------------

bool View::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                    CefProcessId source_process,
                                    CefRefPtr<CefProcessMessage> message) {
  return router_->OnProcessMessageReceived(browser, frame, source_process, message);
}

// ---------------------------------------------------------------------------
// CefRenderHandler
// ---------------------------------------------------------------------------

void View::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect) {
  rect.x = 0;
  rect.y = 0;
  rect.width = std::max(1, width_.load());
  rect.height = std::max(1, height_.load());
}

bool View::GetScreenInfo(CefRefPtr<CefBrowser> browser, CefScreenInfo &screen_info) {
  // The widget is composited into the game's back buffer, which is already in
  // device pixels, so any host DPI scaling must not be applied a second time.
  screen_info.device_scale_factor = 1.0f;
  screen_info.depth = 32;
  screen_info.depth_per_component = 8;
  screen_info.is_monochrome = false;

  CefRect rect;
  GetViewRect(browser, rect);
  screen_info.rect = rect;
  screen_info.available_rect = rect;
  return true;
}

void View::OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) {
  std::lock_guard lock(frame_mutex_);
  popup_visible_ = show;
  if (!show) {
    popup_rect_ = CefRect();
    popup_width_ = popup_height_ = 0;
  }
  has_dirty_ = true;
  dirty_ = {0, 0, static_cast<LONG>(view_width_), static_cast<LONG>(view_height_)};
}

void View::OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect &rect) {
  std::lock_guard lock(frame_mutex_);
  popup_rect_ = rect;
  has_dirty_ = true;
  dirty_ = {0, 0, static_cast<LONG>(view_width_), static_cast<LONG>(view_height_)};
}

void View::OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                   const RectList &dirty_rects, const void *buffer, int width, int height) {
  if (!buffer || width <= 0 || height <= 0)
    return;

  const auto *pixels = static_cast<const uint8_t *>(buffer);
  ++frames_painted_;
  std::lock_guard lock(frame_mutex_);

  if (type == PET_POPUP) {
    const size_t bytes = static_cast<size_t>(width) * height * 4;
    popup_pixels_.assign(pixels, pixels + bytes);
    popup_width_ = width;
    popup_height_ = height;
    // Popups are composited on top of the whole frame, so keep it simple and
    // refresh everything; they are rare and short lived.
    dirty_ = {0, 0, static_cast<LONG>(view_width_), static_cast<LONG>(view_height_)};
    has_dirty_ = true;
    return;
  }

  const size_t bytes = static_cast<size_t>(width) * height * 4;
  const bool resized = width != view_width_ || height != view_height_;
  if (resized) {
    view_pixels_.assign(pixels, pixels + bytes);
    view_width_ = width;
    view_height_ = height;
    dirty_ = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    has_dirty_ = true;
    return;
  }

  for (const auto &rect : dirty_rects) {
    if (rect.width <= 0 || rect.height <= 0)
      continue;
    copy_rect(view_pixels_.data(), pixels, width, rect);

    const RECT as_rect = {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
    if (!has_dirty_) {
      dirty_ = as_rect;
      has_dirty_ = true;
    } else {
      dirty_.left = std::min(dirty_.left, as_rect.left);
      dirty_.top = std::min(dirty_.top, as_rect.top);
      dirty_.right = std::max(dirty_.right, as_rect.right);
      dirty_.bottom = std::max(dirty_.bottom, as_rect.bottom);
    }
  }
}

// ---------------------------------------------------------------------------
// CefLifeSpanHandler
// ---------------------------------------------------------------------------

void View::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  router_->AddHandler(this, false);
  {
    std::lock_guard lock(browser_mutex_);
    browser_ = browser;
  }
  logger::info("monaco: editor %d browser created", id_);

  if (closing_) {
    // destroy() beat the async creation; honour it now that we have a handle.
    browser->GetHost()->CloseBrowser(true);
  }
}

bool View::DoClose(CefRefPtr<CefBrowser> browser) { return false; }

void View::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  router_->OnBeforeClose(browser);
  router_->RemoveHandler(this);
  {
    std::lock_guard lock(browser_mutex_);
    browser_ = nullptr;
  }
  ready_ = false;
  closed_ = true;
  logger::info("monaco: editor %d browser closed", id_);
}

bool View::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString &,
                         const CefString &, cef_window_open_disposition_t, bool,
                         const CefPopupFeatures &, CefWindowInfo &, CefRefPtr<CefClient> &,
                         CefBrowserSettings &, CefRefPtr<CefDictionaryValue> &, bool *) {
  // An editor widget has no business opening windows over the game.
  return true;
}

// ---------------------------------------------------------------------------
// CefLoadHandler / CefDisplayHandler / CefRequestHandler
// ---------------------------------------------------------------------------

void View::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                       ErrorCode error_code, const CefString &error_text,
                       const CefString &failed_url) {
  if (error_code == ERR_ABORTED)
    return;
  logger::error("monaco: editor %d failed to load %s (%d: %s)", id_, failed_url.ToString().c_str(),
                error_code, error_text.ToString().c_str());
}

bool View::OnCursorChange(CefRefPtr<CefBrowser> browser, CefCursorHandle cursor,
                          cef_cursor_type_t type, const CefCursorInfo &custom_cursor_info) {
  cursor_ = static_cast<int>(type);
  // Returning true stops CEF from calling SetCursor behind the game's back;
  // draw() applies the shape through ImGui instead.
  return true;
}

bool View::OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level,
                            const CefString &message, const CefString &source, int line) {
  if (level >= LOGSEVERITY_ERROR) {
    logger::error("monaco: editor %d console: %s (%s:%d)", id_, message.ToString().c_str(),
                  source.ToString().c_str(), line);
  }
  return false;
}

bool View::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefRequest> request, bool user_gesture, bool is_redirect) {
  router_->OnBeforeBrowse(browser, frame);

  // Keep the widget pinned to our own origin; a stray link must not turn the
  // editor into a web browser.
  const std::string url = request->GetURL().ToString();
  if (url.rfind(cef_scheme::kOrigin, 0) != 0) {
    logger::warn("monaco: editor %d blocked navigation to %s", id_, url.c_str());
    return true;
  }
  return false;
}

void View::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus status,
                                     int error_code, const CefString &error_string) {
  logger::error("monaco: editor %d renderer terminated (status %d, %s)", id_,
                static_cast<int>(status), error_string.ToString().c_str());
  router_->OnRenderProcessTerminated(browser);
  ready_ = false;
  document_.reset_version();
  browser->Reload();
}

void View::OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                               CefRefPtr<CefContextMenuParams>, CefRefPtr<CefMenuModel> model) {
  // Chromium's native menu would be a real window floating over the game.
  model->Clear();
}

// ---------------------------------------------------------------------------
// Message router
// ---------------------------------------------------------------------------

bool View::OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t query_id,
                   const CefString &request, bool persistent, CefRefPtr<Callback> callback) {
  handle_query(request.ToString());
  callback->Success("");
  return true;
}

void View::handle_query(const std::string &request) {
  // Wire format is "<verb>:<payload>", chosen over JSON so that pushing a
  // multi-megabyte document does not cost an encode/decode round trip.
  const auto separator = request.find(':');
  const std::string verb = request.substr(0, separator);
  const std::string payload =
      separator == std::string::npos ? std::string() : request.substr(separator + 1);

  if (verb == "ready") {
    // The page loads Monaco but deliberately does not instantiate an editor
    // until we hand it the options and initial content.
    ready_ = true;
    document_.reset_version();
    run_js(bootstrap_script());
    logger::info("monaco: editor %d ready", id_);
    flush_pending_js();
    return;
  }

  if (verb == "text" || verb == "text-host") {
    // "<version>|<content>"
    const auto bar = payload.find('|');
    if (bar == std::string::npos)
      return;
    uint64_t version = std::strtoull(payload.substr(0, bar).c_str(), nullptr, 10);
    document_.on_editor_changed(payload.substr(bar + 1), version, verb == "text");
    return;
  }

  if (verb == "log") {
    logger::info("monaco: editor %d: %s", id_, payload.c_str());
    return;
  }
}

// ---------------------------------------------------------------------------
// Host -> editor
// ---------------------------------------------------------------------------

std::string View::bootstrap_script() const {
  std::lock_guard lock(options_mutex_);
  std::string script = "window.ljeMonaco.configure({";
  script += "value:" + js_string(document_.text());
  script += ",language:" + js_string(options_.language);
  script += ",theme:" + js_string(options_.theme);
  script += ",fontSize:" + std::to_string(options_.font_size);
  script += ",minimap:" + js_bool(options_.minimap);
  script += ",wordWrap:" + js_bool(options_.word_wrap);
  script += ",readOnly:" + js_bool(options_.read_only);
  script += ",lineNumbers:" + js_bool(options_.line_numbers);
  script += "});";
  return script;
}

// Records an option change so that a reload or a renderer crash restores it,
// then hands it to the live editor.
void View::update_option(const std::function<void(EditorOptions &)> &mutate,
                         const std::string &code) {
  {
    std::lock_guard lock(options_mutex_);
    mutate(options_);
  }
  execute_js(code);
}

void View::run_js(const std::string &code) {
  CefRefPtr<CefBrowser> browser;
  {
    std::lock_guard lock(browser_mutex_);
    browser = browser_;
  }
  if (!browser)
    return;

  cef_task::post_ui([browser, code] {
    if (auto frame = browser->GetMainFrame())
      frame->ExecuteJavaScript(code, frame->GetURL(), 0);
  });
}

void View::execute_js(const std::string &code) {
  if (!ready_) {
    std::lock_guard lock(pending_mutex_);
    pending_js_.push_back(code);
    return;
  }
  run_js(code);
}

void View::flush_pending_js() {
  std::vector<std::string> pending;
  {
    std::lock_guard lock(pending_mutex_);
    pending.swap(pending_js_);
  }
  for (const auto &code : pending)
    run_js(code);
}

void View::set_text(const std::string &text) {
  document_.on_host_write(text);
  execute_js("window.ljeMonaco.setText(" + js_string(text) + ");");
}

void View::set_language(const std::string &language) {
  update_option([&](EditorOptions &options) { options.language = language; },
                "window.ljeMonaco.setLanguage(" + js_string(language) + ");");
}

void View::set_theme(const std::string &theme) {
  update_option([&](EditorOptions &options) { options.theme = theme; },
                "window.ljeMonaco.setTheme(" + js_string(theme) + ");");
}

void View::set_read_only(bool read_only) {
  update_option([&](EditorOptions &options) { options.read_only = read_only; },
                "window.ljeMonaco.setOption('readOnly'," + js_bool(read_only) + ");");
}

void View::set_minimap(bool minimap) {
  update_option([&](EditorOptions &options) { options.minimap = minimap; },
                "window.ljeMonaco.setOption('minimap',{enabled:" + js_bool(minimap) + "});");
}

void View::set_word_wrap(bool word_wrap) {
  update_option([&](EditorOptions &options) { options.word_wrap = word_wrap; },
                std::string("window.ljeMonaco.setOption('wordWrap',") +
                    (word_wrap ? "'on'" : "'off'") + ");");
}

void View::set_font_size(int size) {
  update_option([&](EditorOptions &options) { options.font_size = size; },
                "window.ljeMonaco.setOption('fontSize'," + std::to_string(size) + ");");
}

void View::reload() {
  ready_ = false;
  document_.reset_version();

  CefRefPtr<CefBrowser> browser;
  {
    std::lock_guard lock(browser_mutex_);
    browser = browser_;
  }
  if (browser)
    cef_task::post_ui([browser] { browser->ReloadIgnoreCache(); });
}

void View::set_focus(bool focus) {
  if (focused_.exchange(focus) == focus)
    return;

  CefRefPtr<CefBrowser> browser;
  {
    std::lock_guard lock(browser_mutex_);
    browser = browser_;
  }
  if (browser)
    cef_task::post_ui([browser, focus] { browser->GetHost()->SetFocus(focus); });
}

void View::notify_resize() {
  CefRefPtr<CefBrowser> browser;
  {
    std::lock_guard lock(browser_mutex_);
    browser = browser_;
  }
  if (browser)
    cef_task::post_ui([browser] { browser->GetHost()->WasResized(); });
}

// ---------------------------------------------------------------------------
// Render thread
// ---------------------------------------------------------------------------

void View::upload(IDirect3DDevice9 *device) {
  std::lock_guard lock(frame_mutex_);
  if (view_width_ <= 0 || view_height_ <= 0)
    return;

  if (device != last_device_) {
    // A device reset that actually replaced the device leaves us holding
    // textures from the old one; everything has to be re-uploaded.
    last_device_ = device;
    has_dirty_ = true;
    dirty_ = {0, 0, static_cast<LONG>(view_width_), static_cast<LONG>(view_height_)};
  }

  if (!has_dirty_) {
    // Nothing new, but the retire list still needs to age.
    texture_.tick();
    return;
  }

  const uint8_t *source = view_pixels_.data();

  if (popup_visible_ && popup_width_ > 0 && popup_height_ > 0) {
    composite_ = view_pixels_;
    const int x0 = std::max(0, popup_rect_.x);
    const int y0 = std::max(0, popup_rect_.y);
    const int x1 = std::min(view_width_, popup_rect_.x + popup_width_);
    const int y1 = std::min(view_height_, popup_rect_.y + popup_height_);
    for (int y = y0; y < y1; ++y) {
      auto *dst = composite_.data() + (static_cast<size_t>(y) * view_width_ + x0) * 4;
      const auto *src =
          popup_pixels_.data() +
          (static_cast<size_t>(y - popup_rect_.y) * popup_width_ + (x0 - popup_rect_.x)) * 4;
      std::memcpy(dst, src, static_cast<size_t>(x1 - x0) * 4);
    }
    source = composite_.data();
  }

  if (!texture_.upload(device, source, view_width_, view_height_, dirty_))
    return;

  has_dirty_ = false;
  texture_handle_ = texture_.handle();
  texture_width_ = texture_.width();
  texture_height_ = texture_.height();
  content_width_ = texture_.content_width();
  content_height_ = texture_.content_height();
}

bool View::copy_frame(std::vector<uint8_t> &out, int &width, int &height) {
  std::lock_guard lock(frame_mutex_);
  if (view_width_ <= 0 || view_height_ <= 0 || view_pixels_.empty())
    return false;
  out = view_pixels_;
  width = view_width_;
  height = view_height_;
  return true;
}

void View::release_texture() {
  std::lock_guard lock(frame_mutex_);
  texture_.release();
  texture_handle_ = nullptr;
  texture_width_ = texture_height_ = 0;
  content_width_ = content_height_ = 0;
  has_dirty_ = true;
}

// ---------------------------------------------------------------------------
// Script thread: the ImGui widget
// ---------------------------------------------------------------------------

void View::resize(int width, int height) {
  const int wanted_w = std::max(1, width);
  const int wanted_h = std::max(1, height);
  const bool width_changed = width_.exchange(wanted_w) != wanted_w;
  const bool height_changed = height_.exchange(wanted_h) != wanted_h;
  if (width_changed || height_changed)
    notify_resize();
}

void View::draw(float width, float height) {
  const int wanted_w = std::max(1, static_cast<int>(width));
  const int wanted_h = std::max(1, static_cast<int>(height));
  resize(wanted_w, wanted_h);

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 size(static_cast<float>(wanted_w), static_cast<float>(wanted_h));

  ImGui::InvisibleButton(widget_id_.c_str(), size,
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                             ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();

  if (ImGui::IsItemActivated()) {
    set_focus(true);
  } else if (focused_ && !hovered &&
             (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
              ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
    set_focus(false);
  }

  auto *draw_list = ImGui::GetWindowDrawList();
  const ImVec2 bottom_right(origin.x + size.x, origin.y + size.y);

  auto *texture = texture_handle_.load();
  const int tex_w = texture_width_.load();
  const int tex_h = texture_height_.load();
  const int content_w = content_width_.load();
  const int content_h = content_height_.load();

  if (texture && tex_w > 0 && tex_h > 0 && content_w > 0 && content_h > 0) {
    const ImVec2 uv1(static_cast<float>(content_w) / static_cast<float>(tex_w),
                     static_cast<float>(content_h) / static_cast<float>(tex_h));
    draw_list->AddImage(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(texture)), origin,
                        bottom_right, ImVec2(0.0f, 0.0f), uv1);
  } else {
    draw_list->AddRectFilled(origin, bottom_right, IM_COL32(30, 30, 30, 255));
    const char *label = closed_ ? "Monaco editor closed" : "Loading Monaco Editor...";
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    draw_list->AddText(
        ImVec2(origin.x + (size.x - text_size.x) * 0.5f, origin.y + (size.y - text_size.y) * 0.5f),
        IM_COL32(160, 160, 160, 255), label);
  }

  if (focused_)
    draw_list->AddRect(origin, bottom_right, ImGui::GetColorU32(ImGuiCol_NavCursor));

  forward_mouse(origin.x, origin.y, hovered, held);

  if (hovered)
    ImGui::SetMouseCursor(input::to_imgui_cursor(cursor_.load()));

  if (focused_) {
    // The overlay's WndProc only swallows input the moment ImGui claims it, and
    // ImGui has no idea a browser is asking for the keyboard.
    ImGui::SetNextFrameWantCaptureKeyboard(true);
    forward_keyboard();
  }
}

void View::forward_mouse(float origin_x, float origin_y, bool hovered, bool held) {
  ImGuiIO &io = ImGui::GetIO();

  CefRefPtr<CefBrowser> browser;
  {
    std::lock_guard lock(browser_mutex_);
    browser = browser_;
  }
  if (!browser)
    return;

  auto host = browser->GetHost();
  const uint32_t mods = input::modifiers();

  CefMouseEvent event;
  event.x = static_cast<int>(io.MousePos.x - origin_x);
  event.y = static_cast<int>(io.MousePos.y - origin_y);
  event.modifiers = mods;

  const bool inside = hovered || held;
  const bool moved = io.MousePos.x != last_mouse_x_ || io.MousePos.y != last_mouse_y_;

  if (inside && (moved || !mouse_inside_)) {
    cef_task::post_ui([host, event] { host->SendMouseMoveEvent(event, false); });
    last_mouse_x_ = io.MousePos.x;
    last_mouse_y_ = io.MousePos.y;
  } else if (!inside && mouse_inside_) {
    cef_task::post_ui([host, event] { host->SendMouseMoveEvent(event, true); });
  }
  mouse_inside_ = inside;

  static constexpr cef_mouse_button_type_t kButtons[3] = {MBT_LEFT, MBT_RIGHT, MBT_MIDDLE};
  for (int button = 0; button < 3; ++button) {
    if (hovered && ImGui::IsMouseClicked(button)) {
      const int clicks = std::max(1, static_cast<int>(io.MouseClickedCount[button]));
      const auto type = kButtons[button];
      cef_task::post_ui(
          [host, event, type, clicks] { host->SendMouseClickEvent(event, type, false, clicks); });
      button_down_[button] = true;
    }
    // The release is delivered even when the pointer has left the widget so a
    // drag that ends outside still terminates the selection.
    if (button_down_[button] && ImGui::IsMouseReleased(button)) {
      const auto type = kButtons[button];
      cef_task::post_ui([host, event, type] { host->SendMouseClickEvent(event, type, true, 1); });
      button_down_[button] = false;
    }
  }

  if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)) {
    // Chromium wants raw WHEEL_DELTA units; ImGui already normalised them and
    // flipped the horizontal axis.
    const int delta_x = static_cast<int>(-io.MouseWheelH * WHEEL_DELTA);
    const int delta_y = static_cast<int>(io.MouseWheel * WHEEL_DELTA);
    cef_task::post_ui(
        [host, event, delta_x, delta_y] { host->SendMouseWheelEvent(event, delta_x, delta_y); });
  }
}

void View::forward_keyboard() {
  CefRefPtr<CefBrowser> browser;
  {
    std::lock_guard lock(browser_mutex_);
    browser = browser_;
  }
  if (!browser)
    return;

  std::vector<CefKeyEvent> events;
  input::collect_key_events(events);
  if (events.empty())
    return;

  auto host = browser->GetHost();
  cef_task::post_ui([host, events = std::move(events)] {
    for (const auto &event : events)
      host->SendKeyEvent(event);
  });
}

} // namespace monaco