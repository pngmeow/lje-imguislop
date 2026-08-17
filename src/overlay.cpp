#include "overlay.hpp"
#include "log.hpp"
#include "api/imnodes_api.hpp"
#include "monaco/monaco.hpp"
#include <im_anim.h>
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>
#include <imnodes.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
constexpr size_t ENDSCENE_VTABLE_INDEX = 42;
constexpr size_t RESET_VTABLE_INDEX = 16;
}

std::shared_ptr<Overlay> Overlay::instance_ = nullptr;

std::shared_ptr<Overlay> Overlay::get() {
  return instance_;
}

void Overlay::create() {
  logger::info("Overlay::create()");
  instance_ = std::shared_ptr<Overlay>(new Overlay());
}

void Overlay::destroy() {
  logger::info("Overlay::destroy()");
  if (instance_) {
    instance_->shutdown();
    instance_.reset();
  }
}

void Overlay::init_async() {
  logger::info("Overlay::init_async() - spawning init thread");
  state_ = State::Waiting;
  init_thread_ = std::thread(&Overlay::init_thread_func, this);
}

void Overlay::init_thread_func() {
  logger::info("Overlay::init_thread_func() - started");

  while (state_ == State::Waiting) {
    if (try_init()) {
      logger::info("Overlay::init_thread_func() - init succeeded");
      return;
    }
    Sleep(100);
  }

  logger::info("Overlay::init_thread_func() - aborted");
}

bool Overlay::try_init() {
  logger::info("Overlay::try_init() - attempt");

  if (!hook::init()) {
    logger::error("Failed to initialize MinHook");
    return false;
  }

  // Create temp window for dummy device
  WNDCLASSEXA wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = DefWindowProcA;
  wc.hInstance = GetModuleHandleA(nullptr);
  wc.lpszClassName = "lje_imgui_dummy";
  RegisterClassExA(&wc);

  HWND temp_hwnd = CreateWindowExA(
      0, wc.lpszClassName, "", WS_OVERLAPPEDWINDOW,
      0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr
      );

  if (!temp_hwnd) {
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    hook::shutdown();
    return false;
  }

  auto d3d = Direct3DCreate9(D3D_SDK_VERSION);
  if (!d3d) {
    DestroyWindow(temp_hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    hook::shutdown();
    return false;
  }

  D3DPRESENT_PARAMETERS pp = {};
  pp.Windowed = TRUE;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.hDeviceWindow = temp_hwnd;
  pp.BackBufferFormat = D3DFMT_UNKNOWN;

  IDirect3DDevice9 *dummy = nullptr;
  auto hr = d3d->CreateDevice(
      D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, temp_hwnd,
      D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &pp, &dummy
      );

  if (FAILED(hr) || !dummy) {
    d3d->Release();
    DestroyWindow(temp_hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    hook::shutdown();
    return false;
  }

  logger::info("Overlay::try_init() - dummy device created");

  auto vtable = *reinterpret_cast<uintptr_t **>(dummy);
  auto endscene_addr = vtable[ENDSCENE_VTABLE_INDEX];
  auto reset_addr = vtable[RESET_VTABLE_INDEX];
  logger::info("Overlay::try_init() - endscene=%p reset=%p", endscene_addr, reset_addr);

  dummy->Release();
  d3d->Release();
  DestroyWindow(temp_hwnd);
  UnregisterClassA(wc.lpszClassName, wc.hInstance);

  // EndScene hook - capture device pointer and render ImGui
  if (!endscene_.create(endscene_addr, [](IDirect3DDevice9 *dev) -> HRESULT {
    auto overlay = Overlay::get();
    overlay->device_ = dev;

    // Init imgui on first EndScene
    if (overlay->state_ == State::Ready && !overlay->imgui_initialized_) {
      overlay->init_imgui(dev);
    }

    // Render ImGui draw data
    overlay->render_draw_data();

    return overlay->endscene_hook().call(dev);
  })) {
    logger::error("Failed to create EndScene hook");
    hook::shutdown();
    return false;
  }

  // Reset hook
  if (!reset_.create(reset_addr, [](IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp) -> HRESULT {
    auto overlay = Overlay::get();
    overlay->on_reset();
    auto hr = overlay->reset_hook().call(dev, pp);
    if (SUCCEEDED(hr)) {
      overlay->on_reset_after(dev);
    }
    return hr;
  })) {
    logger::error("Failed to create Reset hook");
    endscene_.remove();
    hook::shutdown();
    return false;
  }

  endscene_.enable();
  reset_.enable();

  state_ = State::Ready;
  logger::info("Overlay::try_init() - ready");
  return true;
}

void Overlay::shutdown() {
  logger::info("Overlay::shutdown() - start");

  auto prev_state = state_.exchange(State::Shutdown);
  if (prev_state == State::Shutdown)
    return;

  if (init_thread_.joinable()) {
    logger::info("Overlay::shutdown() - joining init thread");
    init_thread_.join();
  }

  if (prev_state == State::Ready) {
    logger::info("Overlay::shutdown() - releasing monaco textures");
    monaco::release_textures();

    logger::info("Overlay::shutdown() - shutdown_imgui");
    shutdown_imgui();

    logger::info("Overlay::shutdown() - disabling hooks");
    endscene_.disable();
    endscene_.remove();
    reset_.disable();
    reset_.remove();

    logger::info("Overlay::shutdown() - hook::shutdown");
    hook::shutdown();
  }

  logger::info("Overlay::shutdown() - done");
}

void Overlay::new_frame() {
  if (state_ != State::Ready || !imgui_initialized_)
    return;

  if (frame_started_)
    return; // Already in a frame

  // Nav state is computed inside NewFrame, so this has to happen before it.
  monaco::update_imgui_nav();

  ImGui_ImplDX9_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  // ImAnim requires both calls every frame, after NewFrame() and before any
  // tween/clip call, or animations never advance. Scripts get them for free.
  iam_update_begin_frame();
  iam_clip_update(ImGui::GetIO().DeltaTime);

  frame_started_ = true;
}

void Overlay::render() {
  if (state_ != State::Ready || !imgui_initialized_ || !frame_started_)
    return;

  frame_started_ = false;

  // Finalize draw data - actual D3D9 rendering happens in EndScene
  ImGui::EndFrame();
  ImGui::Render();
  frame_ready_ = true;
}

void Overlay::render_draw_data() {
  if (!frame_ready_ || !device_)
    return;

  frame_ready_ = false;

  auto draw_data = ImGui::GetDrawData();
  if (!draw_data)
    return;

  // Off-screen browser frames arrive on the CEF UI thread but may only reach
  // the device from here, and they have to land before the draw data that
  // references their textures is submitted.
  monaco::upload_textures(device_);

  // Save sRGB state
  DWORD srgb_write, srgb_texture;
  device_->GetRenderState(D3DRS_SRGBWRITEENABLE, &srgb_write);
  device_->GetSamplerState(0, D3DSAMP_SRGBTEXTURE, &srgb_texture);

  // Disable sRGB for ImGui
  device_->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
  device_->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);

  ImGui_ImplDX9_RenderDrawData(draw_data);

  // Restore sRGB state
  device_->SetRenderState(D3DRS_SRGBWRITEENABLE, srgb_write);
  device_->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, srgb_texture);
}

