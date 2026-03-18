#include "Font.h"

// #include "SharedConstants.h"
#include "../../App.h"
#include "../../util/Mth.h"
#include "../Options.h"
#include "../renderer/Tesselator.h"
#include "../renderer/Textures.h"
#include <cstring>

namespace {
GraphicsBackend *g_fontGraphicsBackend = nullptr;
float g_fontGraphicsWidth = 0.0f;
float g_fontGraphicsHeight = 0.0f;

auto fontBackendEnabled() -> bool {
  return g_fontGraphicsBackend &&
      g_fontGraphicsBackend->kind() == GraphicsBackendKind::Vulkan &&
      g_fontGraphicsWidth > 0.0f && g_fontGraphicsHeight > 0.0f;
}

void transformGuiPoint(
    const GLfloat *modelView, float x, float y, float &outX, float &outY) {
  outX = modelView[0] * x + modelView[4] * y + modelView[12];
  outY = modelView[1] * x + modelView[5] * y + modelView[13];
}

auto snapToPixel(float value) -> float {
  return value >= 0.0f ? (float)((int)(value + 0.5f))
                       : (float)((int)(value - 0.5f));
}
} // namespace

Font::Font(Options *options, const std::string &name, Textures *textures)
    : options(options), fontTexture(0), fontName(name), index(0), count(0),
      _textures(textures), _x(0), _y(0), _cols(16), _rows(16), _charOffset(0),
      _textureWidth(128), _textureHeight(128), lineHeight(DefaultLineHeight) {
  init(options);
}

// Font::Font( Options* options, const std::string& name, Textures* textures,
// int imgW, int imgH, int x, int y, int cols, int rows, unsigned char
// charOffset )
//:	options(options),
//	fontTexture(0),
//	fontName(name),
//	index(0),
//	count(0),
//	_textures(textures),
//	_x(x), _y(y),
//	_cols(cols), _rows(rows),
//	_charOffset(charOffset)
//{
//	init(options);
//}

void Font::onGraphicsReset() { init(options); }

void Font::setGraphicsBackend(
    GraphicsBackend *graphicsBackend, float width, float height) {
  g_fontGraphicsBackend = graphicsBackend;
  g_fontGraphicsWidth = width;
  g_fontGraphicsHeight = height;
}

void Font::init(Options *options) {
  fontTexture = _textures->loadTexture(fontName);
  const TextureData *tex = _textures->getTemporaryTextureData(fontTexture);

  if (!tex)
    return;

  _textureWidth = tex->w > 0 ? tex->w : 128;
  _textureHeight = tex->h > 0 ? tex->h : 128;
  unsigned char *rawPixels = tex->data;

  const int numChars = _rows * _cols;
  for (int i = 0; i < numChars; i++) {
    int xt = i % _cols;
    int yt = i / _cols;

    int x = 7;
    for (; x >= 0; x--) {
      int xPixel = _x + xt * 8 + x;
      bool emptyColumn = true;
      for (int y = 0; y < 8 && emptyColumn; y++) {
        int yPixel = _y + (yt * 8 + y) * tex->w;
        unsigned char pixelalpha = rawPixels[(xPixel + yPixel) << 2];
        if (pixelalpha > 0)
          emptyColumn = false;
      }
      if (!emptyColumn) {
        break;
      }
    }

    if (i == ' ')
      x = 4 - 2;
    charWidths[i] = x + 2;
    fcharWidths[i] = (float)charWidths[i];
  }

#ifdef USE_VBO
  return; // this <1
#endif

#ifndef USE_VBO
  listPos = glGenLists(256 + 32);

  Tesselator &t = Tesselator::instance;
  for (int i = 0; i < 256; i++) {
    glNewList(listPos + i, GL_COMPILE);
    // @attn @huge @note: This is some dangerous code right here / Aron, added
    // ^1
    t.begin();
    buildChar(i);
    t.end(false, -1);

    glTranslatef2((GLfloat)charWidths[i], 0.0f, 0.0f);
    glEndList();
  }

  for (int i = 0; i < 32; i++) {
    int br = ((i >> 3) & 1) * 0x55;
    int r = ((i >> 2) & 1) * 0xaa + br;
    int g = ((i >> 1) & 1) * 0xaa + br;
    int b = ((i >> 0) & 1) * 0xaa + br;
    if (i == 6) {
      r += 0x55;
    }
    bool darken = i >= 16;

    if (options->anaglyph3d) {
      int cr = (r * 30 + g * 59 + b * 11) / 100;
      int cg = (r * 30 + g * 70) / (100);
      int cb = (r * 30 + b * 70) / (100);

      r = cr;
      g = cg;
      b = cb;
    }

    // color = r << 16 | g << 8 | b;
    if (darken) {
      r /= 4;
      g /= 4;
      b /= 4;
    }

    glNewList(listPos + 256 + i, GL_COMPILE);
    glColor3f(r / 255.0f, g / 255.0f, b / 255.0f);
    glEndList();
  }
#endif
}

