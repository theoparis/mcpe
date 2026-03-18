#ifndef MCPE_PLATFORM_SDL3_GRAPHICSDRIVER_H
#define MCPE_PLATFORM_SDL3_GRAPHICSDRIVER_H

#include <SDL3/SDL.h>

#include "../../App.h"
#include <string>

class Sdl3GraphicsDriver : public GraphicsBackend {
public:
  ~Sdl3GraphicsDriver() override = default;

  [[nodiscard]] virtual auto windowFlags() const -> Uint32 = 0;
  virtual auto createWindow(AppContext &context, const char *title, int width,
      int height, std::string &error) -> bool = 0;
  virtual auto resetSurface(AppContext &context, uint32_t width,
      uint32_t height, std::string &error) -> bool = 0;
  virtual void destroySurface(AppContext &context) = 0;
  virtual void destroyWindow(AppContext &context) = 0;
};

#endif
