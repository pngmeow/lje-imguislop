#pragma once
#include <string>

#include "include/cef_app.h"
#include "include/wrapper/cef_message_router.h"

// CefApp shared by the browser process (lje-imgui.dll, injected into the host)
// and the sub-processes (lje-imgui-cef.exe). Custom scheme registration has to
// happen identically in both, so the same class is compiled into both binaries.
//
// |assets_dir| is only meaningful in the browser process; sub-processes pass an
// empty string.
class MonacoCefApp final : public CefApp,
                           public CefBrowserProcessHandler,
                           public CefRenderProcessHandler {
public:
  explicit MonacoCefApp(std::string assets_dir = {});

  // CefApp
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;
  void OnBeforeCommandLineProcessing(const CefString &process_type,
                                     CefRefPtr<CefCommandLine> command_line) override;

  // CefBrowserProcessHandler
  void OnContextInitialized() override;

  // CefRenderProcessHandler
  void OnWebKitInitialized() override;
  void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override;
  void OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

  // Shared by both sides of the message router so the JS function names match.
  static CefMessageRouterConfig router_config();

private:
  std::string assets_dir_;
  CefRefPtr<CefMessageRouterRendererSide> renderer_router_;

  IMPLEMENT_REFCOUNTING(MonacoCefApp);
  DISALLOW_COPY_AND_ASSIGN(MonacoCefApp);
};