void Font::drawShadow(const std::string &str, float x, float y, int color) {
  draw(str, x + 1, y + 1, color, true);
  draw(str, x, y, color);
}
void Font::drawShadow(const char *str, float x, float y, int color) {
  draw(str, x + 1, y + 1, color, true);
  draw(str, x, y, color);
}

void Font::draw(const std::string &str, float x, float y, int color) {
  draw(str, x, y, color, false);
}

void Font::draw(const char *str, float x, float y, int color) {
  draw(str, x, y, color, false);
}

void Font::draw(const char *str, float x, float y, int color, bool darken) {
#ifdef USE_VBO
  drawSlow(str, x, y, color, darken);
#endif
}

void Font::draw(
    const std::string &str, float x, float y, int color, bool darken) {
#ifdef USE_VBO
  drawSlow(str, x, y, color, darken);
  return;
#endif

  if (str.empty())
    return;

  if (darken) {
    int oldAlpha = color & 0xff000000;
    color = (color & 0xfcfcfc) >> 2;
    color += oldAlpha;
  }

  _textures->loadAndBindTexture(fontName);
  float r = ((color >> 16) & 0xff) / 255.0f;
  float g = ((color >> 8) & 0xff) / 255.0f;
  float b = ((color) & 0xff) / 255.0f;
  float a = ((color >> 24) & 0xff) / 255.0f;
  if (a == 0)
    a = 1;
  glColor4f2(r, g, b, a);

  static const std::string hex("0123456789abcdef");

  index = 0;
  glPushMatrix2();
  glTranslatef2((GLfloat)x, (GLfloat)y, 0.0f);
  for (unsigned int i = 0; i < str.length(); i++) {
    while (str.length() > i + 1 && str[i] == '§') {
      int cc = hex.find((char)tolower(str[i + 1]));
      if (cc < 0 || cc > 15)
        cc = 15;
      lists[index++] = listPos + 256 + cc + (darken ? 16 : 0);

      if (index == 1024) {
        count = index;
        index = 0;
#ifndef USE_VBO
        glCallLists(count, GL_UNSIGNED_INT, lists);
#endif
        count = 1024;
      }

      i += 2;
    }

    if (i < str.length()) {
      // int ch = SharedConstants.acceptableLetters.indexOf(str.charAt(i));
      char ch = str[i];
      if (ch >= 0) {
        // ib.put(listPos + ch + 32);
        lists[index++] = listPos + ch;
      }
    }

    if (index == 1024) {
      count = index;
      index = 0;
#ifndef USE_VBO
      glCallLists(count, GL_UNSIGNED_INT, lists);
#endif
      count = 1024;
    }
  }
  count = index;
  index = 0;
#ifndef USE_VBO
  glCallLists(count, GL_UNSIGNED_INT, lists);
#endif
  glPopMatrix2();
}

