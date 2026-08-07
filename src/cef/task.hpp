#pragma once
#include <functional>
#include <utility>

#include "include/cef_task.h"

// CefBrowserHost input/lifetime methods must run on the CEF UI thread, which is
// never the D3D9 render thread or the Lua thread. Small closure task so callers
// can just say cef_task::post_ui([...] { ... }).
class FunctionTask final : public CefTask {
public:
  explicit FunctionTask(std::function<void()> fn) : fn_(std::move(fn)) {}

  void Execute() override {
    if (fn_)
      fn_();
  }

private:
  std::function<void()> fn_;
  IMPLEMENT_REFCOUNTING(FunctionTask);
};

namespace cef_task {

inline void post(CefThreadId thread, std::function<void()> fn) {
  if (CefCurrentlyOn(thread)) {
    fn();
    return;
  }
  CefPostTask(thread, new FunctionTask(std::move(fn)));
}

inline void post_ui(std::function<void()> fn) { post(TID_UI, std::move(fn)); }

} // namespace cef_task