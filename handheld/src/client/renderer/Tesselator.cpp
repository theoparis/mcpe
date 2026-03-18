#include "Tesselator.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

Tesselator Tesselator::instance(
    sizeof(GLfloat) * MAX_FLOATS); // max size in bytes

const int VertexSizeBytes = sizeof(VERTEX);

namespace {

constexpr float kAtlasTileScale = 1.0f / 16.0f;
constexpr float kTileUvEpsilon = 1.0f / 4096.0f;

void convertAtlasUv(float u, float v, float &localU, float &localV,
    float &tileOriginU, float &tileOriginV) {
  const float scaledU = std::clamp(u / kAtlasTileScale, 0.0f, 15.999f);
  const float scaledV = std::clamp(v / kAtlasTileScale, 0.0f, 15.999f);
  const float tileU = std::floor(scaledU);
  const float tileV = std::floor(scaledV);
  tileOriginU = tileU * kAtlasTileScale;
  tileOriginV = tileV * kAtlasTileScale;
  localU = (u - tileOriginU) / kAtlasTileScale;
  localV = (v - tileOriginV) / kAtlasTileScale;
  localU = std::clamp(localU, 0.0f, 1.0f - kTileUvEpsilon);
  localV = std::clamp(localV, 0.0f, 1.0f - kTileUvEpsilon);
}

} // namespace

Tesselator::Tesselator(int size)
    : size(size), vertices(0), u(0), v(0), _color(0), hasColor(false),
      hasTexture(false), hasNormal(false), p(0), count(0), _noColor(false),
      mode(0), xo(0), yo(0), zo(0), _normal(0), _sx(1), _sy(1),

      tesselating(false), vboId(-1), vboCounts(512), totalSize(0),
      accessMode(ACCESS_STATIC), maxVertices(size / sizeof(VERTEX)),
      _voidBeginEnd(false), _graphicsBackend(nullptr) {
  vboIds = new GLuint[vboCounts];
  std::fill(vboIds, vboIds + vboCounts, 0);

  _varray = new VERTEX[maxVertices];

  char *a = (char *)&_varray[0];
  char *b = (char *)&_varray[1];
  LOGI("Vsize: %lu, %d\n", sizeof(VERTEX), (b - a));
}

Tesselator::~Tesselator() {
  delete[] vboIds;
  delete[] _varray;
}

void Tesselator::init() {
#ifndef STANDALONE_SERVER
  for (int i = 0; i < vboCounts; ++i) {
    std::map<GLuint, RetainedMeshBuffer>::const_iterator existing =
        _bufferMeshes.find(vboIds[i]);
    if (existing != _bufferMeshes.end()) {
      destroyMesh(existing->second.mesh);
    }
  }
  glGenBuffers2(vboCounts, vboIds);
#endif
  vboId = -1;
}

void Tesselator::beginFrame() { vboId = -1; }

void Tesselator::clear() {
  accessMode = ACCESS_STATIC;
  vertices = 0;
  count = 0;
  p = 0;
  _voidBeginEnd = false;
}

void Tesselator::setGraphicsBackend(GraphicsBackend *graphicsBackend) {
  _graphicsBackend = graphicsBackend;
}

auto Tesselator::usingGraphicsBackend() const -> bool {
  return _graphicsBackend &&
      _graphicsBackend->kind() == GraphicsBackendKind::Vulkan;
}

auto Tesselator::resolveWorldPass() const -> GraphicsWorldPass {
  if (glesIsTrackedEnabled(GL_BLEND)) {
    return GraphicsWorldPass::Blend;
  }
  if (glesIsTrackedEnabled(GL_ALPHA_TEST)) {
    return GraphicsWorldPass::AlphaTest;
  }
  return GraphicsWorldPass::Opaque;
}

auto Tesselator::resolveBlendMode() const -> GraphicsBlendMode {
  GLenum src = GL_SRC_ALPHA;
  GLenum dst = GL_ONE_MINUS_SRC_ALPHA;
  glesGetTrackedBlendFunc(src, dst);

  if (src == GL_DST_COLOR && dst == GL_SRC_COLOR) {
    return GraphicsBlendMode::DstColorSrcColor;
  }
  if (src == GL_ZERO && dst == GL_ONE_MINUS_SRC_COLOR) {
    return GraphicsBlendMode::ZeroOneMinusSrcColor;
  }
  if (src == GL_ONE_MINUS_DST_COLOR && dst == GL_ONE_MINUS_SRC_COLOR) {
    return GraphicsBlendMode::OneMinusDstColorOneMinusSrcColor;
  }
  return GraphicsBlendMode::Alpha;
}

