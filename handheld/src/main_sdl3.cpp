#include "MinecraftApp.h"

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <png.h>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>
#include <unistd.h>

#include "App.h"
#include "platform/input/Keyboard.h"
#include "platform/input/Mouse.h"
#include "platform/input/Multitouch.h"
#include "platform/sdl3/Sdl3GraphicsDriver.h"
#include "platform/sdl3/Sdl3VulkanBackend.h"

int width = 848;
int height = 480;

static void png_funcReadFile(
    png_structp pngPtr, png_bytep data, png_size_t length) {
  ((std::istream *)png_get_io_ptr(pngPtr))->read((char *)data, length);
}

static auto getExecutablePath() -> std::string {
  char buffer[4096] = {};
  const ssize_t size = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (size <= 0) {
    return {};
  }

  buffer[(size_t)size] = '\0';
  return std::string(buffer);
}

class AppPlatform_SDL3 : public AppPlatform {
public:
  static auto isTouchscreen() -> bool { return false; }
  auto supportsTouchscreen() -> bool override { return false; }

  static auto getDataSearchPaths() -> std::vector<std::string> {
    std::vector<std::string> paths;
    auto addPath = [&](const std::string &p) {
      if (p.empty()) {
        return;
      }
      std::string out = p;
      while (!out.empty() && out.back() == '/') {
        out.pop_back();
      }
      if (!out.empty()) {
        paths.push_back(out);
      }
    };

    if (const char *env = std::getenv("MCPE_DATA_DIR")) {
      addPath(env);
    }

#ifdef MCPE_INSTALL_DATA_DIR
    addPath(MCPE_INSTALL_DATA_DIR);
#endif

    if (const char *runfiles = std::getenv("RUNFILES_DIR")) {
      addPath(std::string(runfiles) + "/_main/data");
    }

    if (const char *testSrcDir = std::getenv("TEST_SRCDIR")) {
      addPath(std::string(testSrcDir) + "/_main/data");
    }

    addPath("/usr/share/mcpe");
    addPath("/usr/local/share/mcpe");

    const char *base = SDL_GetBasePath();
    if (base != nullptr) {
      std::string basePath(base);
      addPath(basePath + "/data");
      addPath(basePath + "/mcpe-sdl3.runfiles/_main/data");
      addPath(basePath + "/../share/mcpe");
    }

    const std::string exePath = getExecutablePath();
    if (!exePath.empty()) {
      addPath(exePath + ".runfiles/_main/data");
    }

    addPath("data");
    addPath("../data");
    addPath("../../data");
    return paths;
  }

  auto readAssetFile(const std::string &filename) -> BinaryBlob override {
    const auto roots = getDataSearchPaths();
    for (const auto &root : roots) {
      std::string path = root + "/" + filename;
      std::ifstream file(path.c_str(), std::ios::binary);
      if (!file) {
        continue;
      }

      file.seekg(0, std::ios::end);
      std::streamsize size = file.tellg();
      if (size <= 0) {
        continue;
      }
      file.seekg(0, std::ios::beg);

      auto *data = new unsigned char[(size_t)size];
      if (!file.read(reinterpret_cast<char *>(data), size)) {
        delete[] data;
        continue;
      }

      return {data, (unsigned int)size};
    }

    return {};
  }

