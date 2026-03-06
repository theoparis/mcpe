#include "WaylandSignCompositor.h"

#include "../log.h"
#include "../time.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <xkbcommon/xkbcommon.h>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include <drm/drm_fourcc.h>

#include "protocols/linux-dmabuf-unstable-v1-protocol.h"
#include "protocols/xdg-shell-protocol.h"

namespace {

struct DmabufBuffer {
  wl_resource *resource = nullptr;
  int width = 0;
  int height = 0;
  uint32_t format = 0;
  int planeCount = 0;
  DmabufPlaneInfo planes[4];
  uint32_t id = 0;
};

struct Surface {
  wl_resource *resource = nullptr;
  wl_resource *xdgSurface = nullptr;
  wl_resource *xdgToplevel = nullptr;

  DmabufBuffer *pendingBuffer = nullptr;
  DmabufBuffer *currentBuffer = nullptr;

  int width = 0;
  int height = 0;
  bool configured = false;
};

struct BufferParams {
  wl_resource *resource = nullptr;
  DmabufPlaneInfo planes[4];
  int planeCount = 0;
  uint32_t format = 0;
  int width = 0;
  int height = 0;
  bool used = false;
};

struct KeymapData {
  xkb_context *context = nullptr;
  xkb_keymap *keymap = nullptr;
  xkb_state *state = nullptr;
  int fd = -1;
  size_t size = 0;
};

static int createKeymapFile(const char *data, size_t size) {
#ifdef MFD_CLOEXEC
  int fd = memfd_create("mcpe-xkb-keymap", MFD_CLOEXEC);
  if (fd >= 0) {
    ssize_t written = write(fd, data, size);
    if (written < 0 || (size_t)written != size) {
      close(fd);
      return -1;
    }
    lseek(fd, 0, SEEK_SET);
    return fd;
  }
#endif

  char path[] = "/tmp/mcpe-xkb-keymap-XXXXXX";
  int fd = mkstemp(path);
  if (fd < 0) {
    return -1;
  }
  unlink(path);
  ssize_t written = write(fd, data, size);
  if (written < 0 || (size_t)written != size) {
    close(fd);
    return -1;
  }
  lseek(fd, 0, SEEK_SET);
  return fd;
}

static uint32_t mapKeyToEvdev(uint32_t key) {
  if (key >= 'A' && key <= 'Z')
    return KEY_A + (key - 'A');
  if (key >= 'a' && key <= 'z')
    return KEY_A + (key - 'a');
  if (key >= '0' && key <= '9')
    return KEY_0 + (key - '0');

  switch (key) {
  case 8:
    return KEY_BACKSPACE;
  case 13:
    return KEY_ENTER;
  case 27:
    return KEY_ESC;
  case 32:
    return KEY_SPACE;
  default:
    return 0;
  }
}

} // namespace

struct WaylandSignCompositor::Impl {
  wl_display *display = nullptr;
  wl_event_loop *eventLoop = nullptr;

  wl_global *compositorGlobal = nullptr;
  wl_global *seatGlobal = nullptr;
  wl_global *outputGlobal = nullptr;
  wl_global *xdgGlobal = nullptr;
  wl_global *dmabufGlobal = nullptr;

  std::thread thread;
  std::atomic<bool> running{false};
  std::string displayName;

  std::mutex frameMutex;
  DmabufFrame latestFrame{};
  uint64_t sequence = 0;
  bool hasFrame = false;

  Surface *primarySurface = nullptr;

  wl_resource *seatResource = nullptr;
  wl_resource *pointerResource = nullptr;
  wl_resource *keyboardResource = nullptr;

  bool pointerFocused = false;
  bool pointerEntered = false;

  KeymapData keymap;

  int surfaceWidth = 256;
  int surfaceHeight = 128;

  uint32_t nextBufferId = 1;
};