auto Tesselator::resolvePrimitive(int drawMode) const -> GraphicsMeshPrimitive {
  switch (drawMode) {
  case GL_LINES:
    return GraphicsMeshPrimitive::LineList;
  case GL_LINE_STRIP:
    return GraphicsMeshPrimitive::LineStrip;
  default:
    return GraphicsMeshPrimitive::TriangleList;
  }
}

auto Tesselator::buildGraphicsVertices(const VERTEX *source, int sourceCount,
    int drawMode, GraphicsMeshPrimitive &primitive, bool &usesTrackedColor,
    std::vector<GraphicsMeshVertex> &out) const -> bool {
  out.clear();
  usesTrackedColor = false;
  if (source == nullptr || sourceCount <= 0) {
    return false;
  }

  primitive = resolvePrimitive(drawMode);
  usesTrackedColor = !hasColor;

  auto appendVertex = [&](const VERTEX &src) {
    GraphicsMeshVertex dst;
    dst.position[0] = src.x;
    dst.position[1] = src.y;
    dst.position[2] = src.z;
    convertAtlasUv(src.u, src.v, dst.texCoord[0], dst.texCoord[1],
        dst.tileOrigin[0], dst.tileOrigin[1]);
    dst.color = hasColor ? src.color : 0xffffffffu;
    out.push_back(dst);
  };

  switch (drawMode) {
  case GL_TRIANGLES:
  case GL_QUADS:
  case GL_LINES:
  case GL_LINE_STRIP:
    out.reserve((size_t)sourceCount);
    for (int i = 0; i < sourceCount; ++i) {
      appendVertex(source[i]);
    }
    return out.size() >=
        (primitive == GraphicsMeshPrimitive::TriangleList ? 3u : 2u);
  case GL_TRIANGLE_FAN:
    if (sourceCount < 3) {
      return false;
    }
    primitive = GraphicsMeshPrimitive::TriangleList;
    out.reserve((size_t)(sourceCount - 2) * 3);
    for (int i = 1; i + 1 < sourceCount; ++i) {
      appendVertex(source[0]);
      appendVertex(source[i]);
      appendVertex(source[i + 1]);
    }
    return true;
  case GL_TRIANGLE_STRIP:
    if (sourceCount < 3) {
      return false;
    }
    primitive = GraphicsMeshPrimitive::TriangleList;
    out.reserve((size_t)(sourceCount - 2) * 3);
    for (int i = 0; i + 2 < sourceCount; ++i) {
      if ((i & 1) == 0) {
        appendVertex(source[i]);
        appendVertex(source[i + 1]);
      } else {
        appendVertex(source[i + 1]);
        appendVertex(source[i]);
      }
      appendVertex(source[i + 2]);
    }
    return true;
  default:
    return false;
  }
}

auto Tesselator::queueMeshDraw(
    GraphicsMeshHandle meshHandle, GraphicsMeshPrimitive primitive,
    bool usesTrackedColor) -> bool {
  if (!usingGraphicsBackend() || meshHandle == 0) {
    return false;
  }

  GraphicsWorldMeshDraw draw;
  draw.mesh = meshHandle;
  draw.texture = glesIsTrackedEnabled(GL_TEXTURE_2D)
      ? _graphicsBackend->currentTexture()
      : 0;
  draw.pass = resolveWorldPass();
  draw.primitive = primitive;
  draw.blendMode = resolveBlendMode();
  draw.depthTest = glesIsTrackedEnabled(GL_DEPTH_TEST);
  if (usesTrackedColor) {
    glesGetTrackedColor4f(draw.colorMultiplier[0], draw.colorMultiplier[1],
        draw.colorMultiplier[2], draw.colorMultiplier[3]);
  }
  glesGetTrackedMatrix(GL_MODELVIEW_MATRIX, draw.modelView);
  glesGetTrackedMatrix(GL_PROJECTION_MATRIX, draw.projection);
  return _graphicsBackend->drawWorldMesh(draw);
}

