// Headless check of the Monaco/CEF pipeline.
//
// The module itself only ever runs inside a host process we do not control, so
// this executable exercises everything below the game: loading our private
// libcef.dll, bringing up the browser process, serving the editor over the
// monaco:// scheme, off-screen rendering, the message router round trip that
// keeps the C++ document mirror in sync, and the real ImGui input path driven
// through Dear ImGui's null backend.
//
// Build with -DLJE_IMGUI_BUILD_TESTS=ON and run it from the build directory,
// where the lje-imgui/ payload folder sits next to it.

#include <windows.h>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_impl_null.h>

#include "monaco/monaco.hpp"
#include "monaco/runtime.hpp"
#include "monaco/texture.hpp"
#include "monaco/view.hpp"

namespace {

int g_failures = 0;

void check(bool condition, const char *what) {
  std::printf("[%s] %s\n", condition ? " OK " : "FAIL", what);
  if (!condition)
    ++g_failures;
}

// Spins until |predicate| holds or the budget runs out. CEF drives its own UI
// thread, so there is no message loop to pump here.
template <typename Predicate> bool wait_for(Predicate predicate, int timeout_ms, const char *what) {
  const DWORD deadline = GetTickCount() + static_cast<DWORD>(timeout_ms);
  while (GetTickCount() < deadline) {
    if (predicate())
      return true;
    Sleep(20);
  }
  std::printf("       timed out waiting for %s\n", what);
  return false;
}

// A blank frame would still count as "painted", so look for actual variation.
bool frame_has_content(const std::vector<uint8_t> &pixels) {
  if (pixels.size() < 4)
    return false;
  const uint32_t first = *reinterpret_cast<const uint32_t *>(pixels.data());
  for (size_t i = 4; i + 4 <= pixels.size(); i += 4) {
    if (*reinterpret_cast<const uint32_t *>(pixels.data() + i) != first)
      return true;
  }
  return false;
}

void write_ppm(const char *path, const std::vector<uint8_t> &bgra, int width, int height) {
  FILE *file = nullptr;
  if (fopen_s(&file, path, "wb") != 0 || !file)
    return;
  std::fprintf(file, "P6\n%d %d\n255\n", width, height);
  for (int i = 0; i < width * height; ++i) {
    const uint8_t rgb[3] = {bgra[i * 4 + 2], bgra[i * 4 + 1], bgra[i * 4 + 0]};
    std::fwrite(rgb, 1, 3, file);
  }
  std::fclose(file);
  std::printf("       wrote %s\n", path);
}

// A throwaway D3D9 device so the upload path (the one piece that would take the
// host process down with it) can be exercised for real.
struct D3D9Fixture {
  HWND window = nullptr;
  IDirect3D9 *d3d = nullptr;
  IDirect3DDevice9 *device = nullptr;

  // |ex| asks for a D3D9Ex device, which is what Source hands us when the game
  // runs with -d3d9ex. It is not a cosmetic difference: a D3D9Ex device rejects
  // D3DPOOL_MANAGED with D3DERR_INVALIDCALL, so it is the only way to exercise
  // the allocation path the game actually takes.
  bool create(bool ex) {
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "lje_monaco_smoketest";
    RegisterClassExA(&wc);

    window = CreateWindowExA(0, wc.lpszClassName, "", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr,
                             nullptr, wc.hInstance, nullptr);
    if (!window)
      return false;

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;

    const DWORD flags = D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED;

    if (!ex) {
      d3d = Direct3DCreate9(D3D_SDK_VERSION);
      if (!d3d)
        return false;
      return SUCCEEDED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, flags, &pp,
                                         &device)) &&
             device != nullptr;
    }

    using CreateExFn = HRESULT(WINAPI *)(UINT, IDirect3D9Ex **);
    HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
    if (!d3d9)
      d3d9 = LoadLibraryA("d3d9.dll");
    auto create_ex =
        d3d9 ? reinterpret_cast<CreateExFn>(GetProcAddress(d3d9, "Direct3DCreate9Ex")) : nullptr;
    if (!create_ex)
      return false;

    IDirect3D9Ex *d3d_ex = nullptr;
    if (FAILED(create_ex(D3D_SDK_VERSION, &d3d_ex)) || !d3d_ex)
      return false;
    d3d = d3d_ex;

    IDirect3DDevice9Ex *device_ex = nullptr;
    if (FAILED(d3d_ex->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, flags, &pp,
                                      nullptr, &device_ex)) ||
        !device_ex)
      return false;
    device = device_ex;
    return true;
  }

  ~D3D9Fixture() {
    if (device)
      device->Release();
    if (d3d)
      d3d->Release();
    if (window)
      DestroyWindow(window);
  }
};

