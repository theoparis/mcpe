#include "GuiComponent.h"

#include "../../App.h"
#include "../renderer/Tesselator.h"
#include "../renderer/gles.h"
#include "Font.h"

namespace {
GraphicsBackend *g_graphicsBackend = nullptr;
float g_graphicsWidth = 0.0f;
float g_graphicsHeight = 0.0f;

auto backendQuadsEnabled() -> bool {
  return g_graphicsBackend &&
      g_graphicsBackend->kind() == GraphicsBackendKind::Vulkan;
}

auto quadColorFromARGB(int color) -> GraphicsQuadColor {
  GraphicsQuadColor out;
  out.a = (float)((color >> 24) & 0xff) / 255.0f;
  out.r = (float)((color >> 16) & 0xff) / 255.0f;
  out.g = (float)((color >> 8) & 0xff) / 255.0f;
  out.b = (float)(color & 0xff) / 255.0f;
  return out;
}

void setUniformQuadColor(GraphicsQuad &quad, const GraphicsQuadColor &color) {
  quad.topLeft = color;
  quad.topRight = color;
  quad.bottomRight = color;
  quad.bottomLeft = color;
}

auto trackedBlendMode() -> GraphicsBlendMode {
  GLenum src = GL_SRC_ALPHA;
  GLenum dst = GL_ONE_MINUS_SRC_ALPHA;
  glesGetTrackedBlendFunc(src, dst);
  if (src == GL_ZERO && dst == GL_ONE_MINUS_SRC_COLOR) {
    return GraphicsBlendMode::ZeroOneMinusSrcColor;
  }
  if (src == GL_ONE_MINUS_DST_COLOR && dst == GL_ONE_MINUS_SRC_COLOR) {
    return GraphicsBlendMode::OneMinusDstColorOneMinusSrcColor;
  }
  return GraphicsBlendMode::Alpha;
}
}

GuiComponent::GuiComponent() : blitOffset(0) {}

GuiComponent::~GuiComponent() {}

void GuiComponent::setGraphicsBackend(
    GraphicsBackend *graphicsBackend, float width, float height) {
  g_graphicsBackend = graphicsBackend;
  g_graphicsWidth = width;
  g_graphicsHeight = height;
}

bool GuiComponent::tryDrawQuad(const GraphicsQuad &quad) {
  if (!backendQuadsEnabled()) {
    return false;
  }

  GraphicsQuad resolved = quad;
  if (resolved.canvasWidth <= 0.0f) {
    resolved.canvasWidth = g_graphicsWidth;
  }
  if (resolved.canvasHeight <= 0.0f) {
    resolved.canvasHeight = g_graphicsHeight;
  }
  return g_graphicsBackend->drawQuad(resolved);
}

bool GuiComponent::tryDrawTexturedQuad(GraphicsTexturedQuad quad, int color) {
  if (!backendQuadsEnabled()) {
    return false;
  }

  if (quad.canvasWidth <= 0.0f) {
    quad.canvasWidth = g_graphicsWidth;
  }
  if (quad.canvasHeight <= 0.0f) {
    quad.canvasHeight = g_graphicsHeight;
  }

  const GraphicsQuadColor tint = quadColorFromARGB(color);
  quad.r = tint.r;
  quad.g = tint.g;
  quad.b = tint.b;
  quad.a = tint.a;
  return g_graphicsBackend->drawTexturedQuad(quad);
}

void GuiComponent::drawCenteredString(
    Font *font, const std::string &str, int x, int y, int color) {
  font->drawShadow(str, (float)(x - font->width(str) / 2),
      (float)(y - font->height(str) / 2), color);
}

void GuiComponent::drawString(
    Font *font, const std::string &str, int x, int y, int color) {
  font->drawShadow(str, (float)x, (float)y /*- font->height(str)/2*/, color);
}

bool GuiComponent::drawTexturedQuad(float x, float y, float w, float h,
    float u0, float v0, float u1, float v1, int color) {
  GraphicsTexturedQuad quad;
  quad.x = x;
  quad.y = y;
  quad.width = w;
  quad.height = h;
  quad.canvasWidth = g_graphicsWidth;
  quad.canvasHeight = g_graphicsHeight;
  quad.u0 = u0;
  quad.v0 = v0;
  quad.u1 = u1;
  quad.v1 = v1;
  return tryDrawTexturedQuad(quad, color);
}

bool GuiComponent::drawQuad(float x, float y, float w, float h, int color) {
  GraphicsQuad quad;
  quad.x = x;
  quad.y = y;
  quad.width = w;
  quad.height = h;
  quad.canvasWidth = g_graphicsWidth;
  quad.canvasHeight = g_graphicsHeight;
  quad.textured = false;
  const GraphicsQuadColor tint = quadColorFromARGB(color);
  setUniformQuadColor(quad, tint);
  quad.blendMode = trackedBlendMode();
  return tryDrawQuad(quad);
}

