#ifndef MCPE_PLATFORM_WAYLAND_WAYLANDSIGNTEXTURE_H
#define MCPE_PLATFORM_WAYLAND_WAYLANDSIGNTEXTURE_H

#include "WaylandSignCompositor.h"

#include "../../client/renderer/Textures.h"
#include "../../client/renderer/gles.h"

#include <cstdint>

class WaylandSignTexture {
public:
  WaylandSignTexture();
  ~WaylandSignTexture();

  bool update();
  TextureId getTextureId() const { return textureId; }

  int getWidth() const { return width; }
  int getHeight() const { return height; }
  bool isReady() const { return textureId != 0; }

private:
  bool importFrame(const DmabufFrame &frame);

  TextureId textureId;
  void *eglImage;
  uint64_t lastSequence;
  int width;
  int height;
};

#endif
