#ifndef APP_H__
#define APP_H__

// #ifdef STANDALONE_SERVER
// #define NO_EGL
// #endif

#include "AppPlatform.h"
#include "platform/log.h"
#include <cstdint>

struct SDL_Window;

enum class GraphicsBackendKind { None, OpenGLES, Vulkan };
enum class GraphicsBlendMode {
  Alpha,
  DstColorSrcColor,
  ZeroOneMinusSrcColor,
  OneMinusDstColorOneMinusSrcColor,
};
using GraphicsTextureHandle = uint32_t;
using GraphicsMeshHandle = uint32_t;

struct GraphicsTextureOptions {
  bool clamp = false;
  bool blur = false;
  bool mipmap = false;
};

struct GraphicsTextureUpdate {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  const unsigned char *data = nullptr;
  TextureFormat format = TEXF_UNCOMPRESSED_8888;
  bool transparent = true;
};

struct GraphicsTexturedQuad {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float canvasWidth = 0.0f;
  float canvasHeight = 0.0f;
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 1.0f;
  float v1 = 1.0f;
  float r = 1.0f;
  float g = 1.0f;
  float b = 1.0f;
  float a = 1.0f;
  GraphicsBlendMode blendMode = GraphicsBlendMode::Alpha;
};

struct GraphicsQuadColor {
  float r = 1.0f;
  float g = 1.0f;
  float b = 1.0f;
  float a = 1.0f;
};

struct GraphicsQuad {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float canvasWidth = 0.0f;
  float canvasHeight = 0.0f;
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 1.0f;
  float v1 = 1.0f;
  bool textured = true;
  GraphicsBlendMode blendMode = GraphicsBlendMode::Alpha;
  GraphicsQuadColor topLeft;
  GraphicsQuadColor topRight;
  GraphicsQuadColor bottomRight;
  GraphicsQuadColor bottomLeft;
};

enum class GraphicsWorldPass {
  Opaque,
  AlphaTest,
  Blend,
};

enum class GraphicsMeshPrimitive {
  TriangleList,
  LineList,
  LineStrip,
};

struct GraphicsMeshVertex {
  float position[3] = {0.0f, 0.0f, 0.0f};
  float texCoord[2] = {0.0f, 0.0f};
  float tileOrigin[2] = {0.0f, 0.0f};
  uint32_t color = 0xffffffffu;
};

struct GraphicsWorldMeshDraw {
  GraphicsMeshHandle mesh = 0;
  GraphicsTextureHandle texture = 0;
  GraphicsWorldPass pass = GraphicsWorldPass::Opaque;
  GraphicsMeshPrimitive primitive = GraphicsMeshPrimitive::TriangleList;
  GraphicsBlendMode blendMode = GraphicsBlendMode::Alpha;
  bool depthTest = true;
  float colorMultiplier[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float modelView[16] = {};
  float projection[16] = {};
};

struct AppContext;

class GraphicsBackend {
public:
  virtual ~GraphicsBackend() = default;

  [[nodiscard]] virtual auto kind() const -> GraphicsBackendKind = 0;
  [[nodiscard]] virtual auto name() const -> const char * = 0;
  [[nodiscard]] virtual auto currentTexture() const -> GraphicsTextureHandle {
    return 0;
  }
  virtual auto createTexture(const TextureData &texture,
      const GraphicsTextureOptions &options,
      GraphicsTextureHandle &outTexture) -> bool = 0;
  virtual auto importTexture(
      GraphicsTextureHandle nativeTexture, GraphicsTextureHandle &outTexture)
      -> bool {
    (void)nativeTexture;
    (void)outTexture;
    return false;
  }
  virtual auto bindTexture(GraphicsTextureHandle texture) -> bool = 0;
  virtual auto updateTexture(GraphicsTextureHandle texture,
      const GraphicsTextureUpdate &update) -> bool = 0;
  virtual auto uploadMesh(const GraphicsMeshVertex *vertices,
      uint32_t vertexCount, GraphicsMeshHandle existingMesh,
      GraphicsMeshHandle &outMesh) -> bool {
    (void)vertices;
    (void)vertexCount;
    (void)existingMesh;
    (void)outMesh;
    return false;
  }
  virtual auto drawQuad(const GraphicsQuad &quad) -> bool = 0;
  virtual auto drawWorldMesh(const GraphicsWorldMeshDraw &draw) -> bool {
    (void)draw;
    return false;
  }
  virtual auto drawTexturedQuad(const GraphicsTexturedQuad &quad) -> bool {
    GraphicsQuad converted;
    converted.x = quad.x;
    converted.y = quad.y;
    converted.width = quad.width;
    converted.height = quad.height;
    converted.canvasWidth = quad.canvasWidth;
    converted.canvasHeight = quad.canvasHeight;
    converted.u0 = quad.u0;
    converted.v0 = quad.v0;
    converted.u1 = quad.u1;
    converted.v1 = quad.v1;
    converted.blendMode = quad.blendMode;
    converted.topLeft = {quad.r, quad.g, quad.b, quad.a};
    converted.topRight = converted.topLeft;
    converted.bottomRight = converted.topLeft;
    converted.bottomLeft = converted.topLeft;
    return drawQuad(converted);
  }
  virtual void destroyTexture(GraphicsTextureHandle texture) = 0;
  virtual void destroyMesh(GraphicsMeshHandle mesh) { (void)mesh; }
  virtual void present(AppContext &context) = 0;
};

struct AppContext {
#ifndef STANDALONE_SERVER
  SDL_Window *window = nullptr;
  void *display = nullptr;
  void *surface = nullptr;
  void *context = nullptr;
  GraphicsBackend *graphics = nullptr;
  bool doRender = false;
#endif
  AppPlatform *platform = nullptr;
};

class App {
public:
  App() : _finished(false), _inited(false) {}
  virtual ~App() = default;

  void init(AppContext &context) {
    _context = context;
    init();
    _inited = true;
  }
  [[nodiscard]] auto isInited() const -> bool { return _inited; }

  virtual auto platform() -> AppPlatform * { return _context.platform; }
  virtual auto graphics() -> GraphicsBackend * {
#ifndef STANDALONE_SERVER
    return _context.graphics;
#else
    return nullptr;
#endif
  }

  void onGraphicsReset(AppContext &context) {
    _context = context;
    onGraphicsReset();
  }

  virtual void audioEngineOn() {}
  virtual void audioEngineOff() {}

  virtual void destroy() {}

  virtual void loadState(void *state, int stateSize) {}
  virtual bool saveState(void **state, int *stateSize) { return false; }

  void swapBuffers() {
#ifndef STANDALONE_SERVER
    if (_context.doRender && _context.graphics)
      _context.graphics->present(_context);
#endif
  }

  virtual void draw() {}
  virtual void update() {}; // = 0;
  virtual void setSize(int width, int height) {}

  virtual void quit() { _finished = true; }
  virtual bool wantToQuit() { return _finished; }
  virtual bool handleBack(bool isDown) { return false; }

protected:
  virtual void init() {}
  // virtual void onGraphicsLost() = 0;
  virtual void onGraphicsReset() = 0;

private:
  bool _inited;
  bool _finished;
  AppContext _context;
};

#endif // APP_H__
