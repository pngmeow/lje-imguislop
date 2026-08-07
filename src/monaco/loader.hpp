#pragma once

namespace monaco::loader {

// Loads our private copy of the framework (LJE_CEF_MODULE_NAME, a renamed
// libcef.dll - see tools/cef_rename.cpp) from lje-imgui/cef. Must succeed
// before any CEF symbol is touched: libcef.dll is delay loaded precisely so
// that this can happen at runtime from a directory the host process knows
// nothing about.
bool load();

// Same, but from the calling module's own directory. The sub-process
// executable is deployed inside lje-imgui/cef and so sits next to the
// framework rather than above it.
bool load_sibling();

bool loaded();

} // namespace monaco::loader
