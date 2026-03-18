#ifndef NET_MINECRAFT_CLIENT_RENDERER__Textures_H__
#define NET_MINECRAFT_CLIENT_RENDERER__Textures_H__

// package net.minecraft.client.renderer;

#include "TextureData.h"
#include "gles.h"
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

class DynamicTexture;
class Options;
class AppPlatform;
class GraphicsBackend;

typedef int TextureId;
typedef std::map<std::string, TextureId> TextureMap;

struct TextureRecord {
  TextureRecord() : graphicsId(0) {}

  uint32_t graphicsId;
  TextureData image;
};
typedef std::map<TextureId, TextureRecord> TextureRecordMap;

//@todo: Should probably delete the data buffers with image data
//       after we've created an OpenGL-texture, and rewrite the
//       getTemporaryTextureData() to actually load from file IF
//       it's only read ~once anyway.
class Textures {
public:
  Textures(
      Options *options_, AppPlatform *platform_, GraphicsBackend *graphics_);
  ~Textures();

  void addDynamicTexture(DynamicTexture *dynamicTexture);

  void bind(TextureId id);
  TextureId loadTexture(
      const std::string &resourceName, bool inTextureFolder = true);
  TextureId loadAndBindTexture(const std::string &resourceName);

  TextureId assignTexture(
      const std::string &resourceName, const TextureData &img);
  const TextureData *getTemporaryTextureData(TextureId id);

  void tick(bool uploadToGraphicsCard);

  void clear();
  void reloadAll();

  __inline static bool isTextureIdValid(TextureId t) {
    return t != Textures::InvalidId;
  }

private:
  int smoothBlend(int c0, int c1);
  int crispBlend(int c0, int c1);

public:
  static bool MIPMAP;
  static int textureChanges;
  static const TextureId InvalidId;

private:
  TextureId createTextureRecord(
      const std::string &resourceName, uint32_t graphicsId,
      const TextureData &img);
  TextureRecord *getRecord(TextureId id);
  const TextureRecord *getRecord(TextureId id) const;
  void uploadTextureRegion(TextureId id, int x, int y, int width, int height,
      const unsigned char *data, TextureFormat format, bool transparent);

  TextureMap idMap;
  TextureRecordMap loadedImages;

  Options *options;
  AppPlatform *platform;
  GraphicsBackend *graphics;

  bool clamp;
  bool blur;

  TextureId lastBoundTexture;
  TextureId nextId;
  std::vector<DynamicTexture *> dynamicTextures;
};

#endif /*NET_MINECRAFT_CLIENT_RENDERER__Textures_H__*/