static void buffer_destroy(struct wl_client *, struct wl_resource *resource) {
  DmabufBuffer *buffer =
      static_cast<DmabufBuffer *>(wl_resource_get_user_data(resource));
  if (!buffer)
    return;
  for (int i = 0; i < buffer->planeCount; ++i) {
    if (buffer->planes[i].fd >= 0) {
      close(buffer->planes[i].fd);
      buffer->planes[i].fd = -1;
    }
  }
  delete buffer;
}

static const struct wl_buffer_interface buffer_interface = {
    buffer_destroy,
};

static void surface_destroy(struct wl_client *, struct wl_resource *resource) {
  Surface *surface =
      static_cast<Surface *>(wl_resource_get_user_data(resource));
  if (!surface)
    return;
  delete surface;
}

static void surface_attach(struct wl_client *, struct wl_resource *resource,
                           struct wl_resource *bufferResource, int32_t,
                           int32_t) {
  Surface *surface =
      static_cast<Surface *>(wl_resource_get_user_data(resource));
  if (!surface)
    return;
  if (!bufferResource) {
    surface->pendingBuffer = nullptr;
    return;
  }
  DmabufBuffer *buffer =
      static_cast<DmabufBuffer *>(wl_resource_get_user_data(bufferResource));
  surface->pendingBuffer = buffer;
}

static void surface_commit(struct wl_client *, struct wl_resource *resource) {
  Surface *surface =
      static_cast<Surface *>(wl_resource_get_user_data(resource));
  if (!surface)
    return;

  if (surface->pendingBuffer) {
    DmabufBuffer *previous = surface->currentBuffer;
    surface->currentBuffer = surface->pendingBuffer;
    surface->pendingBuffer = nullptr;

    if (previous && previous->resource) {
      wl_buffer_send_release(previous->resource);
    }
  }

  WaylandSignCompositor &compositor = WaylandSignCompositor::get();
  WaylandSignCompositor::Impl *impl = compositor.impl;
  if (!impl || !surface->currentBuffer)
    return;

  std::lock_guard<std::mutex> lock(impl->frameMutex);
  impl->latestFrame.width = surface->currentBuffer->width;
  impl->latestFrame.height = surface->currentBuffer->height;
  impl->latestFrame.format = surface->currentBuffer->format;
  impl->latestFrame.planeCount = surface->currentBuffer->planeCount;
  impl->latestFrame.bufferId = surface->currentBuffer->id;
  impl->latestFrame.sequence = ++impl->sequence;
  for (int i = 0; i < surface->currentBuffer->planeCount; ++i) {
    impl->latestFrame.planes[i] = surface->currentBuffer->planes[i];
  }
  impl->hasFrame = true;
}

static void surface_damage(struct wl_client *, struct wl_resource *, int32_t,
                           int32_t, int32_t, int32_t) {}

static void surface_damage_buffer(struct wl_client *, struct wl_resource *,
                                  int32_t, int32_t, int32_t, int32_t) {}

static void surface_set_buffer_scale(struct wl_client *, struct wl_resource *,
                                     int32_t) {}

static void surface_set_buffer_transform(struct wl_client *,
                                         struct wl_resource *, int32_t) {}

static void surface_set_opaque_region(struct wl_client *, struct wl_resource *,
                                      struct wl_resource *) {}

static void surface_set_input_region(struct wl_client *, struct wl_resource *,
                                     struct wl_resource *) {}

static void surface_frame(struct wl_client *client, struct wl_resource *,
                          uint32_t callback) {
  wl_resource *cb =
      wl_resource_create(client, &wl_callback_interface, 1, callback);
  wl_callback_send_done(cb, (uint32_t)getTimeMs());
  wl_resource_destroy(cb);
}

static const struct wl_surface_interface surface_interface = {
    surface_destroy,
    surface_attach,
    surface_damage,
    surface_frame,
    surface_set_opaque_region,
    surface_set_input_region,
    surface_commit,
    surface_set_buffer_transform,
    surface_set_buffer_scale,
    surface_damage_buffer,
};

