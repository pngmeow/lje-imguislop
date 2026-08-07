#pragma once
#include <d3d9.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "include/cef_client.h"
#include "include/wrapper/cef_message_router.h"

#include "document.hpp"
#include "monaco.hpp"
#include "texture.hpp"

namespace monaco {

// One windowless browser running the official Monaco Editor, presented as an
// ImGui widget.
//
// Three threads meet here and the split is deliberate:
//   * the CEF UI thread owns the browser and delivers OnPaint,
//   * the script thread runs draw() between imgui.new_frame()/render(),
//   * the D3D9 render thread runs upload() from inside the EndScene hook.
// Frames cross from CEF to D3D through a mutex-guarded staging buffer; nothing
// else is shared without an atomic.
class View : public CefClient,
             public CefRenderHandler,
             public CefLifeSpanHandler,
             public CefLoadHandler,
             public CefDisplayHandler,
             public CefRequestHandler,
             public CefContextMenuHandler,
             public CefMessageRouterBrowserSide::Handler {
public:
  View(EditorId id, EditorOptions options);
  ~View() override;

  void create_browser();
  void close();
  bool closing() const { return closing_; }
  bool closed() const { return closed_; }
  bool ready() const { return ready_; }

  EditorId id() const { return id_; }
  Document &document() { return document_; }

  // --- script thread ---------------------------------------------------
  void draw(float width, float height);
  void resize(int width, int height);

  void set_text(const std::string &text);
  void set_language(const std::string &language);
  void set_theme(const std::string &theme);
  void set_read_only(bool read_only);
  void set_minimap(bool minimap);
  void set_word_wrap(bool word_wrap);
  void set_font_size(int size);
  void execute_js(const std::string &code);
  void reload();

  void set_focus(bool focus);
  bool focused() const { return focused_; }

  // --- D3D9 render thread ----------------------------------------------
  void upload(IDirect3DDevice9 *device);
  void release_texture();

  // The surface the widget draws from, or null before the first upload.
  IDirect3DTexture9 *texture_handle() const { return texture_handle_; }

  // Diagnostics: number of frames the browser has painted, and a copy of the
  // most recent one as tightly packed BGRA. Callable from any thread.
  uint64_t frames_painted() const { return frames_painted_; }
  bool copy_frame(std::vector<uint8_t> &out, int &width, int &height);

  // --- CefClient --------------------------------------------------------
  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

  // --- CefRenderHandler -------------------------------------------------
  void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect) override;
  bool GetScreenInfo(CefRefPtr<CefBrowser> browser, CefScreenInfo &screen_info) override;
  void OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) override;
  void OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect &rect) override;
  void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList &dirty_rects,
               const void *buffer, int width, int height) override;

  // --- CefLifeSpanHandler -----------------------------------------------
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popup_id,
                     const CefString &target_url, const CefString &target_frame_name,
                     cef_window_open_disposition_t target_disposition, bool user_gesture,
                     const CefPopupFeatures &popup_features, CefWindowInfo &window_info,
                     CefRefPtr<CefClient> &client, CefBrowserSettings &settings,
                     CefRefPtr<CefDictionaryValue> &extra_info,
                     bool *no_javascript_access) override;

  // --- CefLoadHandler ---------------------------------------------------
  void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode error_code,
                   const CefString &error_text, const CefString &failed_url) override;

  // --- CefDisplayHandler ------------------------------------------------
  bool OnCursorChange(CefRefPtr<CefBrowser> browser, CefCursorHandle cursor, cef_cursor_type_t type,
                      const CefCursorInfo &custom_cursor_info) override;
  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level,
                        const CefString &message, const CefString &source, int line) override;

  // --- CefRequestHandler ------------------------------------------------
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request, bool user_gesture, bool is_redirect) override;
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus status,
                                 int error_code, const CefString &error_string) override;

  // --- CefContextMenuHandler --------------------------------------------
  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override;

  // --- CefMessageRouterBrowserSide::Handler ------------------------------
  bool OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t query_id,
               const CefString &request, bool persistent, CefRefPtr<Callback> callback) override;

private:
  void update_option(const std::function<void(EditorOptions &)> &mutate, const std::string &code);
  void run_js(const std::string &code);
  void flush_pending_js();
  void notify_resize();
  void forward_mouse(float origin_x, float origin_y, bool hovered, bool held);
  void forward_keyboard();
  void handle_query(const std::string &request);
  std::string bootstrap_script() const;

  const EditorId id_;
  const std::string widget_id_;
  // Written by the script thread, read by the CEF UI thread when the page
  // reports itself ready and asks to be configured.
  mutable std::mutex options_mutex_;
  EditorOptions options_;
  Document document_;

  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  mutable std::mutex browser_mutex_;

  std::atomic<bool> ready_{false};
  std::atomic<bool> closing_{false};
  std::atomic<bool> closed_{false};
  std::atomic<bool> focused_{false};
  std::atomic<int> cursor_{0};
  std::atomic<uint64_t> frames_painted_{0};

  // Requested size, written by draw() and read by GetViewRect().
  std::atomic<int> width_;
  std::atomic<int> height_;

  std::mutex pending_mutex_;
  std::vector<std::string> pending_js_;

  // Staging frame shared between the CEF UI thread and the render thread.
  std::mutex frame_mutex_;
  std::vector<uint8_t> view_pixels_;
  int view_width_ = 0;
  int view_height_ = 0;
  std::vector<uint8_t> popup_pixels_;
  int popup_width_ = 0;
  int popup_height_ = 0;
  CefRect popup_rect_;
  bool popup_visible_ = false;
  std::vector<uint8_t> composite_;
  RECT dirty_ = {};
  bool has_dirty_ = false;

  Texture texture_;
  IDirect3DDevice9 *last_device_ = nullptr;
  // Snapshot of texture_ for the script thread; retired textures stay alive
  // long enough that a stale handle here can still be drawn safely.
  std::atomic<IDirect3DTexture9 *> texture_handle_{nullptr};
  std::atomic<int> texture_width_{0};
  std::atomic<int> texture_height_{0};
  std::atomic<int> content_width_{0};
  std::atomic<int> content_height_{0};

  // Mouse state tracked across frames so drags and leave events behave.
  bool mouse_inside_ = false;
  bool button_down_[3] = {false, false, false};
  float last_mouse_x_ = -1.0f;
  float last_mouse_y_ = -1.0f;

  IMPLEMENT_REFCOUNTING(View);
  DISALLOW_COPY_AND_ASSIGN(View);
};

} // namespace monaco