void GuiComponent::blit(
    int x, int y, int sx, int sy, int w, int h, int sw /*=0*/, int sh /*=0*/) {
  if (!sw)
    sw = w;
  if (!sh)
    sh = h;
  float us = 1 / 256.0f;
  float vs = 1 / 256.0f;
  if (g_graphicsBackend &&
      g_graphicsBackend->kind() == GraphicsBackendKind::Vulkan) {
    float r, g, b, a;
    glesGetTrackedColor4f(r, g, b, a);

    GraphicsTexturedQuad quad;
    quad.x = (float)x;
    quad.y = (float)y;
    quad.width = (float)w;
    quad.height = (float)h;
    quad.canvasWidth = g_graphicsWidth;
    quad.canvasHeight = g_graphicsHeight;
    quad.u0 = (float)sx * us;
    quad.v0 = (float)sy * vs;
    quad.u1 = (float)(sx + sw) * us;
    quad.v1 = (float)(sy + sh) * vs;
    quad.r = r;
    quad.g = g;
    quad.b = b;
    quad.a = a;
    quad.blendMode = trackedBlendMode();
    if (g_graphicsBackend->drawTexturedQuad(quad)) {
      return;
    }
  }

  Tesselator &t = Tesselator::instance;
  t.begin();
  t.vertexUV((float)(x), (float)(y + h), blitOffset, (float)(sx)*us,
      (float)(sy + sh) * vs);
  t.vertexUV((float)(x + w), (float)(y + h), blitOffset, (float)(sx + sw) * us,
      (float)(sy + sh) * vs);
  t.vertexUV((float)(x + w), (float)(y), blitOffset, (float)(sx + sw) * us,
      (float)(sy)*vs);
  t.vertexUV(
      (float)(x), (float)(y), blitOffset, (float)(sx)*us, (float)(sy)*vs);
  t.draw();
}
void GuiComponent::blit(float x, float y, int sx, int sy, float w, float h,
    int sw /*=0*/, int sh /*=0*/) {
  if (!sw)
    sw = (int)w;
  if (!sh)
    sh = (int)h;
  float us = 1 / 256.0f;
  float vs = 1 / 256.0f;
  if (g_graphicsBackend &&
      g_graphicsBackend->kind() == GraphicsBackendKind::Vulkan) {
    float r, g, b, a;
    glesGetTrackedColor4f(r, g, b, a);

    GraphicsTexturedQuad quad;
    quad.x = x;
    quad.y = y;
    quad.width = w;
    quad.height = h;
    quad.canvasWidth = g_graphicsWidth;
    quad.canvasHeight = g_graphicsHeight;
    quad.u0 = (float)sx * us;
    quad.v0 = (float)sy * vs;
    quad.u1 = (float)(sx + sw) * us;
    quad.v1 = (float)(sy + sh) * vs;
    quad.r = r;
    quad.g = g;
    quad.b = b;
    quad.a = a;
    quad.blendMode = trackedBlendMode();
    if (g_graphicsBackend->drawTexturedQuad(quad)) {
      return;
    }
  }

  Tesselator &t = Tesselator::instance;
  t.begin();
  t.vertexUV(x, y + h, blitOffset, (float)(sx)*us, (float)(sy + sh) * vs);
  t.vertexUV(
      x + w, y + h, blitOffset, (float)(sx + sw) * us, (float)(sy + sh) * vs);
  t.vertexUV(x + w, y, blitOffset, (float)(sx + sw) * us, (float)(sy)*vs);
  t.vertexUV(x, y, blitOffset, (float)(sx)*us, (float)(sy)*vs);
  t.draw();
}

