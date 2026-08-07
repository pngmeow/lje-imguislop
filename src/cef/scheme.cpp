#include "scheme.hpp"

#include <algorithm>
#include <vector>

#include "include/cef_parser.h"
#include "include/cef_stream.h"
#include "include/wrapper/cef_stream_resource_handler.h"

namespace cef_scheme {
namespace {

// Splits an URL path into components, rejecting anything that could escape the
// asset directory ("..", absolute paths, drive letters, alternate streams).
bool split_path(const std::string &path, std::vector<std::string> &out) {
  std::string component;
  auto flush = [&]() -> bool {
    if (component.empty() || component == ".")
      return true;
    if (component == ".." || component.find(':') != std::string::npos)
      return false;
    out.push_back(component);
    component.clear();
    return true;
  };

  for (char c : path) {
    if (c == '/' || c == '\\') {
      if (!flush())
        return false;
    } else {
      component.push_back(c);
    }
  }
  return flush();
}

std::string extension_of(const std::string &name) {
  auto dot = name.rfind('.');
  if (dot == std::string::npos)
    return {};
  std::string ext = name.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

class FileSchemeHandlerFactory final : public CefSchemeHandlerFactory {
public:
  explicit FileSchemeHandlerFactory(std::string root) : root_(std::move(root)) {}

  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                       const CefString &scheme_name,
                                       CefRefPtr<CefRequest> request) override {
    CefURLParts parts;
    if (!CefParseURL(request->GetURL(), parts))
      return nullptr;

    std::string path =
        CefURIDecode(CefString(&parts.path), false,
                     static_cast<cef_uri_unescape_rule_t>(
                         UU_NORMAL | UU_SPACES | UU_URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS))
            .ToString();

    std::vector<std::string> components;
    if (!split_path(path, components))
      return nullptr;

    if (components.empty())
      components.emplace_back("index.html");

    std::string file = root_;
    for (const auto &component : components) {
      file += '\\';
      file += component;
    }

    auto stream = CefStreamReader::CreateForFile(file);
    if (!stream)
      return nullptr;

    CefString mime = CefGetMimeType(extension_of(components.back()));
    if (mime.empty())
      mime = "application/octet-stream";

    return new CefStreamResourceHandler(mime, stream);
  }

private:
  std::string root_;
  IMPLEMENT_REFCOUNTING(FileSchemeHandlerFactory);
};

} // namespace

void register_scheme(CefRawPtr<CefSchemeRegistrar> registrar) {
  registrar->AddCustomScheme(kName, CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
                                        CEF_SCHEME_OPTION_CORS_ENABLED |
                                        CEF_SCHEME_OPTION_FETCH_ENABLED);
}

void register_handler_factory(const std::string &assets_dir) {
  CefRegisterSchemeHandlerFactory(kName, kDomain, new FileSchemeHandlerFactory(assets_dir));
}

} // namespace cef_scheme