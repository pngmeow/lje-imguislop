#pragma once
#include <d3d9.h>

#include <cstdint>
#include <vector>

namespace monaco {

// Destination for off-screen browser frames.
//
// Lives in a dynamic D3DPOOL_DEFAULT surface - a D3D9Ex device refuses
// D3DPOOL_MANAGED - and is only ever touched from the D3D9 render thread inside
// the EndScene hook. Default-pool surfaces do not survive a device Reset
// (alt-tab, resolution change), so the overlay drops them in its Reset hook and
// the next upload rebuilds from the frame the view still holds.
//
// The surface is over-allocated to a coarse grid so that dragging a window edge
// does not recreate a texture every frame; the widget compensates with UVs. On
// the rare occasions the texture is replaced anyway, the old one is retired for
// a few frames rather than released immediately - ImGui draw data recorded on
// the script thread may still reference it.
class Texture {
public:
  ~Texture();

  // Uploads |dirty| (in pixels, clamped to the frame) from a tightly packed
  // BGRA frame. Returns false if the texture could not be created.
  bool upload(IDirect3DDevice9 *device, const uint8_t *bgra, int width, int height,
              const RECT &dirty);

  // Ages the retire list without uploading, for frames where nothing changed.
  void tick();

  void release();

  IDirect3DTexture9 *handle() const { return texture_; }
  int width() const { return width_; }
  int height() const { return height_; }

  // Size of the content currently held, which may be smaller than the texture.
  int content_width() const { return content_width_; }
  int content_height() const { return content_height_; }

private:
  bool ensure(IDirect3DDevice9 *device, int width, int height);
  void retire();

  IDirect3DDevice9 *device_ = nullptr;
  IDirect3DTexture9 *texture_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  int content_width_ = 0;
  int content_height_ = 0;

  struct Retired {
    IDirect3DTexture9 *texture;
    int frames_left;
  };
  std::vector<Retired> retired_;
};

} // namespace monaco