int Font::width(const std::string &str) {
  int maxLen = 0;
  int len = 0;

  for (unsigned int i = 0; i < str.length(); i++) {
    if (str[i] == '§') {
      i++;
    } else {
      // int ch = SharedConstants.acceptableLetters.indexOf(str.charAt(i));
      // if (ch >= 0) {
      //     len += charWidths[ch + 32];
      // }
      if (str[i] == '\n') {
        if (len > maxLen)
          maxLen = len;
        len = 0;
      } else {
        int charWidth = charWidths[(unsigned char)str[i]];
        len += charWidth;
      }
    }
  }
  return maxLen > len ? maxLen : len;
}

int Font::height(const std::string &str) {
  int h = 0;
  bool hasLine = false;
  for (unsigned int i = 0; i < str.length(); ++i) {
    if (str[i] == '\n')
      hasLine = true;
    else {
      if (hasLine)
        h += lineHeight;
      hasLine = false;
    }
  }
  return h;
}

std::string Font::sanitize(const std::string &str) {
  std::string sanitized(str.length() + 1, 0);
  int j = 0;

  for (unsigned int i = 0; i < str.length(); i++) {
    if (str[i] == '§') {
      i++;
      //} else if (SharedConstants.acceptableLetters.indexOf(str.charAt(i)) >=
      // 0) {
    } else {
      sanitized[j++] = str[i];
    }
  }
  return sanitized.erase(j);
}

void Font::drawWordWrap(
    const std::string &str, float x, float y, float w, int col) {
  char *cstr = new char[str.length() + 1];
  strncpy(cstr, str.c_str(), str.length());
  cstr[str.length()] = 0;

  const char *lims = " \n\t\r";
  char *ptok = strtok(cstr, lims);

  std::vector<std::string> words;
  while (ptok != NULL) {
    words.push_back(ptok);
    ptok = strtok(NULL, lims);
  }

  delete[] cstr;

  int pos = 0;
  while (pos < (int)words.size()) {
    std::string line = words[pos++] + " ";
    while (pos < (int)words.size() && width(line + words[pos]) < w) {
      line += words[pos++] + " ";
    }
    drawShadow(line, x, y, col);
    y += lineHeight;
  }
}

