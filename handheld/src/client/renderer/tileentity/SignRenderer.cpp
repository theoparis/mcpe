#include "SignRenderer.h"
#include "../../../world/level/tile/Tile.h"
#include "../../../world/level/tile/entity/SignTileEntity.h"
#include "../Tesselator.h"
#if defined(MCPE_ENABLE_WAYLAND_SIGN) && defined(LINUX)
#include "../../../platform/wayland/WaylandSignTexture.h"
#endif

void SignRenderer::render(TileEntity *te, float x, float y, float z, float a) {
  SignTileEntity *sign = (SignTileEntity *)te;
  Tile *tile = sign->getTile();

  glPushMatrix();
  float size = 16 / 24.0f;
  if (tile == Tile::sign) {
    glTranslatef(x + 0.5f, y + 0.75f * size, z + 0.5f);
    float rot = sign->getData() * 360 / 16.0f;
    glRotatef(-rot, 0, 1, 0);
    signModel.cube2.visible = true;
  } else {
    int face = sign->getData();
    float rot = 0;

    if (face == 2)
      rot = 180;
    if (face == 4)
      rot = 90;
    if (face == 5)
      rot = -90;

    glTranslatef(x + 0.5f, y + 0.75f * size, z + 0.5f);
    glRotatef(-rot, 0, 1, 0);
    glTranslatef(0, -5 / 16.0f, -7 / 16.0f);

    signModel.cube2.visible = false;
  }

  bindTexture("item/sign.png");

  glPushMatrix();
  glScalef(size, -size, -size);
  signModel.render();
  glPopMatrix();

  bool renderedWayland = false;
#if defined(MCPE_ENABLE_WAYLAND_SIGN) && defined(LINUX)
  static WaylandSignTexture waylandTexture;
  if (sign->messages[0] == "[wayland]") {
    waylandTexture.update();
    if (waylandTexture.isReady()) {
      glBindTexture2(GL_TEXTURE_2D, waylandTexture.getTextureId());
      glColor4f(1, 1, 1, 1);

      float s = 1 / 60.0f * size;
      glTranslatef(0, 0.5f * size, 0.07f * size);
      glScalef(s, -s, s);
      glNormal3f(0, 0, -1 * s);
      glDepthMask(false);

      float quadH = 30.0f;
      float quadW = quadH;
      const int texW = waylandTexture.getWidth();
      const int texH = waylandTexture.getHeight();
      if (texW > 0 && texH > 0) {
        const float aspect = (float)texW / (float)texH;
        quadW = quadH * aspect;
        if (quadW > 60.0f) {
          quadW = 60.0f;
          quadH = quadW / aspect;
        }
      }

      const float x0 = -quadW / 2.0f;
      const float y0 = -quadH / 2.0f;
      const float x1 = quadW / 2.0f;
      const float y1 = quadH / 2.0f;

      Tesselator &t = Tesselator::instance;
      t.begin();
      t.color(0xffffffff);
      t.vertexUV(x0, y0, 0, 0, 1);
      t.vertexUV(x1, y0, 0, 1, 1);
      t.vertexUV(x1, y1, 0, 1, 0);
      t.vertexUV(x0, y1, 0, 0, 0);
      t.draw();

      glDepthMask(true);
      glColor4f(1, 1, 1, 1);
      renderedWayland = true;
    }
  }
#endif

  if (!renderedWayland) {
    Font *font = getFont();

    float s = 1 / 60.0f * size;
    glTranslatef(0, 0.5f * size, 0.07f * size);
    glScalef(s, -s, s);
    glNormal3f(0, 0, -1 * s);
    glDepthMask(false);

    int col = 0;
    float yy = (float)(SignTileEntity::NUM_LINES * -5);
    for (int i = 0; i < SignTileEntity::NUM_LINES; i++) {
      std::string &msg = sign->messages[i];
      if (i == sign->selectedLine) {
        std::string s = "> " + msg + " <";
        font->draw(s, (float)-font->width(s) / 2, yy, col);
      } else {
        font->draw(msg, (float)-font->width(msg) / 2, yy, col);
      }
      yy += 10;
    }
    glDepthMask(true);
    glColor4f(1, 1, 1, 1);
  }

  glPopMatrix();
}

void SignRenderer::onGraphicsReset() { signModel.onGraphicsReset(); }
