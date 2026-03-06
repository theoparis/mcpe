#ifndef MCPE_PLATFORM_WAYLAND_WAYLANDSIGNCOMPOSITOR_H
#define MCPE_PLATFORM_WAYLAND_WAYLANDSIGNCOMPOSITOR_H

#include <cstdint>
#include <string>

struct DmabufPlaneInfo {
  int fd = -1;
  uint32_t stride = 0;
  uint32_t offset = 0;
  uint64_t modifier = 0;
};

struct DmabufFrame {
  int width = 0;
  int height = 0;
  uint32_t format = 0;
  int planeCount = 0;
  DmabufPlaneInfo planes[4];
  uint32_t bufferId = 0;
  uint64_t sequence = 0;
};

class WaylandSignCompositor {
public:
  static WaylandSignCompositor &get();

  bool start(int width, int height);
  void stop();
  bool isRunning() const;

  std::string getDisplayName() const;

  void setSurfaceSize(int width, int height);

  bool acquireLatestFrame(DmabufFrame &out);
  void releaseFrame(DmabufFrame &frame);

  void setPointerFocus(bool focused);
  void sendPointerMotion(float u, float v);
  void sendPointerButton(uint32_t button, bool pressed);

  void sendKey(uint32_t key, bool pressed);
  void sendText(const std::string &text);

private:
  WaylandSignCompositor();
  ~WaylandSignCompositor();
  WaylandSignCompositor(const WaylandSignCompositor &) = delete;
  WaylandSignCompositor &operator=(const WaylandSignCompositor &) = delete;

  struct Impl;
  Impl *impl;
};

#endif