int Tesselator::getVboCount() { return vboCounts; }

RenderChunk Tesselator::end(bool useMine, int bufferId, bool uploadMesh,
    GraphicsMeshHandle existingMeshHandle) {
#ifndef STANDALONE_SERVER
  // if (!tesselating) throw /*new*/ IllegalStateException("Not tesselating!");
  if (!tesselating)
    LOGI("not tesselating!\n");

  if (!tesselating || _voidBeginEnd)
    return RenderChunk();

  tesselating = false;
  const int o_vertices = vertices;
  GraphicsMeshHandle meshHandle = existingMeshHandle;
  const bool shouldUploadMesh = uploadMesh || usingGraphicsBackend();
  GraphicsMeshPrimitive primitive = GraphicsMeshPrimitive::TriangleList;
  bool usesTrackedColor = false;

  if (vertices > 0) {
    if (++vboId >= vboCounts)
      vboId = 0;

#ifdef USE_VBO
    // Using VBO, use default buffer id only if we don't send in any
    if (!useMine) {
      bufferId = vboIds[vboId];
    }
#else
    // Not using VBO - always use the next buffer object
    bufferId = vboIds[vboId];
#endif
    int access = GL_STATIC_DRAW; //(accessMode==ACCESS_DYNAMIC) ?
                                 // GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    int bytes = p * sizeof(VERTEX);
    glBindBuffer2(GL_ARRAY_BUFFER, bufferId);
    glBufferData2(GL_ARRAY_BUFFER, bytes, _varray, access); // GL_STREAM_DRAW
    totalSize += bytes;

#ifndef USE_VBO
    // 0 1 2 3 4 5 6 7
    // x y z u v c
    if (hasTexture) {
      glTexCoordPointer2(2, GL_FLOAT, VertexSizeBytes, (GLvoid *)(3 * 4));
      glEnableClientState2(GL_TEXTURE_COORD_ARRAY);
    }
    if (hasColor) {
      glColorPointer2(4, GL_UNSIGNED_BYTE, VertexSizeBytes, (GLvoid *)(5 * 4));
      glEnableClientState2(GL_COLOR_ARRAY);
    }
    if (hasNormal) {
      glNormalPointer(GL_BYTE, VertexSizeBytes, (GLvoid *)(6 * 4));
      glEnableClientState2(GL_NORMAL_ARRAY);
    }
    glVertexPointer2(3, GL_FLOAT, VertexSizeBytes, 0);
    glEnableClientState2(GL_VERTEX_ARRAY);

    if (mode == GL_QUADS) {
      glDrawArrays2(GL_TRIANGLES, 0, vertices);
    } else {
      glDrawArrays2(mode, 0, vertices);
    }
    // printf("drawing %d tris, size %d (%d,%d,%d)\n", vertices, p, hasTexture,
    // hasColor, hasNormal);
    glDisableClientState2(GL_VERTEX_ARRAY);
    if (hasTexture)
      glDisableClientState2(GL_TEXTURE_COORD_ARRAY);
    if (hasColor)
      glDisableClientState2(GL_COLOR_ARRAY);
    if (hasNormal)
      glDisableClientState2(GL_NORMAL_ARRAY);
#endif /*!USE_VBO*/
  }

  if (shouldUploadMesh && _graphicsBackend && o_vertices > 0) {
    if (meshHandle == 0 && bufferId >= 0) {
      std::map<GLuint, RetainedMeshBuffer>::const_iterator existing =
          _bufferMeshes.find((GLuint)bufferId);
      if (existing != _bufferMeshes.end()) {
        meshHandle = existing->second.mesh;
      }
    }

    std::vector<GraphicsMeshVertex> meshVertices;
    GraphicsMeshHandle uploadedMesh = meshHandle;
    if (buildGraphicsVertices(
            _varray, o_vertices, mode, primitive, usesTrackedColor,
            meshVertices) &&
        _graphicsBackend->uploadMesh(meshVertices.data(),
            (uint32_t)meshVertices.size(), meshHandle, uploadedMesh)) {
      meshHandle = uploadedMesh;
      if (bufferId >= 0) {
        _bufferMeshes[(GLuint)bufferId] = {
            uploadedMesh, primitive, usesTrackedColor};
      }
    } else if (meshHandle == 0) {
      meshHandle = 0;
      LOGI("failed to upload retained mesh with %d vertices\n", o_vertices);
    }
  }
  if (bufferId >= 0 && o_vertices == 0) {
    std::map<GLuint, RetainedMeshBuffer>::iterator existing =
        _bufferMeshes.find((GLuint)bufferId);
    if (existing != _bufferMeshes.end()) {
      destroyMesh(existing->second.mesh);
    }
  }

  clear();
  RenderChunk out(bufferId, o_vertices, meshHandle);
  // map.insert( std::make_pair(bufferId, out.id) );
  return out;
#else
  return RenderChunk();
#endif
}

