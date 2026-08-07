#include "app.hpp"

#include "scheme.hpp"

MonacoCefApp::MonacoCefApp(std::string assets_dir) : assets_dir_(std::move(assets_dir)) {}

CefMessageRouterConfig MonacoCefApp::router_config() {
  CefMessageRouterConfig config;
  config.js_query_function = "ljeMonacoQuery";
  config.js_cancel_function = "ljeMonacoQueryCancel";
  return config;
}

void MonacoCefApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  cef_scheme::register_scheme(registrar);
}

void MonacoCefApp::OnBeforeCommandLineProcessing(const CefString &process_type,
                                                 CefRefPtr<CefCommandLine> command_line) {
  if (!process_type.empty())
    return;

  // We are a guest inside somebody else's process and only ever render an
  // editor into a texture: no need for background throttling heuristics or the
  // usual first-run/profile chrome.
  command_line->AppendSwitch("disable-background-timer-throttling");
  command_line->AppendSwitch("disable-renderer-backgrounding");
  command_line->AppendSwitch("disable-backgrounding-occluded-windows");
  command_line->AppendSwitchWithValue("disable-features", "Translate,MediaRouter");
  command_line->AppendSwitch("no-first-run");
}

void MonacoCefApp::OnContextInitialized() {
  if (!assets_dir_.empty())
    cef_scheme::register_handler_factory(assets_dir_);
}

void MonacoCefApp::OnWebKitInitialized() {
  renderer_router_ = CefMessageRouterRendererSide::Create(router_config());
}

void MonacoCefApp::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                    CefRefPtr<CefV8Context> context) {
  if (renderer_router_)
    renderer_router_->OnContextCreated(browser, frame, context);
}

void MonacoCefApp::OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                     CefRefPtr<CefV8Context> context) {
  if (renderer_router_)
    renderer_router_->OnContextReleased(browser, frame, context);
}

bool MonacoCefApp::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                            CefRefPtr<CefFrame> frame, CefProcessId source_process,
                                            CefRefPtr<CefProcessMessage> message) {
  return renderer_router_ &&
         renderer_router_->OnProcessMessageReceived(browser, frame, source_process, message);
}