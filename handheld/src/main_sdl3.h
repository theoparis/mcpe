#pragma once
#include "MinecraftApp.h"
#include "client/renderer/gles.h"
#include <cassert>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <png.h>
#include <vector>

#include <SDL3/SDL.h>
#include <unistd.h>

#define check() assert(glGetError() == 0)

#include "App.h"
#include "platform/input/Keyboard.h"
#include "platform/input/Mouse.h"
#include "platform/input/Multitouch.h"

int width = 848;
int height = 480;

static void png_funcReadFile(png_structp pngPtr, png_bytep data,
                             png_size_t length) {
  ((std::istream *)png_get_io_ptr(pngPtr))->read((char *)data, length);
}

class AppPlatform_SDL3 : public AppPlatform {
public:
  bool isTouchscreen() { return false; }

  std::vector<std::string> getDataSearchPaths() const {
    std::vector<std::string> paths;
    auto addPath = [&](const std::string &p) {
      if (p.empty())
        return;
      std::string out = p;
      while (!out.empty() && out.back() == '/')
        out.pop_back();
      if (!out.empty())
        paths.push_back(out);
    };

    if (const char *env = std::getenv("MCPE_DATA_DIR"))
      addPath(env);

#ifdef MCPE_INSTALL_DATA_DIR
    addPath(MCPE_INSTALL_DATA_DIR);
#endif

    addPath("/usr/share/mcpe");
    addPath("/usr/local/share/mcpe");

    const char *base = SDL_GetBasePath();
    if (base) {
      std::string basePath(base);
      addPath(basePath + "/data");
      addPath(basePath + "/../share/mcpe");
    }

    addPath("data");
    addPath("../data");
    addPath("../../data");
    return paths;
  }

  BinaryBlob readAssetFile(const std::string &filename) override {
    const auto roots = getDataSearchPaths();
    for (const auto &root : roots) {
      std::string path = root + "/" + filename;
      std::ifstream file(path.c_str(), std::ios::binary);
      if (!file)
        continue;

      file.seekg(0, std::ios::end);
      std::streamsize size = file.tellg();
      if (size <= 0)
        continue;
      file.seekg(0, std::ios::beg);

      unsigned char *data = new unsigned char[(size_t)size];
      if (!file.read(reinterpret_cast<char *>(data), size)) {
        delete[] data;
        continue;
      }

      return BinaryBlob(data, (unsigned int)size);
    }

    return BinaryBlob();
  }

  TextureData loadTexture(const std::string &filename_,
                          bool textureFolder) override {
    TextureData out;

    std::string rel = textureFolder ? "images/" + filename_ : filename_;
    const auto roots = getDataSearchPaths();
    for (const auto &root : roots) {
      std::string filename = root + "/" + rel;
      std::ifstream source(filename.c_str(), std::ios::binary);
      if (!source)
        continue;

      png_structp pngPtr =
          png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

      if (!pngPtr)
        return out;

      png_infop infoPtr = png_create_info_struct(pngPtr);

      if (!infoPtr) {
        png_destroy_read_struct(&pngPtr, NULL, NULL);
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

static bool _inited_egl = false;
static bool _app_inited = false;
static int _app_window_normal = true;

static void move_surface(App *app, AppContext *state, uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h);

static void initEgl(App *app, AppContext *state, uint32_t w, uint32_t h) {
  move_surface(app, state, 16, 16, w, h);
}

static void deinitEgl(AppContext *state) {
  if (!_inited_egl) {
    return;
  }

  SDL_GL_MakeCurrent(state->window, nullptr);
  SDL_GL_DestroyContext(state->context);
  SDL_DestroyWindow(state->window);
  state->window = NULL;

  // state->doRender = false;
  _inited_egl = false;
}

void move_surface(App *app, AppContext *state, uint32_t x, uint32_t y,
                  uint32_t w, uint32_t h) {
  int32_t success = 0;

  deinitEgl(state);
  // printf("initEgl\n");

  state->context = SDL_GL_CreateContext(state->window);
  if (!state->context) {
    fprintf(stderr, "Failed to create GL context: %s\n", SDL_GetError());
    return;
  }

  SDL_ShowWindow(state->window);
  SDL_RaiseWindow(state->window);
  SDL_GL_MakeCurrent(state->window, state->context);
  SDL_GL_SetSwapInterval(0);

  _inited_egl = true;

  if (!_app_inited) {
    _app_inited = true;
    app->init(*state);
  } else {
    app->onGraphicsReset(*state);
  }
  app->setSize(w, h);
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

int handleEvents(App *app, AppContext *state) {
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
      app->setSize(width, height);
      continue;
    }
  }

  return 0;
}

void updateWindowPosition(App *app, AppContext *state) {
  int newWidth = 0;
  int newHeight = 0;
  SDL_GetWindowSize(state->window, &newWidth, &newHeight);

  if (newWidth != width || newHeight != height) {
    width = newWidth;
    height = newHeight;
    app->setSize(width, height);
  }
}

int main(int argc, char **argv) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    printf("Couldn't initialize SDL\n");
    return -1;
  }

  // std::string path = argv[0];
  // int e = path.rfind('/');
  // if (e != std::string::npos) {
  //   path = path.substr(0, e);
  //   chdir(path.c_str());
  // }

  char buf[1024];
  getcwd(buf, 1000);
  // printf("getcwd: %s\n", buf);

  // printf("HOME: %s\n", getenv("HOME"));

  atexit(teardown);
  // setGrabbed(false);;

  // printf("storage: %s\n", app->externalStoragePath.c_str());

  AppContext context;
  AppPlatform_SDL3 platform;
  context.doRender = true;
  context.platform = &platform;

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  context.window = SDL_CreateWindow("Minecraft", width, height,
                                    SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!context.window) {
    fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }
  SDL_SetWindowTitle(context.window, "Minecraft - pocket edition");
  SDL_StartTextInput(context.window);

  MinecraftApp *app = new MinecraftApp(context.window);
  std::string storagePath = getenv("HOME");
  storagePath += "/.minecraft/";
  app->externalStoragePath = storagePath;
  app->externalCacheStoragePath = storagePath;

  int commandPort = 0;
  if (argc > 1) {
    commandPort = atoi(argv[1]);
  }

  if (commandPort != 0)
    app->commandPort = commandPort;

  initEgl(app, &context, width, height);

  bool running = true;
  while (running) {
    updateWindowPosition(app, &context);

    running = handleEvents(app, &context) == 0;
    app->update();
  }

  deinitEgl(&context);

  return 0;
}