bool Tesselator::drawBuffer(GLuint bufferId) {
#ifndef STANDALONE_SERVER
  if (!usingGraphicsBackend()) {
    return false;
  }

  std::map<GLuint, RetainedMeshBuffer>::const_iterator it =
      _bufferMeshes.find(bufferId);
  if (it == _bufferMeshes.end()) {
    return false;
  }
  return queueMeshDraw(
      it->second.mesh, it->second.primitive, it->second.usesTrackedColor);
#else
  (void)bufferId;
  return false;
#endif
}

RenderChunk Tesselator::uploadRetainedMesh(
    const std::vector<GraphicsMeshVertex> &vertices,
    GraphicsMeshHandle existingMeshHandle) {
#ifndef STANDALONE_SERVER
  if (!usingGraphicsBackend() || !_graphicsBackend || vertices.empty()) {
    return RenderChunk();
  }

  GraphicsMeshHandle meshHandle = existingMeshHandle;
  if (!_graphicsBackend->uploadMesh(vertices.data(), (uint32_t)vertices.size(),
          existingMeshHandle, meshHandle)) {
    return RenderChunk();
  }

  return RenderChunk(-1, (int)vertices.size(), meshHandle);
#else
  (void)vertices;
  (void)existingMeshHandle;
  return RenderChunk();
#endif
}

void Tesselator::begin(int mode) {
  if (tesselating || _voidBeginEnd) {
    if (tesselating && !_voidBeginEnd)
      LOGI("already tesselating!\n");
    return;
  }
  // if (tesselating) {
  //     throw /*new*/ IllegalStateException("Already tesselating!");
  // }
  tesselating = true;

  clear();
  this->mode = mode;
  hasNormal = false;
  hasColor = false;
  hasTexture = false;
  _noColor = false;
}

void Tesselator::begin() { begin(GL_QUADS); }

void Tesselator::tex(float u, float v) {
  hasTexture = true;
  this->u = u;
  this->v = v;
}

int Tesselator::getColor() { return _color; }

void Tesselator::color(float r, float g, float b) {
  color((int)(r * 255), (int)(g * 255), (int)(b * 255));
}