  auto loadTexture(const std::string &filename_, bool textureFolder)
      -> TextureData override {
    TextureData out;

    std::string rel = textureFolder ? "images/" + filename_ : filename_;
    const auto roots = getDataSearchPaths();
    for (const auto &root : roots) {
      std::string filename = root + "/" + rel;
      std::ifstream source(filename.c_str(), std::ios::binary);
      if (!source) {
        continue;
      }

      png_structp pngPtr =
          png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);

      if (pngPtr == nullptr) {
        return out;
      }

      png_infop infoPtr = png_create_info_struct(pngPtr);

      if (infoPtr == nullptr) {
        png_destroy_read_struct(&pngPtr, nullptr, nullptr);
        return out;
      }

      // Hack to get around the broken libpng for windows
      png_set_read_fn(pngPtr, (void *)&source, png_funcReadFile);

      png_read_info(pngPtr, infoPtr);

      // Set up the texdata properties
      out.w = png_get_image_width(pngPtr, infoPtr);
      out.h = png_get_image_height(pngPtr, infoPtr);

      png_bytep *rowPtrs = new png_bytep[out.h];
      out.data = new unsigned char[4 * out.w * out.h];
      out.memoryHandledExternally = false;

      int rowStrideBytes = 4 * out.w;
      for (int i = 0; i < out.h; i++) {
        rowPtrs[i] = (png_bytep)&out.data[i * rowStrideBytes];
      }
      png_read_image(pngPtr, rowPtrs);

      // Teardown and return
      png_destroy_read_struct(&pngPtr, &infoPtr, (png_infopp)0);
      delete[] rowPtrs;
      source.close();

      return out;
    }

    LOGE("Couldn't find file: %s\n", rel.c_str());
    return out;
  }
};

struct LaunchOptions {
  int commandPort = 0;
};

static bool _surface_ready = false;
static bool _app_inited = false;
static int _app_window_normal = true;

static auto parseBackendName(std::string_view name) -> bool {
  if (name == "gles" || name == "opengl" || name == "gl") {
    fprintf(stderr,
        "SDL3 Bazel build no longer supports the OpenGL renderer; "
        "falling back to Vulkan.\n");
    return true;
  }
  if (name == "vulkan" || name == "vk") {
    return true;
  }
  return false;
}

static auto parseLaunchOptions(int argc, char **argv) -> LaunchOptions {
  LaunchOptions options;

  if (const char *env = std::getenv("MCPE_RENDERER")) {
    if (!parseBackendName(env)) {
      fprintf(stderr, "Ignoring unknown MCPE_RENDERER value: %s\n", env);
    }
  }

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    constexpr std::string_view backendEq = "--renderer=";
    constexpr std::string_view backendLongEq = "--backend=";

    if (arg.starts_with(backendEq)) {
      if (!parseBackendName(arg.substr(backendEq.size()))) {
        fprintf(stderr, "Ignoring unknown renderer value: %.*s\n",
            (int)arg.substr(backendEq.size()).size(),
            arg.substr(backendEq.size()).data());
      }
      continue;
    }
    if (arg.starts_with(backendLongEq)) {
      if (!parseBackendName(arg.substr(backendLongEq.size()))) {
        fprintf(stderr, "Ignoring unknown backend value: %.*s\n",
            (int)arg.substr(backendLongEq.size()).size(),
            arg.substr(backendLongEq.size()).data());
      }
      continue;
    }
    if (arg == "--renderer" || arg == "--backend") {
      if (i + 1 < argc) {
        if (!parseBackendName(argv[i + 1])) {
          fprintf(stderr, "Ignoring unknown renderer value: %s\n", argv[i + 1]);
        }
        ++i;
      }
      continue;
    }
    if (arg.starts_with("--")) {
      continue;
    }

    if (options.commandPort == 0) {
      options.commandPort = atoi(argv[i]);
    }
  }

  return options;
}

static auto backendWindowTitle() -> const char * {
  return "Minecraft - pocket edition [Vulkan]";
}

static auto resetSurface(
    App *app, Sdl3GraphicsDriver &graphics, AppContext *state, uint32_t w,
    uint32_t h) -> bool {
  std::string error;
  if (!graphics.resetSurface(*state, w, h, error)) {
    fprintf(stderr, "Failed to reset %s surface: %s\n", graphics.name(),
        error.c_str());
    return false;
  }

  _surface_ready = true;
  if (!_app_inited) {
    app->init(*state);
    _app_inited = true;
    app->setSize(w, h);
    return true;
  }

  app->setSize(w, h);
  app->onGraphicsReset(*state);
  return true;
}