// Reads a pixel straight out of a managed texture.
uint32_t read_texel(IDirect3DTexture9 *texture, int x, int y) {
  D3DLOCKED_RECT locked = {};
  if (FAILED(texture->LockRect(0, &locked, nullptr, D3DLOCK_READONLY)))
    return 0;
  const auto value = *reinterpret_cast<const uint32_t *>(
      static_cast<const uint8_t *>(locked.pBits) + static_cast<size_t>(y) * locked.Pitch +
      static_cast<size_t>(x) * 4);
  texture->UnlockRect(0);
  return value;
}

constexpr float kEditorWidth = 900.0f;
constexpr float kEditorHeight = 600.0f;

// Runs one complete ImGui frame containing the editor widget, exactly the way
// a Lua script would between imgui.new_frame() and imgui.render().
// Passing |p_open| gives the window a title bar and therefore a close button,
// which is what the keyboard-nav regression needs something to press.
void imgui_frame(const std::function<void()> &body, bool *p_open = nullptr) {
  // Exactly where the overlay calls it, so the real gate is under test.
  monaco::update_imgui_nav();

  ImGui_ImplNull_NewFrame();
  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(kEditorWidth + 40.0f, kEditorHeight + 60.0f));
  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
  if (!p_open)
    flags |= ImGuiWindowFlags_NoTitleBar;
  ImGui::Begin("editor", p_open, flags);
  body();
  ImGui::End();
  ImGui::Render();
}

// The D3D9 upload path, driven the way the EndScene hook drives it. Run against
// both a plain and a D3D9Ex device: they disagree about which memory pools
// exist, and the game only ever gives us the latter.
void run_texture_checks(IDirect3DDevice9 *device, monaco::View *view, const char *label) {
  const auto tag = [label](const char *what) { return std::string(label) + ": " + what; };

  monaco::Texture texture;
  const int tw = 300;
  const int th = 200;
  std::vector<uint8_t> source(static_cast<size_t>(tw) * th * 4, 0);
  for (int i = 0; i < tw * th; ++i) {
    source[i * 4 + 0] = 0x11; // B
    source[i * 4 + 1] = 0x22; // G
    source[i * 4 + 2] = 0x33; // R
    source[i * 4 + 3] = 0xFF; // A
  }

  const RECT full = {0, 0, tw, th};
  check(texture.upload(device, source.data(), tw, th, full), tag("a frame uploads to D3D9").c_str());
  check(texture.handle() != nullptr, tag("the upload creates a texture").c_str());
  check(texture.width() >= tw && texture.height() >= th,
        tag("the texture is at least as large as the frame").c_str());
  check(read_texel(texture.handle(), 10, 10) == 0xFF332211,
        tag("uploaded pixels keep their BGRA byte order").c_str());

  // A partial update must leave everything outside the dirty rect alone.
  for (int y = 0; y < 10; ++y) {
    for (int x = 0; x < 10; ++x) {
      auto *pixel = source.data() + (static_cast<size_t>(y) * tw + x) * 4;
      pixel[0] = 0xAA;
      pixel[1] = 0xBB;
      pixel[2] = 0xCC;
      pixel[3] = 0xFF;
    }
  }
  const RECT dirty = {0, 0, 10, 10};
  check(texture.upload(device, source.data(), tw, th, dirty),
        tag("a dirty-rect update uploads").c_str());
  check(read_texel(texture.handle(), 5, 5) == 0xFFCCBBAA, tag("the dirty rect is updated").c_str());
  check(read_texel(texture.handle(), 50, 50) == 0xFF332211,
        tag("pixels outside it are untouched").c_str());

  // Growing past the allocation granularity replaces the surface, and the old
  // one has to stay alive for a few frames because draw data may still cite it.
  auto *before_grow = texture.handle();
  const int bw = 700;
  const int bh = 500;
  std::vector<uint8_t> bigger(static_cast<size_t>(bw) * bh * 4, 0x40);
  const RECT big_full = {0, 0, bw, bh};
  check(texture.upload(device, bigger.data(), bw, bh, big_full),
        tag("a larger frame uploads").c_str());
  check(texture.handle() != before_grow,
        tag("growing past the granularity replaces the texture").c_str());
  check(before_grow->AddRef() > 1,
        tag("the replaced texture is still alive for in-flight draw data").c_str());
  before_grow->Release();

  // And the real thing.
  view->upload(device);
  check(view->texture_handle() != nullptr, tag("an editor frame reaches a D3D9 texture").c_str());

  // A Reset invalidates default-pool surfaces; the overlay drops them and the
  // next upload has to rebuild from the frame the view still holds.
  view->release_texture();
  check(view->texture_handle() == nullptr, tag("releasing drops the texture").c_str());
  view->upload(device);
  check(view->texture_handle() != nullptr, tag("the editor texture is rebuilt after a reset").c_str());
}

} // namespace

