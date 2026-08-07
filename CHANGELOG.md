# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [0.2.0] - 2026-08-07

### Added

- Monaco Editor widget backed by the Chromium Embedded Framework. The official
  `monaco-editor` bundle runs unmodified inside a windowless CEF browser, is
  composited into the overlay as a Dear ImGui image, and is exposed to Lua as
  the `monaco` table.
- CEF 151 (Chromium 151) and `monaco-editor` 0.56.0 are fetched and unpacked by
  CMake on first configure; nothing has to be installed by hand.
- `lje-imgui-cef.exe` sub-process executable, deployed with the CEF runtime into
  the `lje-imgui/` payload folder next to the DLL.
- `LJE_IMGUI_BUILD_TESTS` CMake option building `monaco-smoketest`, a headless
  end-to-end check of the editor pipeline.

## [0.1.0] - 2026-02-04

### Added

- DirectX 9 hook-based ImGui overlay rendering
- ImGui and imnodes library integration
- LJE SDK module system integration
- Lua-exposed ImGui and imnodes API bindings
