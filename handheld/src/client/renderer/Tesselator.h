#ifndef NET_MINECRAFT_CLIENT_RENDERER__Tesselator_H__
#define NET_MINECRAFT_CLIENT_RENDERER__Tesselator_H__

// package net.minecraft.client.renderer;

#include "RenderChunk.h"
#include "VertecDecl.h"
#include "gles.h"
#include <map>
#include <vector>

extern const int VertexSizeBytes;

typedef VertexDeclPTC VERTEX;
typedef std::map<GLuint, GLsizei> IntGLMap;

class Tesselator {
  static const int MAX_MEMORY_USE = 16 * 1024 * 1024;
  static const int MAX_FLOATS = MAX_MEMORY_USE / 4 / 2;

  Tesselator(int size);

public:
  static const int ACCESS_DYNAMIC = 1;
  static const int ACCESS_STATIC = 2;

  static Tesselator instance;

  ~Tesselator();

  void init();
  void beginFrame();
  void clear();
  void setGraphicsBackend(GraphicsBackend *graphicsBackend);

  void begin();
  void begin(int mode);
  void draw();
  RenderChunk end(bool useMine, int bufferId, bool uploadMesh = false,
      GraphicsMeshHandle existingMeshHandle = 0);
  void destroyMesh(GraphicsMeshHandle meshHandle);
  bool drawBuffer(GLuint bufferId);
  RenderChunk uploadRetainedMesh(const std::vector<GraphicsMeshVertex> &vertices,
      GraphicsMeshHandle existingMeshHandle = 0);

  void color(int c);
  void color(int c, int alpha);
  void color(float r, float g, float b);
  void color(float r, float g, float b, float a);
  void color(int r, int g, int b);
  void color(int r, int g, int b, int a);
  void color(char r, char g, char b);
  void colorABGR(int c);

  void normal(float x, float y, float z);
  void voidBeginAndEndCalls(bool doVoid);

  void tex(float u, float v);

  void vertex(float x, float y, float z);
  void vertexUV(float x, float y, float z, float u, float v);

  void scale2d(float x, float y);
  void resetScale();
  void getScale2d(float &sx, float &sy) const;

  void noColor();
  void enableColor();

private:
  struct RetainedMeshBuffer {
    GraphicsMeshHandle mesh = 0;
    GraphicsMeshPrimitive primitive = GraphicsMeshPrimitive::TriangleList;
    bool usesTrackedColor = false;
  };

  void setAccessMode(int mode);
  [[nodiscard]] auto usingGraphicsBackend() const -> bool;
  [[nodiscard]] auto resolveWorldPass() const -> GraphicsWorldPass;
  [[nodiscard]] auto resolveBlendMode() const -> GraphicsBlendMode;
  [[nodiscard]] auto resolvePrimitive(int drawMode) const
      -> GraphicsMeshPrimitive;
  auto buildGraphicsVertices(const VERTEX *source, int sourceCount, int mode,
      GraphicsMeshPrimitive &primitive, bool &usesTrackedColor,
      std::vector<GraphicsMeshVertex> &out) const -> bool;
  auto queueMeshDraw(GraphicsMeshHandle meshHandle,
      GraphicsMeshPrimitive primitive, bool usesTrackedColor = false) -> bool;

public:
  void offset(float xo, float yo, float zo);
  void offset(const Vec3 &v);
  void addOffset(float x, float y, float z);
  void addOffset(const Vec3 &v);

  int getVboCount();

  int getColor();

  __inline void beginOverride() {
    begin();
    voidBeginAndEndCalls(true);
  }
  __inline void endOverrideAndDraw() {
    voidBeginAndEndCalls(false);
    draw();
  }
  __inline bool isOverridden() { return _voidBeginEnd; }
  __inline RenderChunk endOverride(int bufferId) {
    voidBeginAndEndCalls(false);
    return end(true, bufferId);
  }

private:
  Tesselator(const Tesselator &rhs) {}
  Tesselator &operator=(const Tesselator &rhs) { return *this; }
  VERTEX *_varray;

  int vertices;

  float xo, yo, zo;
  float u, v;
  unsigned int _color;
  int _normal;
  float _sx, _sy;

  bool hasColor;
  bool hasTexture;
  bool hasNormal;
  bool _noColor;
  bool _voidBeginEnd;

  int p;
  int count;

  bool tesselating;

  bool vboMode;
  int vboCounts;
  int vboId;
  GLuint *vboIds;

  int size;
  int totalSize;
  int maxVertices;

  int mode;
  int accessMode;

  IntGLMap map;
  GraphicsBackend *_graphicsBackend;
  std::map<GLuint, RetainedMeshBuffer> _bufferMeshes;
};

#endif /*NET_MINECRAFT_CLIENT_RENDERER__Tesselator_H__*/