static void compositor_create_surface(struct wl_client *client,
                                      struct wl_resource *resource,
                                      uint32_t id) {
  Surface *surface = new Surface();
  wl_resource *res = wl_resource_create(client, &wl_surface_interface,
                                        wl_resource_get_version(resource), id);
  wl_resource_set_implementation(res, &surface_interface, surface, nullptr);
  surface->resource = res;

  WaylandSignCompositor &compositor = WaylandSignCompositor::get();
  if (compositor.impl && compositor.impl->primarySurface == nullptr) {
    compositor.impl->primarySurface = surface;
  }
}

static void region_destroy(struct wl_client *, struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

static void region_add(struct wl_client *, struct wl_resource *, int32_t,
                       int32_t, int32_t, int32_t) {}

static void region_subtract(struct wl_client *, struct wl_resource *, int32_t,
                            int32_t, int32_t, int32_t) {}

static const struct wl_region_interface region_interface = {
    region_destroy, region_add, region_subtract};

static void compositor_create_region(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id) {
  wl_resource *res = wl_resource_create(client, &wl_region_interface,
                                        wl_resource_get_version(resource), id);
  wl_resource_set_implementation(res, &region_interface, nullptr, nullptr);
}

static const struct wl_compositor_interface compositor_interface = {
    compositor_create_surface, compositor_create_region};

static void bind_compositor(struct wl_client *client, void *data,
                            uint32_t version, uint32_t id) {
  wl_resource *resource =
      wl_resource_create(client, &wl_compositor_interface, version, id);
  wl_resource_set_implementation(resource, &compositor_interface, data,
                                 nullptr);
}

static void xdg_wm_base_destroy(struct wl_client *,
                                struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

static void xdg_wm_base_pong(struct wl_client *, struct wl_resource *,
                             uint32_t) {}

static void xdg_positioner_destroy(struct wl_client *,
                                   struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

static void xdg_positioner_set_size(struct wl_client *, struct wl_resource *,
                                    int32_t, int32_t) {}

static void xdg_positioner_set_anchor_rect(struct wl_client *,
                                           struct wl_resource *, int32_t,
                                           int32_t, int32_t, int32_t) {}

static void xdg_positioner_set_anchor(struct wl_client *, struct wl_resource *,
                                      uint32_t) {}

static void xdg_positioner_set_gravity(struct wl_client *, struct wl_resource *,
                                       uint32_t) {}

static void xdg_positioner_set_constraint_adjustment(struct wl_client *,
                                                     struct wl_resource *,
                                                     uint32_t) {}

static void xdg_positioner_set_offset(struct wl_client *, struct wl_resource *,
                                      int32_t, int32_t) {}

static void xdg_positioner_set_reactive(struct wl_client *,
                                        struct wl_resource *) {}

static void xdg_positioner_set_parent_size(struct wl_client *,
                                           struct wl_resource *, int32_t,
                                           int32_t) {}

static void xdg_positioner_set_parent_configure(struct wl_client *,
                                                struct wl_resource *,
                                                uint32_t) {}

static const struct xdg_positioner_interface xdg_positioner_interface = {
    xdg_positioner_destroy,         xdg_positioner_set_size,
    xdg_positioner_set_anchor_rect, xdg_positioner_set_anchor,
    xdg_positioner_set_gravity,     xdg_positioner_set_constraint_adjustment,
    xdg_positioner_set_offset,      xdg_positioner_set_reactive,
    xdg_positioner_set_parent_size, xdg_positioner_set_parent_configure,
};

static void xdg_wm_base_create_positioner(struct wl_client *client,
                                          struct wl_resource *resource,
                                          uint32_t id) {
  wl_resource *positioner = wl_resource_create(
      client, &xdg_positioner_interface, wl_resource_get_version(resource), id);
  wl_resource_set_implementation(positioner, &xdg_positioner_interface, nullptr,
                                 nullptr);
}

static void xdg_surface_destroy(struct wl_client *,
                                struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

static void xdg_surface_ack_configure(struct wl_client *, struct wl_resource *,
                                      uint32_t) {}

static void xdg_surface_set_window_geometry(struct wl_client *,
                                            struct wl_resource *, int32_t,
                                            int32_t, int32_t, int32_t) {}

static void xdg_toplevel_destroy(struct wl_client *,
                                 struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

static void xdg_toplevel_set_parent(struct wl_client *, struct wl_resource *,
                                    struct wl_resource *) {}

static void xdg_toplevel_set_title(struct wl_client *, struct wl_resource *,
                                   const char *) {}

static void xdg_toplevel_set_app_id(struct wl_client *, struct wl_resource *,
                                    const char *) {}

static void xdg_toplevel_show_window_menu(struct wl_client *,
                                          struct wl_resource *,
                                          struct wl_resource *, uint32_t,
                                          int32_t, int32_t) {}

static void xdg_toplevel_move(struct wl_client *, struct wl_resource *,
                              struct wl_resource *, uint32_t) {}

static void xdg_toplevel_resize(struct wl_client *, struct wl_resource *,
                                struct wl_resource *, uint32_t, uint32_t) {}

static void xdg_toplevel_set_max_size(struct wl_client *, struct wl_resource *,
                                      int32_t, int32_t) {}

static void xdg_toplevel_set_min_size(struct wl_client *, struct wl_resource *,
                                      int32_t, int32_t) {}

static void xdg_toplevel_set_maximized(struct wl_client *,
                                       struct wl_resource *) {}

static void xdg_toplevel_unset_maximized(struct wl_client *,
                                         struct wl_resource *) {}

static void xdg_toplevel_set_fullscreen(struct wl_client *,
                                        struct wl_resource *,
                                        struct wl_resource *) {}

static void xdg_toplevel_unset_fullscreen(struct wl_client *,
                                          struct wl_resource *) {}

static void xdg_toplevel_set_minimized(struct wl_client *,
                                       struct wl_resource *) {}

static void xdg_surface_get_toplevel(struct wl_client *client,
                                     struct wl_resource *resource, uint32_t id);

static const struct xdg_surface_interface xdg_surface_interface = {
    xdg_surface_destroy, xdg_surface_get_toplevel,
    xdg_surface_set_window_geometry, xdg_surface_ack_configure};

static const struct xdg_toplevel_interface xdg_toplevel_interface = {
    xdg_toplevel_destroy,          xdg_toplevel_set_parent,
    xdg_toplevel_set_title,        xdg_toplevel_set_app_id,
    xdg_toplevel_show_window_menu, xdg_toplevel_move,
    xdg_toplevel_resize,           xdg_toplevel_set_max_size,
    xdg_toplevel_set_min_size,     xdg_toplevel_set_maximized,
    xdg_toplevel_unset_maximized,  xdg_toplevel_set_fullscreen,
    xdg_toplevel_unset_fullscreen, xdg_toplevel_set_minimized};

static void xdg_wm_base_get_xdg_surface(struct wl_client *client,
                                        struct wl_resource *resource,
                                        uint32_t id,
                                        struct wl_resource *surfaceResource) {
  WaylandSignCompositor &compositor = WaylandSignCompositor::get();
  WaylandSignCompositor::Impl *impl = compositor.impl;

  wl_resource *xdgSurfaceRes = wl_resource_create(
      client, &xdg_surface_interface, wl_resource_get_version(resource), id);
  wl_resource_set_implementation(xdgSurfaceRes, &xdg_surface_interface, nullptr,
                                 nullptr);

  Surface *surface =
      static_cast<Surface *>(wl_resource_get_user_data(surfaceResource));
  if (surface) {
    surface->xdgSurface = xdgSurfaceRes;
    uint32_t serial = wl_display_next_serial(impl->display);
    xdg_surface_send_configure(xdgSurfaceRes, serial);
  }
}

static void xdg_wm_base_destroy_resource(struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

static const struct xdg_wm_base_interface xdg_wm_base_interface = {
    xdg_wm_base_destroy, xdg_wm_base_create_positioner,
    xdg_wm_base_get_xdg_surface, xdg_wm_base_pong};

static void xdg_surface_get_toplevel(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id) {
  WaylandSignCompositor &compositor = WaylandSignCompositor::get();
  WaylandSignCompositor::Impl *impl = compositor.impl;

  wl_resource *toplevel = wl_resource_create(
      client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);
  wl_resource_set_implementation(toplevel, &xdg_toplevel_interface, nullptr,
                                 nullptr);

  wl_array states;
  wl_array_init(&states);
  xdg_toplevel_send_configure(toplevel, impl->surfaceWidth, impl->surfaceHeight,
                              &states);
  wl_array_release(&states);

  Surface *surface = impl->primarySurface;
  if (surface) {
    surface->xdgToplevel = toplevel;
  }
}

static void bind_xdg_wm_base(struct wl_client *client, void *data,
                             uint32_t version, uint32_t id) {
  wl_resource *resource =
      wl_resource_create(client, &xdg_wm_base_interface, version, id);
  wl_resource_set_implementation(resource, &xdg_wm_base_interface, data,
                                 nullptr);
}

static void params_destroy(struct wl_client *, struct wl_resource *resource) {
  BufferParams *params =
      static_cast<BufferParams *>(wl_resource_get_user_data(resource));
  if (!params)
    return;
  for (int i = 0; i < params->planeCount; ++i) {
    if (params->planes[i].fd >= 0) {
      close(params->planes[i].fd);
      params->planes[i].fd = -1;
    }
  }
  delete params;
}

static void params_add(struct wl_client *, struct wl_resource *resource,
                       int32_t fd, uint32_t planeIdx, uint32_t offset,
                       uint32_t stride, uint32_t modifier_hi,
                       uint32_t modifier_lo) {
  BufferParams *params =
      static_cast<BufferParams *>(wl_resource_get_user_data(resource));
  if (!params || planeIdx >= 4)
    return;

  if ((int)planeIdx >= params->planeCount) {
    params->planeCount = planeIdx + 1;
  }

  params->planes[planeIdx].fd = fd;
  params->planes[planeIdx].offset = offset;
  params->planes[planeIdx].stride = stride;
  params->planes[planeIdx].modifier =
      (static_cast<uint64_t>(modifier_hi) << 32) | modifier_lo;
}

static void params_create(struct wl_client *client,
                          struct wl_resource *resource, int32_t width,
                          int32_t height, uint32_t format, uint32_t flags) {
  zwp_linux_buffer_params_v1_send_failed(resource);
}

static void params_create_immed(struct wl_client *client,
                                struct wl_resource *resource, uint32_t id,
                                int32_t width, int32_t height, uint32_t format,
                                uint32_t flags) {
  BufferParams *params =
      static_cast<BufferParams *>(wl_resource_get_user_data(resource));
  if (!params || params->used) {
    return;
  }

  params->used = true;
  params->width = width;
  params->height = height;
  params->format = format;

  WaylandSignCompositor &compositor = WaylandSignCompositor::get();
  WaylandSignCompositor::Impl *impl = compositor.impl;

  DmabufBuffer *buffer = new DmabufBuffer();
  buffer->width = width;
  buffer->height = height;
  buffer->format = format;
  buffer->planeCount = params->planeCount;
  for (int i = 0; i < params->planeCount; ++i) {
    buffer->planes[i] = params->planes[i];
  }
  buffer->id = impl->nextBufferId++;

  wl_resource *bufRes = wl_resource_create(client, &wl_buffer_interface, 1, id);
  buffer->resource = bufRes;
  wl_resource_set_implementation(bufRes, &buffer_interface, buffer, nullptr);
}

static const struct zwp_linux_buffer_params_v1_interface params_interface = {
    params_destroy,
    params_add,
    params_create,
    params_create_immed,
};

static void dmabuf_create_params(struct wl_client *client,
                                 struct wl_resource *resource, uint32_t id) {
  wl_resource *res =
      wl_resource_create(client, &zwp_linux_buffer_params_v1_interface,
                         wl_resource_get_version(resource), id);
  BufferParams *params = new BufferParams();
  params->resource = res;
  wl_resource_set_implementation(res, &params_interface, params, nullptr);
}

static const struct zwp_linux_dmabuf_v1_interface dmabuf_interface = {
    dmabuf_create_params,
};

static void bind_dmabuf(struct wl_client *client, void *data, uint32_t version,
                        uint32_t id) {
  wl_resource *resource =
      wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);
  wl_resource_set_implementation(resource, &dmabuf_interface, data, nullptr);

  if (version >= 3) {
    zwp_linux_dmabuf_v1_send_modifier(resource, DRM_FORMAT_XRGB8888, 0, 0);
    zwp_linux_dmabuf_v1_send_modifier(resource, DRM_FORMAT_ARGB8888, 0, 0);
  } else {
    zwp_linux_dmabuf_v1_send_format(resource, DRM_FORMAT_XRGB8888);
    zwp_linux_dmabuf_v1_send_format(resource, DRM_FORMAT_ARGB8888);
  }
}

static void pointer_set_cursor(struct wl_client *, struct wl_resource *,
                               uint32_t, struct wl_resource *, int32_t,
                               int32_t) {}

static void pointer_release(struct wl_client *, struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

static const struct wl_pointer_interface pointer_interface = {
    pointer_set_cursor, pointer_release};

static void keyboard_release(struct wl_client *, struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

static const struct wl_keyboard_interface keyboard_interface = {
    keyboard_release};

static void seat_get_pointer(struct wl_client *client,
                             struct wl_resource *resource, uint32_t id) {
  WaylandSignCompositor &compositor = WaylandSignCompositor::get();
  WaylandSignCompositor::Impl *impl = compositor.impl;

  wl_resource *pointer = wl_resource_create(
      client, &wl_pointer_interface, wl_resource_get_version(resource), id);
  wl_resource_set_implementation(pointer, &pointer_interface, nullptr, nullptr);
  impl->pointerResource = pointer;
}

static void seat_get_keyboard(struct wl_client *client,
                              struct wl_resource *resource, uint32_t id) {
  WaylandSignCompositor &compositor = WaylandSignCompositor::get();
  WaylandSignCompositor::Impl *impl = compositor.impl;

  wl_resource *keyboard = wl_resource_create(
      client, &wl_keyboard_interface, wl_resource_get_version(resource), id);
  wl_resource_set_implementation(keyboard, &keyboard_interface, nullptr,
                                 nullptr);
  impl->keyboardResource = keyboard;

  if (impl->keymap.fd >= 0) {
    wl_keyboard_send_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                            impl->keymap.fd, impl->keymap.size);
  }
}

static void seat_get_touch(struct wl_client *, struct wl_resource *, uint32_t) {
}

static void seat_release(struct wl_client *, struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

static const struct wl_seat_interface seat_interface = {
    seat_get_pointer,
    seat_get_keyboard,
    seat_get_touch,
    seat_release,
};

static void bind_seat(struct wl_client *client, void *data, uint32_t version,
                      uint32_t id) {
  WaylandSignCompositor &compositor = WaylandSignCompositor::get();
  WaylandSignCompositor::Impl *impl = compositor.impl;

  wl_resource *seat =
      wl_resource_create(client, &wl_seat_interface, version, id);
  wl_resource_set_implementation(seat, &seat_interface, data, nullptr);
  impl->seatResource = seat;

  wl_seat_send_capabilities(seat, WL_SEAT_CAPABILITY_POINTER |
                                      WL_SEAT_CAPABILITY_KEYBOARD);
  wl_seat_send_name(seat, "mcpe-seat");
}

static void output_release(struct wl_client *, struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

static const struct wl_output_interface output_interface = {output_release};

static void bind_output(struct wl_client *client, void *data, uint32_t version,
                        uint32_t id) {
  WaylandSignCompositor &compositor = WaylandSignCompositor::get();
  WaylandSignCompositor::Impl *impl = compositor.impl;

  wl_resource *output =
      wl_resource_create(client, &wl_output_interface, version, id);
  wl_resource_set_implementation(output, &output_interface, data, nullptr);

  wl_output_send_geometry(output, 0, 0, 100, 100, WL_OUTPUT_SUBPIXEL_NONE,
                          "mcpe", "virtual", WL_OUTPUT_TRANSFORM_NORMAL);
  wl_output_send_mode(output, WL_OUTPUT_MODE_CURRENT, impl->surfaceWidth,
                      impl->surfaceHeight, 60000);
  if (version >= WL_OUTPUT_DONE_SINCE_VERSION) {
    wl_output_send_done(output);
  }
}

WaylandSignCompositor &WaylandSignCompositor::get() {
  static WaylandSignCompositor instance;
  return instance;
}

WaylandSignCompositor::WaylandSignCompositor() : impl(new Impl()) {}

WaylandSignCompositor::~WaylandSignCompositor() { stop(); }

bool WaylandSignCompositor::start(int width, int height) {
  if (impl->running.load())
    return true;

  impl->surfaceWidth = width;
  impl->surfaceHeight = height;

  impl->display = wl_display_create();
  if (!impl->display) {
    LOGE("WaylandSignCompositor: wl_display_create failed\n");
    return false;
  }

  impl->eventLoop = wl_display_get_event_loop(impl->display);
  const char *name = wl_display_add_socket_auto(impl->display);
  if (!name) {
    LOGE("WaylandSignCompositor: wl_display_add_socket_auto failed\n");
    wl_display_destroy(impl->display);
    impl->display = nullptr;
    return false;
  }
  impl->displayName = name;

  impl->compositorGlobal = wl_global_create(
      impl->display, &wl_compositor_interface, 4, impl, bind_compositor);
  impl->seatGlobal =
      wl_global_create(impl->display, &wl_seat_interface, 5, impl, bind_seat);
  impl->outputGlobal = wl_global_create(impl->display, &wl_output_interface, 3,
                                        impl, bind_output);
  impl->xdgGlobal = wl_global_create(impl->display, &xdg_wm_base_interface, 3,
                                     impl, bind_xdg_wm_base);
  impl->dmabufGlobal = wl_global_create(
      impl->display, &zwp_linux_dmabuf_v1_interface, 3, impl, bind_dmabuf);

  impl->keymap.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (impl->keymap.context) {
    xkb_rule_names names = {};
    impl->keymap.keymap = xkb_keymap_new_from_names(
        impl->keymap.context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (impl->keymap.keymap) {
      impl->keymap.state = xkb_state_new(impl->keymap.keymap);
      char *str = xkb_keymap_get_as_string(impl->keymap.keymap,
                                           XKB_KEYMAP_FORMAT_TEXT_V1);
      if (str) {
        impl->keymap.size = strlen(str) + 1;
        impl->keymap.fd = createKeymapFile(str, impl->keymap.size);
        free(str);
      }
    }
  }

  impl->running.store(true);
  impl->thread = std::thread([this]() {
    wl_display_run(impl->display);
    impl->running.store(false);
  });

  LOGI("WaylandSignCompositor: started on %s\n", impl->displayName.c_str());
  return true;
}

void WaylandSignCompositor::stop() {
  if (!impl->running.load())
    return;

  wl_display_terminate(impl->display);
  if (impl->thread.joinable())
    impl->thread.join();

  if (impl->keymap.fd >= 0)
    close(impl->keymap.fd);
  if (impl->keymap.state)
    xkb_state_unref(impl->keymap.state);
  if (impl->keymap.keymap)
    xkb_keymap_unref(impl->keymap.keymap);
  if (impl->keymap.context)
    xkb_context_unref(impl->keymap.context);

  if (impl->display) {
    wl_display_destroy(impl->display);
    impl->display = nullptr;
  }
  impl->running.store(false);
}

bool WaylandSignCompositor::isRunning() const { return impl->running.load(); }

std::string WaylandSignCompositor::getDisplayName() const {
  return impl->displayName;
}

void WaylandSignCompositor::setSurfaceSize(int width, int height) {
  impl->surfaceWidth = width;
  impl->surfaceHeight = height;

  if (impl->primarySurface && impl->primarySurface->xdgToplevel) {
    wl_array states;
    wl_array_init(&states);
    xdg_toplevel_send_configure(impl->primarySurface->xdgToplevel, width,
                                height, &states);
    wl_array_release(&states);
  }
}

bool WaylandSignCompositor::acquireLatestFrame(DmabufFrame &out) {
  std::lock_guard<std::mutex> lock(impl->frameMutex);
  if (!impl->hasFrame)
    return false;

  out = impl->latestFrame;
  for (int i = 0; i < out.planeCount; ++i) {
    if (out.planes[i].fd >= 0) {
      out.planes[i].fd = dup(out.planes[i].fd);
    }
  }
  return true;
}

void WaylandSignCompositor::releaseFrame(DmabufFrame &frame) {
  for (int i = 0; i < frame.planeCount; ++i) {
    if (frame.planes[i].fd >= 0) {
      close(frame.planes[i].fd);
      frame.planes[i].fd = -1;
    }
  }
}

void WaylandSignCompositor::setPointerFocus(bool focused) {
  impl->pointerFocused = focused;

  if (!focused && impl->pointerResource && impl->pointerEntered &&
      impl->primarySurface && impl->primarySurface->resource) {
    uint32_t serial = wl_display_next_serial(impl->display);
    wl_pointer_send_leave(impl->pointerResource, serial,
                          impl->primarySurface->resource);
    impl->pointerEntered = false;
  }
}

void WaylandSignCompositor::sendPointerMotion(float u, float v) {
  if (!impl->pointerResource || !impl->primarySurface ||
      !impl->primarySurface->resource || !impl->pointerFocused) {
    return;
  }

  uint32_t serial = wl_display_next_serial(impl->display);
  if (!impl->pointerEntered) {
    wl_pointer_send_enter(impl->pointerResource, serial,
                          impl->primarySurface->resource,
                          wl_fixed_from_double(u * impl->surfaceWidth),
                          wl_fixed_from_double(v * impl->surfaceHeight));
    impl->pointerEntered = true;
  }

  uint32_t time = (uint32_t)getTimeMs();
  wl_pointer_send_motion(impl->pointerResource, time,
                         wl_fixed_from_double(u * impl->surfaceWidth),
                         wl_fixed_from_double(v * impl->surfaceHeight));
}

void WaylandSignCompositor::sendPointerButton(uint32_t button, bool pressed) {
  if (!impl->pointerResource || !impl->primarySurface ||
      !impl->primarySurface->resource || !impl->pointerFocused) {
    return;
  }

  uint32_t serial = wl_display_next_serial(impl->display);
  uint32_t state = pressed ? WL_POINTER_BUTTON_STATE_PRESSED
                           : WL_POINTER_BUTTON_STATE_RELEASED;
  uint32_t time = (uint32_t)getTimeMs();
  wl_pointer_send_button(impl->pointerResource, serial, time, button, state);
}

void WaylandSignCompositor::sendKey(uint32_t key, bool pressed) {
  if (!impl->keyboardResource || !impl->primarySurface ||
      !impl->primarySurface->resource) {
    return;
  }

  uint32_t evdev = mapKeyToEvdev(key);
  if (evdev == 0)
    return;

  uint32_t serial = wl_display_next_serial(impl->display);
  wl_array keys;
  wl_array_init(&keys);
  wl_keyboard_send_enter(impl->keyboardResource, serial,
                         impl->primarySurface->resource, &keys);
  wl_array_release(&keys);

  uint32_t time = (uint32_t)getTimeMs();
  wl_keyboard_send_key(impl->keyboardResource, serial, time, evdev,
                       pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
                               : WL_KEYBOARD_KEY_STATE_RELEASED);
}

void WaylandSignCompositor::sendText(const std::string &) {}