static void destroySurface(Sdl3GraphicsDriver &graphics, AppContext *state) {
  if (!_surface_ready) {
    return;
  }

  graphics.destroySurface(*state);
  _surface_ready = false;
}

static void teardownGraphics(Sdl3GraphicsDriver &graphics, AppContext *state) {
  destroySurface(graphics, state);
  graphics.destroyWindow(*state);
}

void teardown() { SDL_Quit(); }

/*
static bool isGrabbed = false;
static void setGrabbed(bool status) {
        SDL_WM_GrabInput(status? SDL_GRAB_ON : SDL_GRAB_OFF);
        SDL_ShowCursor  (status? 0 : 1);
        isGrabbed = status;
        printf("set grabbed: %d\n", isGrabbed);
}
*/

static unsigned char transformKey(int key) {
  // Handle ALL keys here. If not handled -> return 0 ("invalid")
  if (key == SDLK_LSHIFT)
    return Keyboard::KEY_LSHIFT;
  if (key == SDLK_DOWN)
    return 40;
  if (key == SDLK_UP)
    return 38;
  if (key == SDLK_SPACE)
    return Keyboard::KEY_SPACE;
  if (key == SDLK_RETURN)
    return 13; // Keyboard::KEY_RETURN;
  if (key == SDLK_BACKSPACE)
    return Keyboard::KEY_BACKSPACE;
  if (key == SDLK_ESCAPE)
    return Keyboard::KEY_ESCAPE;
  if (key == SDLK_TAB)
    return 250;
  if (key >= 'a' && key <= 'z')
    return key - 32;
  if (key >= SDLK_0 && key <= SDLK_9)
    return '0' + (key - SDLK_0);
  return 0;
}

int handleEvents(
    App *app, AppContext *state, Sdl3GraphicsDriver &graphicsBackend) {
  static float relAccumX = 0.0f;
  static float relAccumY = 0.0f;
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (SDL_EVENT_QUIT == event.type) {
      return -1;
    }

    if (SDL_EVENT_KEY_DOWN == event.type) {
      int key = event.key.key;
      unsigned char transformed = transformKey(key);
      if (transformed)
        Keyboard::feed(transformed, 1);
      continue;
    }

    if (SDL_EVENT_KEY_UP == event.type) {
      int key = event.key.key;
      unsigned char transformed = transformKey(key);
      if (transformed)
        Keyboard::feed(transformed, 0);
      continue;
    }

    if (SDL_EVENT_TEXT_INPUT == event.type) {
      const char *text = event.text.text;
      if (text) {
        for (const char *p = text; *p; ++p) {
          if (*p != '\r' && *p != '\n') {
            Keyboard::feedText(*p);
          }
        }
      }
      continue;
    }

    if (SDL_EVENT_MOUSE_BUTTON_DOWN == event.type) {
      bool left = SDL_BUTTON_LEFT == event.button.button;
      char button = left ? 1 : 2;
      bool relative = SDL_GetWindowRelativeMouseMode(state->window);
      int mx = relative ? (width / 2) : event.button.x;
      int my = relative ? (height / 2) : event.button.y;
      Mouse::feed(button, 1, mx, my);
      Multitouch::feed(button, 1, mx, my, 0);
      continue;
    }

    if (SDL_EVENT_MOUSE_BUTTON_UP == event.type) {
      bool left = SDL_BUTTON_LEFT == event.button.button;
      char button = left ? 1 : 2;
      bool relative = SDL_GetWindowRelativeMouseMode(state->window);
      int mx = relative ? (width / 2) : event.button.x;
      int my = relative ? (height / 2) : event.button.y;
      Mouse::feed(button, 0, mx, my);
      Multitouch::feed(button, 0, mx, my, 0);
      continue;
    }

    if (SDL_EVENT_MOUSE_WHEEL == event.type) {
      Mouse::feed(3, 0, event.wheel.x, event.wheel.y, 0, event.wheel.direction);
      continue;
    }

    if (SDL_EVENT_MOUSE_MOTION == event.type) {
      bool relative = SDL_GetWindowRelativeMouseMode(state->window);
      if (relative) {
        relAccumX += event.motion.xrel;
        relAccumY += -event.motion.yrel;

        int dx = (int)std::lround(relAccumX);
        int dy = (int)std::lround(relAccumY);

        if (dx != 0 || dy != 0) {
          int mx = width / 2;
          int my = height / 2;
          Mouse::feed(0, 0, mx, my, (short)dx, (short)dy);
          relAccumX -= dx;
          relAccumY -= dy;
        }
      } else {
        float x = event.motion.x;
        float y = event.motion.y;
        Multitouch::feed(0, 0, x, y, 0);
        Mouse::feed(0, 0, x, y, event.motion.xrel, -event.motion.yrel);
      }
      continue;
    }

    if (SDL_EVENT_WINDOW_FOCUS_GAINED == event.type) {
      _app_window_normal = true;
      continue;
    }

    if (SDL_EVENT_WINDOW_RESIZED == event.type ||
        SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED == event.type) {
      width = event.window.data1;
      height = event.window.data2;

      if (graphicsBackend.kind() == GraphicsBackendKind::Vulkan) {
        if (!resetSurface(app, graphicsBackend, state, width, height)) {
          return -1;
        }
      } else {
        app->setSize(width, height);
      }
      continue;
    }
  }

  return 0;
}

