#include "WaylandSignTexture.h"

#include "../../platform/log.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <unistd.h>

static PFNEGLCREATEIMAGEKHRPROC g_eglCreateImageKHR = nullptr;
static PFNEGLDESTROYIMAGEKHRPROC g_eglDestroyImageKHR = nullptr;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC g_glEGLImageTargetTexture2DOES =
    nullptr;

static bool ensureEglSymbols() {
  if (!g_eglCreateImageKHR) {
    g_eglCreateImageKHR =
        (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
  }
  if (!g_eglDestroyImageKHR) {
    g_eglDestroyImageKHR =
        (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
  }
  if (!g_glEGLImageTargetTexture2DOES) {
    g_glEGLImageTargetTexture2DOES =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress(
            "glEGLImageTargetTexture2DOES");
  }
  return g_eglCreateImageKHR && g_eglDestroyImageKHR &&
         g_glEGLImageTargetTexture2DOES;
}

WaylandSignTexture::WaylandSignTexture()
    : textureId(0), eglImage(nullptr), lastSequence(0), width(0), height(0) {}

WaylandSignTexture::~WaylandSignTexture() {
  if (eglImage) {
    EGLDisplay display = eglGetCurrentDisplay();
    if (display != EGL_NO_DISPLAY && g_eglDestroyImageKHR) {
      g_eglDestroyImageKHR(display, (EGLImageKHR)eglImage);
    }
    eglImage = nullptr;
  }

  if (textureId != 0) {
    GLuint id = textureId;
    glDeleteTextures(1, &id);
    textureId = 0;
  }
}

bool WaylandSignTexture::update() {
  DmabufFrame frame;
  if (!WaylandSignCompositor::get().acquireLatestFrame(frame)) {
    return false;
  }

  bool updated = false;
  if (frame.sequence > lastSequence) {
    updated = importFrame(frame);
    if (updated) {
      lastSequence = frame.sequence;
    }
  }

  WaylandSignCompositor::get().releaseFrame(frame);
  return updated;
}

bool WaylandSignTexture::importFrame(const DmabufFrame &frame) {
  EGLDisplay display = eglGetCurrentDisplay();
  if (display == EGL_NO_DISPLAY) {
    LOGE("WaylandSignTexture: no EGL display\n");
    return false;
  }

  if (!ensureEglSymbols()) {
    LOGE("WaylandSignTexture: missing EGL DMA-BUF functions\n");
    return false;
  }

  EGLint attribs[64];
  int idx = 0;
  attribs[idx++] = EGL_WIDTH;
  attribs[idx++] = frame.width;
  attribs[idx++] = EGL_HEIGHT;
  attribs[idx++] = frame.height;
  attribs[idx++] = EGL_LINUX_DRM_FOURCC_EXT;
  attribs[idx++] = (EGLint)frame.format;

  if (frame.planeCount > 0) {
    attribs[idx++] = EGL_DMA_BUF_PLANE0_FD_EXT;
    attribs[idx++] = frame.planes[0].fd;
    attribs[idx++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
    attribs[idx++] = (EGLint)frame.planes[0].offset;
    attribs[idx++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
    attribs[idx++] = (EGLint)frame.planes[0].stride;
    attribs[idx++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
    attribs[idx++] = (EGLint)(frame.planes[0].modifier & 0xffffffff);
    attribs[idx++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
    attribs[idx++] = (EGLint)(frame.planes[0].modifier >> 32);
  }

  if (frame.planeCount > 1) {
    attribs[idx++] = EGL_DMA_BUF_PLANE1_FD_EXT;
    attribs[idx++] = frame.planes[1].fd;
    attribs[idx++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
    attribs[idx++] = (EGLint)frame.planes[1].offset;
    attribs[idx++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
    attribs[idx++] = (EGLint)frame.planes[1].stride;
    attribs[idx++] = EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT;
    attribs[idx++] = (EGLint)(frame.planes[1].modifier & 0xffffffff);
    attribs[idx++] = EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT;
    attribs[idx++] = (EGLint)(frame.planes[1].modifier >> 32);
  }

  if (frame.planeCount > 2) {
    attribs[idx++] = EGL_DMA_BUF_PLANE2_FD_EXT;
    attribs[idx++] = frame.planes[2].fd;
    attribs[idx++] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;
    attribs[idx++] = (EGLint)frame.planes[2].offset;
    attribs[idx++] = EGL_DMA_BUF_PLANE2_PITCH_EXT;
    attribs[idx++] = (EGLint)frame.planes[2].stride;
    attribs[idx++] = EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT;
    attribs[idx++] = (EGLint)(frame.planes[2].modifier & 0xffffffff);
    attribs[idx++] = EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT;
    attribs[idx++] = (EGLint)(frame.planes[2].modifier >> 32);
  }

  if (frame.planeCount > 3) {
    attribs[idx++] = EGL_DMA_BUF_PLANE3_FD_EXT;
    attribs[idx++] = frame.planes[3].fd;
    attribs[idx++] = EGL_DMA_BUF_PLANE3_OFFSET_EXT;
    attribs[idx++] = (EGLint)frame.planes[3].offset;
    attribs[idx++] = EGL_DMA_BUF_PLANE3_PITCH_EXT;
    attribs[idx++] = (EGLint)frame.planes[3].stride;
    attribs[idx++] = EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT;
    attribs[idx++] = (EGLint)(frame.planes[3].modifier & 0xffffffff);
    attribs[idx++] = EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT;
    attribs[idx++] = (EGLint)(frame.planes[3].modifier >> 32);
  }

  attribs[idx++] = EGL_NONE;

  EGLImageKHR image = g_eglCreateImageKHR(
      display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attribs);

  if (image == EGL_NO_IMAGE_KHR) {
    LOGE("WaylandSignTexture: eglCreateImageKHR failed\n");
    return false;
  }

  if (eglImage) {
    g_eglDestroyImageKHR(display, (EGLImageKHR)eglImage);
  }
  eglImage = image;

  if (textureId == 0) {
    glGenTextures(1, &textureId);
    glBindTexture2(GL_TEXTURE_2D, textureId);
    glTexParameteri2(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri2(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri2(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri2(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  } else {
    glBindTexture2(GL_TEXTURE_2D, textureId);
  }

  g_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)eglImage);

  width = frame.width;
  height = frame.height;
  return true;
}