void Tesselator::color(float r, float g, float b, float a) {
  color((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(a * 255));
}

void Tesselator::color(int r, int g, int b) { color(r, g, b, 255); }

void Tesselator::color(int r, int g, int b, int a) {
  if (_noColor)
    return;

  if (r > 255)
    r = 255;
  if (g > 255)
    g = 255;
  if (b > 255)
    b = 255;
  if (a > 255)
    a = 255;
  if (r < 0)
    r = 0;
  if (g < 0)
    g = 0;
  if (b < 0)
    b = 0;
  if (a < 0)
    a = 0;

  hasColor = true;
  // if (ByteOrder.nativeOrder() == ByteOrder.LITTLE_ENDIAN) {
  if (true) {
    _color = (a << 24) | (b << 16) | (g << 8) | (r);
  } else {
    _color = (r << 24) | (g << 16) | (b << 8) | (a);
  }
}

void Tesselator::color(char r, char g, char b) {
  color(r & 0xff, g & 0xff, b & 0xff);
}

void Tesselator::color(int c) {
  int r = ((c >> 16) & 255);
  int g = ((c >> 8) & 255);
  int b = ((c) & 255);
  color(r, g, b);
}

//@note: doesn't care about endianess
void Tesselator::colorABGR(int c) {
  if (_noColor)
    return;
  hasColor = true;
  _color = c;
}

void Tesselator::color(int c, int alpha) {
  int r = ((c >> 16) & 255);
  int g = ((c >> 8) & 255);
  int b = ((c) & 255);
  color(r, g, b, alpha);
}

void Tesselator::vertexUV(float x, float y, float z, float u, float v) {
  tex(u, v);
  vertex(x, y, z);
}

void Tesselator::scale2d(float sx, float sy) {
  _sx *= sx;
  _sy *= sy;
}

void Tesselator::resetScale() { _sx = _sy = 1; }

void Tesselator::getScale2d(float &sx, float &sy) const {
  sx = _sx;
  sy = _sy;
}

void Tesselator::vertex(float x, float y, float z) {
#ifndef STANDALONE_SERVER
  count++;

  if (mode == GL_QUADS && (count & 3) == 0) {
    for (int i = 0; i < 2; i++) {

      const int offs = 3 - i;
      VERTEX &src = _varray[p - offs];
      VERTEX &dst = _varray[p];

      if (hasTexture) {
        dst.u = src.u;
        dst.v = src.v;
      }
      if (hasColor) {
        dst.color = src.color;
      }
      // if (hasNormal) {
      //	dst.normal = src.normal;
      // }

      dst.x = src.x;
      dst.y = src.y;
      dst.z = src.z;

      ++vertices;
      ++p;
    }
  }

  VERTEX &vertex = _varray[p];

  if (hasTexture) {
    vertex.u = u;
    vertex.v = v;
  }
  if (hasColor) {
    vertex.color = _color;
  }
  // if (hasNormal) {
  //	vertex.normal = _normal;
  // }

  vertex.x = _sx * (x + xo);
  vertex.y = _sy * (y + yo);
  vertex.z = z + zo;

  if (mode == GL_QUADS && (count & 3) == 0) {
    const VERTEX &v0 = _varray[p - 5];
    const VERTEX &v1 = _varray[p - 4];
    const VERTEX &v2 = _varray[p - 3];
    const VERTEX &v3 = vertex;

    float dx1 = v1.x - v0.x;
    float dy1 = v1.y - v0.y;
    float dz1 = v1.z - v0.z;
    float dx2 = v2.x - v0.x;
    float dy2 = v2.y - v0.y;
    float dz2 = v2.z - v0.z;

    float area = (dy1 * dz2 - dz1 * dy2) * (dy1 * dz2 - dz1 * dy2) +
        (dz1 * dx2 - dx1 * dz2) * (dz1 * dx2 - dx1 * dz2) +
        (dx1 * dy2 - dy1 * dx2) * (dx1 * dy2 - dy1 * dx2);

    if (area < 0.0001f) {
      p -= 5;
      vertices -= 5;
      return;
    }
  }

  ++p;
  ++vertices;

  if ((vertices & 3) == 0 && p >= maxVertices - 1) {
    for (int i = 0; i < 3; ++i)
      printf(
          "Overwriting the vertex buffer! This chunk/entity won't show up\n");
    clear();
  }
#endif
}

void Tesselator::noColor() { _noColor = true; }

void Tesselator::setAccessMode(int mode) { accessMode = mode; }

void Tesselator::normal(float x, float y, float z) {
  static int _warn_t = 0;
  if ((++_warn_t & 32767) == 1)
    LOGI("WARNING: Can't use normals (Tesselator::normal)\n");
  return;

  if (!tesselating)
    printf("But..");
  hasNormal = true;
  char xx = (char)(x * 128);
  char yy = (char)(y * 127);
  char zz = (char)(z * 127);

  _normal = xx | (yy << 8) | (zz << 16);
}

void Tesselator::offset(float xo, float yo, float zo) {
  this->xo = xo;
  this->yo = yo;
  this->zo = zo;
}

void Tesselator::addOffset(float x, float y, float z) {
  xo += x;
  yo += y;
  zo += z;
}

void Tesselator::offset(const Vec3 &v) {
  xo = v.x;
  yo = v.y;
  zo = v.z;
}

void Tesselator::addOffset(const Vec3 &v) {
  xo += v.x;
  yo += v.y;
  zo += v.z;
}

void Tesselator::draw() {
#ifndef STANDALONE_SERVER
  if (!tesselating)
    LOGI("not (draw) tesselating!\n");

  if (!tesselating || _voidBeginEnd)
    return;

  tesselating = false;

  if (vertices > 0) {
    if (++vboId >= vboCounts)
      vboId = 0;

    int bufferId = vboIds[vboId];

    if (usingGraphicsBackend()) {
      GraphicsMeshHandle meshHandle = 0;
      std::map<GLuint, RetainedMeshBuffer>::const_iterator existing =
          _bufferMeshes.find((GLuint)bufferId);
      if (existing != _bufferMeshes.end()) {
        meshHandle = existing->second.mesh;
      }

      GraphicsMeshPrimitive primitive = GraphicsMeshPrimitive::TriangleList;
      bool usesTrackedColor = false;
      std::vector<GraphicsMeshVertex> meshVertices;
      GraphicsMeshHandle uploadedMesh = meshHandle;
      if (buildGraphicsVertices(
              _varray, vertices, mode, primitive, usesTrackedColor,
              meshVertices) &&
          _graphicsBackend->uploadMesh(meshVertices.data(),
              (uint32_t)meshVertices.size(), meshHandle, uploadedMesh)) {
        _bufferMeshes[(GLuint)bufferId] = {
            uploadedMesh, primitive, usesTrackedColor};
        queueMeshDraw(uploadedMesh, primitive, usesTrackedColor);
      }

      clear();
      return;
    }

    int access = GL_DYNAMIC_DRAW; //(accessMode==ACCESS_DYNAMIC) ?
                                  // GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    int bytes = p * sizeof(VERTEX);
    glBindBuffer2(GL_ARRAY_BUFFER, bufferId);
    glBufferData2(GL_ARRAY_BUFFER, bytes, _varray, access); // GL_STREAM_DRAW

    if (hasTexture) {
      glTexCoordPointer2(2, GL_FLOAT, VertexSizeBytes, (GLvoid *)(3 * 4));
      // glTexCoordPointer2(2, GL_FLOAT, VertexSizeBytes, (GLvoid*)
      // &_varray->u);
      glEnableClientState2(GL_TEXTURE_COORD_ARRAY);
    }
    if (hasColor) {
      glColorPointer2(4, GL_UNSIGNED_BYTE, VertexSizeBytes, (GLvoid *)(5 * 4));
      // glColorPointer2(4, GL_UNSIGNED_BYTE, VertexSizeBytes, (GLvoid*)
      // &_varray->color);
      glEnableClientState2(GL_COLOR_ARRAY);
    }
    // if (hasNormal) {
    //	glNormalPointer(GL_BYTE, VertexSizeBytes, (GLvoid*) (6 * 4));
    //	glEnableClientState2(GL_NORMAL_ARRAY);
    // }
    // glVertexPointer2(3, GL_FLOAT, VertexSizeBytes, (GLvoid*)&_varray);
    glVertexPointer2(3, GL_FLOAT, VertexSizeBytes, 0);
    glEnableClientState2(GL_VERTEX_ARRAY);

    if (mode == GL_QUADS) {
      glDrawArrays2(GL_TRIANGLES, 0, vertices);
    } else {
      glDrawArrays2(mode, 0, vertices);
    }

    glDisableClientState2(GL_VERTEX_ARRAY);
    if (hasTexture)
      glDisableClientState2(GL_TEXTURE_COORD_ARRAY);
    if (hasColor)
      glDisableClientState2(GL_COLOR_ARRAY);
    // if (hasNormal) glDisableClientState2(GL_NORMAL_ARRAY);
  }

  clear();
#endif
}

void Tesselator::voidBeginAndEndCalls(bool doVoid) { _voidBeginEnd = doVoid; }

void Tesselator::enableColor() { _noColor = false; }

void Tesselator::destroyMesh(GraphicsMeshHandle meshHandle) {
  if (meshHandle == 0) {
    return;
  }

  for (std::map<GLuint, RetainedMeshBuffer>::iterator it = _bufferMeshes.begin();
      it != _bufferMeshes.end();) {
    if (it->second.mesh == meshHandle) {
      it = _bufferMeshes.erase(it);
    } else {
      ++it;
    }
  }

  if (_graphicsBackend) {
    _graphicsBackend->destroyMesh(meshHandle);
  }
}