void updateWindowPosition(
    App *app, AppContext *state, Sdl3GraphicsDriver &graphicsBackend) {
  int newWidth = 0;
  int newHeight = 0;
  SDL_GetWindowSizeInPixels(state->window, &newWidth, &newHeight);

  if (newWidth != width || newHeight != height) {
    width = newWidth;
    height = newHeight;

    if (graphicsBackend.kind() == GraphicsBackendKind::Vulkan) {
      resetSurface(app, graphicsBackend, state, width, height);
    } else {
      app->setSize(width, height);
    }
  }
}

int main(int argc, char **argv) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    fprintf(stderr, "Couldn't initialize SDL: %s\n", SDL_GetError());
    return -1;
  }

  char buf[1024];
  getcwd(buf, 1000);

  atexit(teardown);

  const LaunchOptions options = parseLaunchOptions(argc, argv);

  AppContext context;
  AppPlatform_SDL3 platform;
  context.doRender = true;
  context.platform = &platform;

  std::unique_ptr<Sdl3GraphicsDriver> graphicsBackend =
      std::make_unique<Sdl3VulkanBackend>();
  context.graphics = graphicsBackend.get();

  std::string error;
  if (!graphicsBackend->createWindow(
          context, backendWindowTitle(), width, height, error)) {
    fprintf(stderr, "Failed to create %s window: %s\n", graphicsBackend->name(),
        error.c_str());
    return EXIT_FAILURE;
  }

  LOGI("Running Vulkan mode without hidden GL compatibility context\n");

  SDL_StartTextInput(context.window);

  auto minecraftApp = std::make_unique<MinecraftApp>(context.window);
  std::string storagePath = getenv("HOME");
  storagePath += "/.minecraft/";
  minecraftApp->externalStoragePath = storagePath;
  minecraftApp->externalCacheStoragePath = storagePath;

  if (options.commandPort != 0)
    minecraftApp->commandPort = options.commandPort;

  std::unique_ptr<App> app = std::move(minecraftApp);

  if (!resetSurface(app.get(), *graphicsBackend, &context, width, height)) {
    app.reset();
    teardownGraphics(*graphicsBackend, &context);
    return EXIT_FAILURE;
  }

  bool running = true;
  while (running) {
    updateWindowPosition(app.get(), &context, *graphicsBackend);

    running = handleEvents(app.get(), &context, *graphicsBackend) == 0;
    app->update();
  }

  app.reset();
  teardownGraphics(*graphicsBackend, &context);

  return 0;
}
