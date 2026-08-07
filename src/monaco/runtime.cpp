#include "runtime.hpp"

#include <windows.h>

#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

#include "include/cef_app.h"

#include "../cef/app.hpp"
#include "../log.hpp"
#include "loader.hpp"
#include "paths.hpp"

namespace monaco::runtime {
namespace {

bool file_exists(const std::wstring &path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// The runtime owns a thread of its own because CefInitialize and CefShutdown
// must be paired on the same thread, and neither the game's main thread nor the
// script thread is ours to block. With multi_threaded_message_loop CEF spins up
// its own UI thread, so this one just parks until shutdown.
class Runtime {
public:
  static Runtime &get() {
    static Runtime instance;
    return instance;
  }

  bool start() {
    std::unique_lock lock(mutex_);
    if (state_ == State::Running)
      return true;
    if (state_ == State::Failed)
      return false;

    if (state_ == State::Idle) {
      state_ = State::Starting;
      thread_ = std::thread(&Runtime::thread_main, this);
    }

    cv_.wait(lock, [this] { return state_ != State::Starting; });
    return state_ == State::Running;
  }

  void stop() {
    {
      std::unique_lock lock(mutex_);
      if (state_ == State::Idle)
        return;
      cv_.wait(lock, [this] { return state_ != State::Starting; });
      stop_requested_ = true;
    }
    cv_.notify_all();

    if (thread_.joinable())
      thread_.join();
  }

  bool running() {
    std::lock_guard lock(mutex_);
    return state_ == State::Running;
  }

  std::string error() {
    std::lock_guard lock(mutex_);
    return error_;
  }

private:
  enum class State { Idle, Starting, Running, Failed, Stopped };

  void fail(std::string message) {
    logger::error("monaco: %s", message.c_str());
    {
      std::lock_guard lock(mutex_);
      error_ = std::move(message);
      state_ = State::Failed;
    }
    cv_.notify_all();
  }

  bool check_payload() {
    struct Required {
      std::wstring path;
      const char *what;
    };
    const Required required[] = {
        {paths::cef_dir() + L"\\" LJE_CEF_MODULE_NAME, "the CEF framework"},
        {paths::cef_dir() + L"\\lje-imgui-cef.exe", "the CEF sub-process executable"},
        {paths::cef_dir() + L"\\resources.pak", "the CEF resource pak"},
        {paths::cef_dir() + L"\\icudtl.dat", "the ICU data file"},
        {paths::assets_dir() + L"\\index.html", "the Monaco host page"},
        {paths::assets_dir() + L"\\vs\\loader.js", "the Monaco editor bundle"},
    };

    for (const auto &entry : required) {
      if (!file_exists(entry.path)) {
        char message[512];
        std::snprintf(message, sizeof(message),
                      "missing %s. Expected it next to lje-imgui.dll at %S", entry.what,
                      entry.path.c_str());
        fail(message);
        return false;
      }
    }
    return true;
  }

  void thread_main() {
    if (!check_payload())
      return;

    if (!loader::load()) {
      fail("could not load the CEF framework from the module payload directory");
      return;
    }

    paths::ensure_directory(paths::cache_dir());

    CefSettings settings;
    settings.no_sandbox = true;
    // The host process owns the message loop and we are not allowed to block
    // it, so CEF runs its UI thread itself.
    settings.multi_threaded_message_loop = true;
    settings.windowless_rendering_enabled = true;
    settings.background_color = 0xFF1E1E1E;
    settings.log_severity = LOGSEVERITY_WARNING;

    // Nothing is discoverable relative to the host executable, so every path
    // has to be spelled out.
    CefString(&settings.browser_subprocess_path) =
        paths::to_utf8(paths::cef_dir() + L"\\lje-imgui-cef.exe");
    CefString(&settings.resources_dir_path) = paths::to_utf8(paths::cef_dir());
    CefString(&settings.locales_dir_path) = paths::to_utf8(paths::cef_dir() + L"\\locales");
    CefString(&settings.root_cache_path) = paths::to_utf8(paths::cache_dir());
    CefString(&settings.cache_path) = paths::to_utf8(paths::cache_dir() + L"\\profile");
    CefString(&settings.log_file) = paths::to_utf8(paths::cache_dir() + L"\\cef.log");

    CefMainArgs main_args(GetModuleHandleW(nullptr));
    CefRefPtr<CefApp> app(new MonacoCefApp(paths::to_utf8(paths::assets_dir())));

    if (!CefInitialize(main_args, settings, app, nullptr)) {
      char message[256];
      std::snprintf(message, sizeof(message),
                    "CefInitialize failed with exit code %d. The host process may already "
                    "host its own Chromium instance.",
                    CefGetExitCode());
      fail(message);
      return;
    }

    logger::info("monaco: CEF initialized");
    {
      std::lock_guard lock(mutex_);
      state_ = State::Running;
    }
    cv_.notify_all();

    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] { return stop_requested_; });
    }

    logger::info("monaco: shutting CEF down");
    CefShutdown();

    {
      std::lock_guard lock(mutex_);
      state_ = State::Stopped;
    }
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::thread thread_;
  State state_ = State::Idle;
  bool stop_requested_ = false;
  std::string error_;
};

} // namespace

bool start() { return Runtime::get().start(); }

void stop() { Runtime::get().stop(); }

bool running() { return Runtime::get().running(); }

std::string error() { return Runtime::get().error(); }

} // namespace monaco::runtime