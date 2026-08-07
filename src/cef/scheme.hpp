#pragma once
#include <string>

#include "include/cef_scheme.h"

// Monaco is served over a private "monaco://editor/" scheme instead of file://
// so that it is treated as a standard, secure origin. Web workers, fetch and
// module loading all work exactly as they do over http, which is what the
// official editor bundle expects.
namespace cef_scheme {

inline constexpr char kName[] = "monaco";
inline constexpr char kDomain[] = "editor";
inline constexpr char kOrigin[] = "monaco://editor";

// Called from CefApp::OnRegisterCustomSchemes in every process.
void register_scheme(CefRawPtr<CefSchemeRegistrar> registrar);

// Called once in the browser process after CEF is initialized. |assets_dir| is
// the directory the scheme serves files from (UTF-8, no trailing separator).
void register_handler_factory(const std::string &assets_dir);

} // namespace cef_scheme