void Overlay::on_reset() {
  logger::info("Overlay::on_reset()");
  frame_started_ = false;
  // Editor frames live in the default pool, which Reset invalidates. Dropping
  // them here re-arms a full upload on the next EndScene.
  monaco::release_textures();
  if (imgui_initialized_) {
    ImGui_ImplDX9_InvalidateDeviceObjects();
  }
}

void Overlay::on_reset_after(IDirect3DDevice9 *dev) {
  logger::info("Overlay::on_reset_after()");
  if (imgui_initialized_) {
    ImGui_ImplDX9_CreateDeviceObjects();
  }
}

bool Overlay::init_imgui(IDirect3DDevice9 *dev) {
  logger::info("Overlay::init_imgui() - start");

  hwnd_ = get_device_window(dev);
  if (!hwnd_) {
    logger::error("Failed to get device window");
    return false;
  }
  logger::info("Overlay::init_imgui() - hwnd=%p", hwnd_);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  imnodes_api::init();

  auto &io = ImGui::GetIO();
  io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;

  ImGui::StyleColorsDark();

  auto &style = ImGui::GetStyle();
  style.WindowBorderSize = 1.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;

  if (!ImGui_ImplWin32_Init(hwnd_)) {
    logger::error("Failed to init ImGui Win32");
    return false;
  }

  if (!ImGui_ImplDX9_Init(dev)) {
    logger::error("Failed to init ImGui DX9");
    return false;
  }

  original_wndproc_ = reinterpret_cast<WNDPROC>(
    SetWindowLongPtr(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wndproc))
  );
  logger::info("Overlay::init_imgui() - original_wndproc=%p", original_wndproc_);

  imgui_initialized_ = true;
  logger::info("Overlay::init_imgui() - done");
  return true;
}

void Overlay::shutdown_imgui() {
  logger::info("Overlay::shutdown_imgui() - start");
  if (!imgui_initialized_)
    return;

  if (hwnd_ && original_wndproc_) {
    SetWindowLongPtr(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_wndproc_));
  }

  ImGui_ImplDX9_Shutdown();
  ImGui_ImplWin32_Shutdown();
  imnodes_api::shutdown();
  ImGui::DestroyContext();

  imgui_initialized_ = false;
  logger::info("Overlay::shutdown_imgui() - done");
}

HWND Overlay::get_device_window(IDirect3DDevice9 *dev) {
  D3DDEVICE_CREATION_PARAMETERS params;
  if (SUCCEEDED(dev->GetCreationParameters(&params))) {
    return params.hFocusWindow;
  }
  return nullptr;
}

LRESULT CALLBACK Overlay::wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  auto overlay = Overlay::get();
  if (!overlay) {
    return DefWindowProc(hwnd, msg, wparam, lparam);
  }

  // Toggle visibility. While the overlay is up and something inside it owns the
  // keyboard - a focused Monaco editor, say - the key belongs to that text
  // field, otherwise typing the bind hides the overlay out from under you.
  // Clicking outside the editor blurs it and hands the bind back.
  if (msg == WM_KEYDOWN && wparam == overlay->toggle_bind_) {
    const bool typing = overlay->is_visible() && overlay->imgui_ready() &&
                        ImGui::GetIO().WantCaptureKeyboard;
    if (!typing)
      overlay->toggle_visible();
  }

  // When visible, let ImGui handle input first
  if (overlay->is_visible()) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
      return true;
    }

    // Block input from reaching game when overlay is open
    switch (msg) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
      if (ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard) {
        return true;
      }
      break;
    }
  }

  return CallWindowProc(overlay->original_wndproc_, hwnd, msg, wparam, lparam);
}