void Font::drawSlow(const std::string &str, float x, float y, int color,
    bool darken /*= false*/) {
  drawSlow(str.c_str(), x, y, color, darken);
}
void Font::drawSlow(
    const char *str, float x, float y, int color, bool darken /*= false*/) {
  if (!str)
    return;

  if (darken) {
    int oldAlpha = color & 0xff000000;
    color = (color & 0xfcfcfc) >> 2;
    color += oldAlpha;
  }

  if (!Textures::isTextureIdValid(fontTexture)) {
    fontTexture = _textures->loadTexture(fontName);
  }
  if (!Textures::isTextureIdValid(fontTexture)) {
    return;
  }
  _textures->bind(fontTexture);

  int alpha = (0xff000000 & color) >> 24;
  if (!alpha)
    alpha = 0xff;

  if (fontBackendEnabled()) {
    const float r = (float)((color >> 16) & 0xff) / 255.0f;
    const float g = (float)((color >> 8) & 0xff) / 255.0f;
    const float b = (float)(color & 0xff) / 255.0f;
    const float a = (float)alpha / 255.0f;
    const float glyphWidth = 7.99f;
    const float glyphHeight = 7.99f;
    const float textureWidth = (float)(_textureWidth > 0 ? _textureWidth : 128);
    const float textureHeight =
        (float)(_textureHeight > 0 ? _textureHeight : 128);
    float tessScaleX = 1.0f;
    float tessScaleY = 1.0f;
    Tesselator::instance.getScale2d(tessScaleX, tessScaleY);
    GLfloat modelView[16];
    glesGetTrackedMatrix(GL_MODELVIEW_MATRIX, modelView);

    float xOffset = 0.0f;
    float yOffset = 0.0f;
    for (const unsigned char *it = (const unsigned char *)str; *it; ++it) {
      const unsigned char ch = *it;
      if (ch == '\n') {
        xOffset = 0.0f;
        yOffset += (float)lineHeight;
        continue;
      }

      const float ix = (float)((ch & 15) * 8);
      const float iy = (float)((ch >> 4) * 8);
      const float localX0 = tessScaleX * (x + xOffset);
      const float localY0 = tessScaleY * (y + yOffset);
      const float localX1 = tessScaleX * (x + xOffset + glyphWidth);
      const float localY1 = tessScaleY * (y + yOffset + glyphHeight);
      float screenX0 = 0.0f;
      float screenY0 = 0.0f;
      float screenX1 = 0.0f;
      float screenY1 = 0.0f;
      transformGuiPoint(modelView, localX0, localY0, screenX0, screenY0);
      transformGuiPoint(modelView, localX1, localY1, screenX1, screenY1);

      screenX0 = snapToPixel(screenX0);
      screenY0 = snapToPixel(screenY0);
      screenX1 = snapToPixel(screenX1);
      screenY1 = snapToPixel(screenY1);
      if (screenX1 <= screenX0) {
        screenX1 = screenX0 + 1.0f;
      }
      if (screenY1 <= screenY0) {
        screenY1 = screenY0 + 1.0f;
      }

      GraphicsTexturedQuad quad;
      quad.x = screenX0;
      quad.y = screenY0;
      quad.width = screenX1 - screenX0;
      quad.height = screenY1 - screenY0;
      quad.canvasWidth = g_fontGraphicsWidth;
      quad.canvasHeight = g_fontGraphicsHeight;
      quad.u0 = (ix + 0.5f) / textureWidth;
      quad.v0 = (iy + 0.5f) / textureHeight;
      quad.u1 = (ix + glyphWidth - 0.5f) / textureWidth;
      quad.v1 = (iy + glyphHeight - 0.5f) / textureHeight;
      quad.r = r;
      quad.g = g;
      quad.b = b;
      quad.a = a;
      if (!g_fontGraphicsBackend->drawTexturedQuad(quad)) {
        return;
      }
      xOffset += fcharWidths[ch];
    }
    return;
  }

  Tesselator &t = Tesselator::instance;
  t.begin();
  t.color((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff, alpha);

  t.addOffset((float)x, (float)y, 0);
  float xOffset = 0;
  float yOffset = 0;

  while (unsigned char ch = *(str++)) {
    if (ch == '\n') {
      xOffset = 0;
      yOffset += lineHeight;
    } else {
      buildChar(ch, xOffset, yOffset);
      xOffset += fcharWidths[ch];
    }
  }
  t.draw();
  t.addOffset(-(float)x, -(float)y, 0);
}

void Font::buildChar(unsigned char i, float x /*= 0*/, float y /*=0*/) {
  Tesselator &t = Tesselator::instance;

  // i -= _charOffset;
  // int ix = (i % _cols) * 8 + _x;
  // int iy = (i / _cols) * 8 + _y;
  float ix = (float)((i & 15) * 8);
  float iy = (float)((i >> 4) * 8);
  float s = 7.99f;

  const float textureWidth = (float)(_textureWidth > 0 ? _textureWidth : 128);
  const float textureHeight =
      (float)(_textureHeight > 0 ? _textureHeight : 128);

  t.vertexUV(x, y + s, 0, ix / textureWidth, (iy + s) / textureHeight);
  t.vertexUV(
      x + s, y + s, 0, (ix + s) / textureWidth, (iy + s) / textureHeight);
  t.vertexUV(x + s, y, 0, (ix + s) / textureWidth, iy / textureHeight);
  t.vertexUV(x, y, 0, ix / textureWidth, iy / textureHeight);
}
