#include "texture.hpp"

#include <algorithm>
#include <cstring>

#include "../log.hpp"

namespace monaco {
namespace {

// Texture dimensions are rounded up to this so that a resize drag reuses the
// same surface instead of thrashing the allocator.
constexpr int kGranularity = 128;

// How many EndScene calls a replaced texture stays alive for. Draw data built
// on the script thread lags the render thread by at most one frame; three is
// generous.
constexpr int kRetireFrames = 3;

int round_up(int value) { return ((value + kGranularity - 1) / kGranularity) * kGranularity; }

} // namespace

Texture::~Texture() { release(); }

bool Texture::ensure(IDirect3DDevice9 *device, int width, int height) {
  if (device_ != device) {
    // The host recreated its device; everything we hold belongs to the old one
    // and must not be released through it.
    texture_ = nullptr;
    retired_.clear();
    width_ = height_ = 0;
    device_ = device;
  }

  const int wanted_w = round_up(width);
  const int wanted_h = round_up(height);

  // Grow eagerly, shrink only once the surface is more than twice as large as
  // it needs to be.
  const bool fits = texture_ && width_ >= wanted_w && height_ >= wanted_h &&
                    width_ <= wanted_w * 2 && height_ <= wanted_h * 2;
  if (fits)
    return true;

  // A D3D9Ex device - which is what Source gives us when the game runs with
  // -d3d9ex - rejects D3DPOOL_MANAGED outright with D3DERR_INVALIDCALL, so the
  // frame lives in a dynamic default-pool surface instead: both device kinds
  // accept it and it stays lockable. Hardware old enough to lack
  // D3DCAPS2_DYNAMICTEXTURES falls back to the managed path.
  IDirect3DTexture9 *created = nullptr;
  auto hr = device->CreateTexture(static_cast<UINT>(wanted_w), static_cast<UINT>(wanted_h), 1,
                                  D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &created,
                                  nullptr);
  if (FAILED(hr) || !created) {
    hr = device->CreateTexture(static_cast<UINT>(wanted_w), static_cast<UINT>(wanted_h), 1, 0,
                               D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &created, nullptr);
  }
  if (FAILED(hr) || !created) {
    logger::error("monaco: CreateTexture(%dx%d) failed (0x%08lX)", wanted_w, wanted_h,
                  static_cast<unsigned long>(hr));
    return false;
  }

  if (texture_)
    retired_.push_back({texture_, kRetireFrames});

  texture_ = created;
  width_ = wanted_w;
  height_ = wanted_h;
  content_width_ = 0;
  content_height_ = 0;
  return true;
}

void Texture::tick() { retire(); }

void Texture::retire() {
  for (auto it = retired_.begin(); it != retired_.end();) {
    if (--it->frames_left <= 0) {
      it->texture->Release();
      it = retired_.erase(it);
    } else {
      ++it;
    }
  }
}

bool Texture::upload(IDirect3DDevice9 *device, const uint8_t *bgra, int width, int height,
                     const RECT &dirty) {
  if (!device || !bgra || width <= 0 || height <= 0)
    return false;

  if (!ensure(device, width, height))
    return false;

  retire();

  RECT rect;
  rect.left = std::max(0L, dirty.left);
  rect.top = std::max(0L, dirty.top);
  rect.right = std::min<LONG>(width, dirty.right);
  rect.bottom = std::min<LONG>(height, dirty.bottom);

  // A grown texture has undefined contents outside the previously written area,
  // so the first upload after a resize has to cover the whole frame.
  if (content_width_ != width || content_height_ != height) {
    rect.left = 0;
    rect.top = 0;
    rect.right = width;
    rect.bottom = height;
  }

  if (rect.right <= rect.left || rect.bottom <= rect.top)
    return true;

  D3DLOCKED_RECT locked = {};
  // Locking only the sub-rect that changed is what keeps typing cheap.
  // D3DLOCK_DISCARD is deliberately not used: it is whole-surface only and
  // would throw away the pixels outside the dirty rect.
  if (FAILED(texture_->LockRect(0, &locked, &rect, 0)))
    return false;

  const size_t row_bytes = static_cast<size_t>(rect.right - rect.left) * 4;
  const size_t src_pitch = static_cast<size_t>(width) * 4;
  auto *dst = static_cast<uint8_t *>(locked.pBits);
  const uint8_t *src =
      bgra + static_cast<size_t>(rect.top) * src_pitch + static_cast<size_t>(rect.left) * 4;

  for (LONG y = rect.top; y < rect.bottom; ++y) {
    std::memcpy(dst, src, row_bytes);
    dst += locked.Pitch;
    src += src_pitch;
  }

  texture_->UnlockRect(0);

  content_width_ = width;
  content_height_ = height;
  return true;
}

void Texture::release() {
  for (auto &entry : retired_)
    entry.texture->Release();
  retired_.clear();

  if (texture_) {
    texture_->Release();
    texture_ = nullptr;
  }
  width_ = height_ = 0;
  content_width_ = content_height_ = 0;
  device_ = nullptr;
}

} // namespace monaco