void GuiComponent::fill(int x0, int y0, int x1, int y1, int col) {
  fill((float)x0, (float)y0, (float)x1, (float)y1, col);
}
void GuiComponent::fill(float x0, float y0, float x1, float y1, int col) {
  GraphicsQuad quad;
  quad.x = x0;
  quad.y = y0;
  quad.width = x1 - x0;
  quad.height = y1 - y0;
  quad.canvasWidth = g_graphicsWidth;
  quad.canvasHeight = g_graphicsHeight;
  quad.textured = false;
  setUniformQuadColor(quad, quadColorFromARGB(col));
  if (tryDrawQuad(quad)) {
    return;
  }

  // float a = ((col >> 24) & 0xff) / 255.0f;
  // float r = ((col >> 16) & 0xff) / 255.0f;
  // float g = ((col >> 8) & 0xff) / 255.0f;
  // float b = ((col) & 0xff) / 255.0f;
  // glColor4f2(r, g, b, a);

  Tesselator &t = Tesselator::instance;
  glEnable2(GL_BLEND);
  glDisable2(GL_TEXTURE_2D);
  glBlendFunc2(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // LOGI("col: %f, %f, %f, %f\n", r, g, b, a);
  t.begin();
  const int color =
      (col & 0xff00ff00) | ((col & 0xff0000) >> 16) | ((col & 0xff) << 16);
  t.colorABGR(color);
  t.vertex(x0, y1, 0);
  t.vertex(x1, y1, 0);
  t.vertex(x1, y0, 0);
  t.vertex(x0, y0, 0);
  t.draw();
  glEnable2(GL_TEXTURE_2D);
  glDisable2(GL_BLEND);
}

void GuiComponent::fillGradient(
    int x0, int y0, int x1, int y1, int col1, int col2) {
  fillGradient((float)x0, (float)y0, (float)x1, (float)y1, col1, col2);
}
void GuiComponent::fillGradient(
    float x0, float y0, float x1, float y1, int col1, int col2) {
  GraphicsQuad quad;
  quad.x = x0;
  quad.y = y0;
  quad.width = x1 - x0;
  quad.height = y1 - y0;
  quad.canvasWidth = g_graphicsWidth;
  quad.canvasHeight = g_graphicsHeight;
  quad.textured = false;
  quad.topLeft = quadColorFromARGB(col1);
  quad.topRight = quadColorFromARGB(col1);
  quad.bottomRight = quadColorFromARGB(col2);
  quad.bottomLeft = quadColorFromARGB(col2);
  if (tryDrawQuad(quad)) {
    return;
  }

  float a1 = ((col1 >> 24) & 0xff) / 255.0f;
  float r1 = ((col1 >> 16) & 0xff) / 255.0f;
  float g1 = ((col1 >> 8) & 0xff) / 255.0f;
  float b1 = ((col1) & 0xff) / 255.0f;

  float a2 = ((col2 >> 24) & 0xff) / 255.0f;
  float r2 = ((col2 >> 16) & 0xff) / 255.0f;
  float g2 = ((col2 >> 8) & 0xff) / 255.0f;
  float b2 = ((col2) & 0xff) / 255.0f;
  glDisable2(GL_TEXTURE_2D);
  glEnable2(GL_BLEND);
  glDisable2(GL_ALPHA_TEST);
  glBlendFunc2(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glShadeModel2(GL_SMOOTH);

  Tesselator &t = Tesselator::instance;
  t.begin();
  t.color(r1, g1, b1, a1);
  t.vertex(x1, y0, 0);
  t.vertex(x0, y0, 0);
  t.color(r2, g2, b2, a2);
  t.vertex(x0, y1, 0);
  t.vertex(x1, y1, 0);
  t.draw();

  glShadeModel2(GL_FLAT);
  glDisable2(GL_BLEND);
  glEnable2(GL_ALPHA_TEST);
  glEnable2(GL_TEXTURE_2D);
}
void GuiComponent::fillHorizontalGradient(
    int x0, int y0, int x1, int y1, int col1, int col2) {
  fillHorizontalGradient(
      (float)x0, (float)y0, (float)x1, (float)y1, col1, col2);
}
void GuiComponent::fillHorizontalGradient(
    float x0, float y0, float x1, float y1, int col1, int col2) {
  GraphicsQuad quad;
  quad.x = x0;
  quad.y = y0;
  quad.width = x1 - x0;
  quad.height = y1 - y0;
  quad.canvasWidth = g_graphicsWidth;
  quad.canvasHeight = g_graphicsHeight;
  quad.textured = false;
  quad.topLeft = quadColorFromARGB(col1);
  quad.bottomLeft = quadColorFromARGB(col1);
  quad.topRight = quadColorFromARGB(col2);
  quad.bottomRight = quadColorFromARGB(col2);
  if (tryDrawQuad(quad)) {
    return;
  }

  float a1 = ((col1 >> 24) & 0xff) / 255.0f;
  float r1 = ((col1 >> 16) & 0xff) / 255.0f;
  float g1 = ((col1 >> 8) & 0xff) / 255.0f;
  float b1 = ((col1) & 0xff) / 255.0f;

  float a2 = ((col2 >> 24) & 0xff) / 255.0f;
  float r2 = ((col2 >> 16) & 0xff) / 255.0f;
  float g2 = ((col2 >> 8) & 0xff) / 255.0f;
  float b2 = ((col2) & 0xff) / 255.0f;
  glDisable2(GL_TEXTURE_2D);
  glEnable2(GL_BLEND);
  glDisable2(GL_ALPHA_TEST);
  glBlendFunc2(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glShadeModel2(GL_SMOOTH);

  Tesselator &t = Tesselator::instance;
  t.begin();
  t.color(r2, g2, b2, a2);
  t.vertex(x1, y0, 0);
  t.color(r1, g1, b1, a1);
  t.vertex(x0, y0, 0);
  t.color(r1, g1, b1, a1);
  t.vertex(x0, y1, 0);
  t.color(r2, g2, b2, a2);
  t.vertex(x1, y1, 0);
  t.draw();

  glShadeModel2(GL_FLAT);
  glDisable2(GL_BLEND);
  glEnable2(GL_ALPHA_TEST);
  glEnable2(GL_TEXTURE_2D);
}