int main() {
  // A crash anywhere below would otherwise take the buffered progress with it,
  // and this test exists precisely to localize crashes.
  setvbuf(stdout, nullptr, _IONBF, 0);

  std::printf("lje-imgui monaco smoke test\n\n");

  check(monaco::initialize(), "CEF starts");
  if (!monaco::available()) {
    std::printf("\nCEF failed to start: %s\n", monaco::last_error().c_str());
    return 1;
  }

  monaco::EditorOptions options;
  options.text = "-- lje-imgui\nlocal function hello()\n  print('monaco')\nend\n";
  options.language = "lua";
  options.width = static_cast<int>(kEditorWidth);
  options.height = static_cast<int>(kEditorHeight);

  CefRefPtr<monaco::View> view = new monaco::View(1, options);
  view->create_browser();

  check(wait_for([&] { return view->ready(); }, 30000, "the editor to report ready"),
        "monaco:// page loads and Monaco initializes");

  check(wait_for([&] { return view->frames_painted() > 0; }, 10000, "the first paint"),
        "off-screen rendering produces frames");

  // The initial content travels host -> page -> host and must come back intact.
  check(wait_for([&] { return view->document().text() == options.text; }, 10000,
                 "the initial document to echo back"),
        "initial text round-trips through the editor");

  // Let the editor settle so the captured frame shows the real thing rather
  // than the loading placeholder.
  Sleep(1000);

  std::vector<uint8_t> pixels;
  int width = 0;
  int height = 0;
  const bool got_frame = view->copy_frame(pixels, width, height);
  check(got_frame, "a frame can be read back");
  check(got_frame && width == options.width && height == options.height,
        "the frame matches the requested size");
  check(got_frame && frame_has_content(pixels), "the frame is not blank");
  if (got_frame)
    write_ppm("monaco_smoketest.ppm", pixels, width, height);

  const uint64_t version_before_write = view->document().version();
  const std::string written = "print('written from the host')\n-- \xE2\x9C\x93 unicode\n";
  view->set_text(written);
  check(view->document().text() == written, "set_text updates the mirror immediately");
  check(wait_for([&] { return view->document().version() > version_before_write; }, 10000,
                 "the editor to acknowledge the write"),
        "set_text reaches the editor");
  check(view->document().text() == written, "the editor echoes the host write back verbatim");
  check(!view->document().consume_changed(), "a host write is not reported as a user edit");

  // ------------------------------------------------------------------
  // Several editors at once, through the public facade.
  // ------------------------------------------------------------------
  monaco::EditorOptions alpha_options;
  alpha_options.text = "alpha";
  monaco::EditorOptions beta_options;
  beta_options.text = "beta";
  beta_options.language = "javascript";
  beta_options.theme = "vs";

  const auto alpha = monaco::create(alpha_options);
  const auto beta = monaco::create(beta_options);
  check(alpha != monaco::kInvalidEditor && beta != monaco::kInvalidEditor && alpha != beta,
        "several editors can be created");
  check(monaco::count() == 2, "the registry tracks them all");
  check(wait_for([&] { return monaco::ready(alpha) && monaco::ready(beta); }, 30000,
                 "both editors to become ready"),
        "every instance loads independently");
  check(wait_for([&] { return monaco::text(alpha) == "alpha" && monaco::text(beta) == "beta"; },
                 10000, "both documents to echo back"),
        "instances keep separate documents");

  monaco::set_text(alpha, "alpha changed");
  check(
      wait_for([&] { return monaco::text(alpha) == "alpha changed"; }, 10000, "the write to land"),
      "writing to one instance works");
  check(monaco::text(beta) == "beta", "writing to one instance does not disturb another");

  check(monaco::destroy(alpha) && monaco::destroy(beta), "editors can be destroyed");
  check(monaco::count() == 0, "destroying removes them from the registry");
  check(!monaco::destroy(alpha), "destroying an unknown editor is a no-op");
  check(monaco::text(alpha).empty(), "a destroyed editor reads back empty");

  // ------------------------------------------------------------------
  // Input, driven through the same code path the overlay uses.
  // ------------------------------------------------------------------
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui_ImplNull_Init();

  // First frame just establishes the widget and its rectangle.
  imgui_frame([&] { view->draw(kEditorWidth, kEditorHeight); });

  ImGuiIO &io = ImGui::GetIO();
  const ImVec2 inside(200.0f, 40.0f); // Somewhere over line 1 of the editor.

  io.AddMousePosEvent(inside.x, inside.y);
  imgui_frame([&] { view->draw(kEditorWidth, kEditorHeight); });

  io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
  imgui_frame([&] { view->draw(kEditorWidth, kEditorHeight); });
  check(view->focused(), "clicking the widget focuses the editor");

  io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
  imgui_frame([&] { view->draw(kEditorWidth, kEditorHeight); });

  check(io.WantCaptureKeyboard,
        "a focused editor makes ImGui claim the keyboard so the overlay swallows keys");

  // Select everything and type over it: Ctrl+A then "xy".
  io.AddKeyEvent(ImGuiMod_Ctrl, true);
  io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
  io.AddKeyEvent(ImGuiKey_A, true);
  imgui_frame([&] { view->draw(kEditorWidth, kEditorHeight); });
  io.AddKeyEvent(ImGuiKey_A, false);
  io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
  io.AddKeyEvent(ImGuiMod_Ctrl, false);
  imgui_frame([&] { view->draw(kEditorWidth, kEditorHeight); });

  for (char typed : std::string("xy")) {
    const ImGuiKey key = static_cast<ImGuiKey>(ImGuiKey_A + (typed - 'a'));
    io.AddKeyEvent(key, true);
    io.AddInputCharacter(static_cast<unsigned int>(typed));
    imgui_frame([&] { view->draw(kEditorWidth, kEditorHeight); });
    io.AddKeyEvent(key, false);
    imgui_frame([&] { view->draw(kEditorWidth, kEditorHeight); });
  }

  check(wait_for([&] { return view->document().text() == "xy"; }, 10000,
                 "typed text to reach the document"),
        "keyboard input is forwarded to Monaco");
  check(view->document().consume_changed(), "a user edit raises the changed flag");

  // Space and Enter are ImGui's nav "activate" keys, and the overlay cannot stop
  // feeding keys to ImGui while an editor is up because Monaco reads its own
  // input back out of ImGui's IO. Newlines followed by a space used to press
  // whatever nav had settled on - the window's own close button included, which
  // made the whole window vanish and never come back.
  {
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    bool window_open = true;
    imgui_frame([&] { view->draw(kEditorWidth, kEditorHeight); }, &window_open);
    check(view->focused(), "the editor still owns the keyboard going into the nav check");

    for (int i = 0; i < 50; ++i) {
      io.AddKeyEvent(ImGuiKey_Enter, true);
      imgui_frame([&] { view->draw(kEditorWidth, kEditorHeight); }, &window_open);
      io.AddKeyEvent(ImGuiKey_Enter, false);
      imgui_frame([&] { view->draw(kEditorWidth, kEditorHeight); }, &window_open);
    }

    io.AddKeyEvent(ImGuiKey_Space, true);
    imgui_frame([&] { view->draw(kEditorWidth, kEditorHeight); }, &window_open);
    io.AddKeyEvent(ImGuiKey_Space, false);
    imgui_frame([&] { view->draw(kEditorWidth, kEditorHeight); }, &window_open);

    check(window_open,
          "newlines then space in a focused editor leave the host window alone");
  }

  // Clicking outside the widget must hand the keyboard back to the game.
  io.AddMousePosEvent(kEditorWidth + 20.0f, kEditorHeight + 40.0f);
  imgui_frame([&] { view->draw(kEditorWidth, kEditorHeight); });
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
  imgui_frame([&] { view->draw(kEditorWidth, kEditorHeight); });
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
  imgui_frame([&] { view->draw(kEditorWidth, kEditorHeight); });
  check(!view->focused(), "clicking away blurs the editor");

  // A resize must reach the browser and produce a differently sized frame.
  const uint64_t before_resize = view->frames_painted();
  imgui_frame([&] { view->draw(640.0f, 480.0f); });
  check(wait_for(
            [&] {
              int w = 0, h = 0;
              std::vector<uint8_t> buffer;
              return view->frames_painted() > before_resize && view->copy_frame(buffer, w, h) &&
                     w == 640 && h == 480;
            },
            10000, "the resized frame"),
        "resizing re-lays out the editor");

  ImGui_ImplNull_Shutdown();
  ImGui::DestroyContext();

  // ------------------------------------------------------------------
  // D3D9 upload, the way the EndScene hook drives it.
  // ------------------------------------------------------------------
  struct DeviceKind {
    bool ex;
    const char *label;
  };
  for (const auto &kind : {DeviceKind{false, "d3d9"}, DeviceKind{true, "d3d9ex"}}) {
    D3D9Fixture d3d;
    if (!d3d.create(kind.ex)) {
      std::printf("[SKIP] no %s device available, skipping its texture upload checks\n",
                  kind.label);
      continue;
    }
    run_texture_checks(d3d.device, view.get(), kind.label);
  }

  view->close();
  wait_for([&] { return view->closed(); }, 5000, "the browser to close");
  view = nullptr;

  monaco::shutdown();

  std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
  return g_failures == 0 ? 0 : 1;
}
