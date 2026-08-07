#pragma once
#include <Windows.h>
#include <d3d9.h>
#include <memory>
#include <thread>
#include <atomic>
#include "hook.hpp"

class Overlay {
public:
  enum class State { Uninitialized, Waiting, Ready, Shutdown };

  using EndScene_t = HRESULT(__stdcall*)(IDirect3DDevice9 *);
  using Reset_t = HRESULT(__stdcall*)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);

  static std::shared_ptr<Overlay> get();
  static void create();
  static void destroy();

  void init_async();
  void shutdown();

  void new_frame();
  void render();
  void render_draw_data();
  void on_reset();
  void on_reset_after(IDirect3DDevice9 *dev);

  bool is_visible() const { return visible_; }
  // Whether an ImGui context exists, i.e. whether ImGui::GetIO() is safe.
  bool imgui_ready() const { return imgui_initialized_; }
  void set_visible(bool v) { visible_ = v; }
  void toggle_visible() { visible_ = !visible_; }
  void set_toggle_bind(WPARAM vk) { toggle_bind_ = vk; }

  State state() const { return state_; }

  Hook<EndScene_t> &endscene_hook() { return endscene_; }
  Hook<Reset_t> &reset_hook() { return reset_; }

private:
  Overlay() = default;

  void init_thread_func();
  bool try_init();
  bool init_imgui(IDirect3DDevice9 *dev);
  void shutdown_imgui();
  HWND get_device_window(IDirect3DDevice9 *dev);

  static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  static std::shared_ptr<Overlay> instance_;

  std::atomic<State> state_ = State::Uninitialized;
  std::atomic<bool> visible_ = true;
  std::thread init_thread_;

  Hook<EndScene_t> endscene_;
  Hook<Reset_t> reset_;
  IDirect3DDevice9 *device_ = nullptr;

  HWND hwnd_ = nullptr;
  WNDPROC original_wndproc_ = nullptr;
  bool imgui_initialized_ = false;

  // Frames are split, so Lua will prepare a frame and queue its draw data, and then
  // Overlay will render it in the next call to EndScene, so it is undetectable/grabbable.
  bool frame_started_ = false;
  bool frame_ready_ = false; // Draw data ready to render
  WPARAM toggle_bind_ = VK_INSERT;
};
