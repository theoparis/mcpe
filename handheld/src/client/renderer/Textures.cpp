#include "Textures.h"

#include "../../App.h"
#include "../../AppPlatform.h"
#include "../../platform/time.h"
#include "../Options.h"
#include "TextureData.h"
#include "ptexture/DynamicTexture.h"

/*static*/ int Textures::textureChanges = 0;
/*static*/ bool Textures::MIPMAP = false;
/*static*/ const TextureId Textures::InvalidId = -1;

Textures::Textures(
    Options *options_, AppPlatform *platform_, GraphicsBackend *graphics_)
    : clamp(false), blur(false), options(options_), platform(platform_),
      graphics(graphics_), lastBoundTexture(Textures::InvalidId), nextId(1) {}

Textures::~Textures() {
  clear();

  for (unsigned int i = 0; i < dynamicTextures.size(); ++i)
    delete dynamicTextures[i];
}

void Textures::bind(TextureId id) {
  if (id == Textures::InvalidId) {
    LOGI("invalidId!\n");
    return;
  }
  if (lastBoundTexture == id) {
    return;
  }

  TextureRecord *record = getRecord(id);
  if (!record) {
    LOGI("missing textureId: %d\n", id);
    return;
  }
  if (!graphics || !graphics->bindTexture(record->graphicsId)) {
    LOGI("failed to bind textureId: %d\n", id);
    return;
  }

  lastBoundTexture = id;
  ++textureChanges;
}

void Textures::clear() {
  for (TextureRecordMap::iterator it = loadedImages.begin();
      it != loadedImages.end(); ++it) {
    if (graphics && it->second.graphicsId != 0) {
      graphics->destroyTexture(it->second.graphicsId);
    }
    if (!(it->second.image).memoryHandledExternally) {
      delete[] (it->second.image).data;
    }
  }
  idMap.clear();
  loadedImages.clear();

  lastBoundTexture = Textures::InvalidId;
  nextId = 1;
}

TextureId Textures::loadAndBindTexture(const std::string &resourceName) {
  TextureId id = loadTexture(resourceName);
  if (id != Textures::InvalidId)
    bind(id);

  return id;
}

TextureId Textures::loadTexture(
    const std::string &resourceName, bool inTextureFolder /* = true */) {
  TextureMap::iterator it = idMap.find(resourceName);
  if (it != idMap.end())
    return it->second;

  TextureData texdata = platform->loadTexture(resourceName, inTextureFolder);
  if (texdata.data) {
    TextureId id = assignTexture(resourceName, texdata);
    if (id != Textures::InvalidId) {
      return id;
    }
    if (!texdata.memoryHandledExternally) {
      delete[] texdata.data;
    }
  } else if (texdata.identifier != InvalidId) {
    GraphicsTextureHandle graphicsId = 0;
    if (graphics && graphics->importTexture(
            (GraphicsTextureHandle)texdata.identifier, graphicsId)) {
      return createTextureRecord(resourceName, graphicsId, texdata);
    }
  }

  idMap.insert(std::make_pair(resourceName, Textures::InvalidId));
  return Textures::InvalidId;
}

TextureId Textures::assignTexture(
    const std::string &resourceName, const TextureData &img) {
  if (!graphics) {
    return Textures::InvalidId;
  }

  GraphicsTextureOptions textureOptions;
  textureOptions.clamp = clamp;
  textureOptions.blur = blur;
  textureOptions.mipmap = MIPMAP;

  GraphicsTextureHandle graphicsId = 0;
  if (!graphics->createTexture(img, textureOptions, graphicsId)) {
    return Textures::InvalidId;
  }

  return createTextureRecord(resourceName, graphicsId, img);
}

const TextureData *Textures::getTemporaryTextureData(TextureId id) {
  TextureRecordMap::iterator it = loadedImages.find(id);
  if (it == loadedImages.end())
    return NULL;

  return &it->second.image;
}

void Textures::tick(bool uploadToGraphicsCard) {
  for (unsigned int i = 0; i < dynamicTextures.size(); ++i) {
    DynamicTexture *tex = dynamicTextures[i];
    tex->tick();

    if (uploadToGraphicsCard) {
      tex->bindTexture(this);
      if (lastBoundTexture == Textures::InvalidId) {
        continue;
      }
      for (int xx = 0; xx < tex->replicate; xx++)
        for (int yy = 0; yy < tex->replicate; yy++) {
          uploadTextureRegion(lastBoundTexture, tex->tex % 16 * 16 + xx * 16,
              tex->tex / 16 * 16 + yy * 16, 16, 16, tex->pixels,
              TEXF_UNCOMPRESSED_8888, true);
        }
    }
  }
}

void Textures::addDynamicTexture(DynamicTexture *dynamicTexture) {
  dynamicTextures.push_back(dynamicTexture);
  dynamicTexture->tick();
}

void Textures::reloadAll() {
  // TexturePack skin = skins.selected;

  // for (int id : loadedImages.keySet()) {
  //     BufferedImage image = loadedImages.get(id);
  //     loadTexture(image, id);
  // }

  ////for (HttpTexture httpTexture : httpTextures.values()) {
  ////    httpTexture.isLoaded = false;
  ////}

  // for (std::string name : idMap.keySet()) {
  //     try {
  //         BufferedImage image;
  //         if (name.startsWith("##")) {
  //             image =
  //             makeStrip(readImage(skin.getResource(name.substring(2))));
  //         } else if (name.startsWith("%clamp%")) {
  //             clamp = true;
  //             image = readImage(skin.getResource(name.substring(7)));
  //         } else if (name.startsWith("%blur%")) {
  //             blur = true;
  //             image = readImage(skin.getResource(name.substring(6)));
  //         } else {
  //             image = readImage(skin.getResource(name));
  //         }
  //         int id = idMap.get(name);
  //         loadTexture(image, id);
  //         blur = false;
  //         clamp = false;
  //     } catch (IOException e) {
  //         e.printStackTrace();
  //     }
  // }
}

int Textures::smoothBlend(int c0, int c1) {
  int a0 = (int)(((c0 & 0xff000000) >> 24)) & 0xff;
  int a1 = (int)(((c1 & 0xff000000) >> 24)) & 0xff;
  return ((a0 + a1) >> 1 << 24) +
      (((c0 & 0x00fefefe) + (c1 & 0x00fefefe)) >> 1);
}

int Textures::crispBlend(int c0, int c1) {
  int a0 = (int)(((c0 & 0xff000000) >> 24)) & 0xff;
  int a1 = (int)(((c1 & 0xff000000) >> 24)) & 0xff;

  int a = 255;
  if (a0 + a1 == 0) {
    a0 = 1;
    a1 = 1;
    a = 0;
  }

  int r0 = ((c0 >> 16) & 0xff) * a0;
  int g0 = ((c0 >> 8) & 0xff) * a0;
  int b0 = ((c0) & 0xff) * a0;

  int r1 = ((c1 >> 16) & 0xff) * a1;
  int g1 = ((c1 >> 8) & 0xff) * a1;
  int b1 = ((c1) & 0xff) * a1;

  int r = (r0 + r1) / (a0 + a1);
  int g = (g0 + g1) / (a0 + a1);
  int b = (b0 + b1) / (a0 + a1);

  return (a << 24) | (r << 16) | (g << 8) | b;
}

TextureId Textures::createTextureRecord(const std::string &resourceName,
    uint32_t graphicsId, const TextureData &img) {
  const TextureId id = nextId++;
  TextureRecord record;
  record.graphicsId = graphicsId;
  record.image = img;

  idMap[resourceName] = id;
  loadedImages[id] = record;
  return id;
}

TextureRecord *Textures::getRecord(TextureId id) {
  TextureRecordMap::iterator it = loadedImages.find(id);
  if (it == loadedImages.end()) {
    return NULL;
  }
  return &it->second;
}

const TextureRecord *Textures::getRecord(TextureId id) const {
  TextureRecordMap::const_iterator it = loadedImages.find(id);
  if (it == loadedImages.end()) {
    return NULL;
  }
  return &it->second;
}

void Textures::uploadTextureRegion(TextureId id, int x, int y, int width,
    int height, const unsigned char *data, TextureFormat format,
    bool transparent) {
  TextureRecord *record = getRecord(id);
  if (!record || !graphics || !data) {
    return;
  }

  GraphicsTextureUpdate update;
  update.x = x;
  update.y = y;
  update.width = width;
  update.height = height;
  update.data = data;
  update.format = format;
  update.transparent = transparent;
  graphics->updateTexture(record->graphicsId, update);
}

///*public*/ int loadHttpTexture(std::string url, std::string backup) {
//    HttpTexture texture = httpTextures.get(url);
//    if (texture != NULL) {
//        if (texture.loadedImage != NULL && !texture.isLoaded) {
//            if (texture.id < 0) {
//                texture.id = getTexture(texture.loadedImage);
//            } else {
//                loadTexture(texture.loadedImage, texture.id);
//            }
//            texture.isLoaded = true;
//        }
//    }
//    if (texture == NULL || texture.id < 0) {
//        if (backup == NULL) return -1;
//        return loadTexture(backup);
//    }
//    return texture.id;
//}

// HttpTexture addHttpTexture(std::string url, HttpTextureProcessor processor) {
//     HttpTexture texture = httpTextures.get(url);
//     if (texture == NULL) {
//         httpTextures.put(url, /*new*/ HttpTexture(url, processor));
//     } else {
//         texture.count++;
//     }
//     return texture;
// }

// void removeHttpTexture(std::string url) {
//     HttpTexture texture = httpTextures.get(url);
//     if (texture != NULL) {
//         texture.count--;
//         if (texture.count == 0) {
//             if (texture.id >= 0) releaseTexture(texture.id);
//             httpTextures.remove(url);
//         }
//     }
// }

// void tick() {
//	for (int i = 0; i < dynamicTextures.size(); i++) {
//		DynamicTexture dynamicTexture = dynamicTextures.get(i);
//		dynamicTexture.anaglyph3d = options.anaglyph3d;
//		dynamicTexture.tick();
//
//		pixels.clear();
//		pixels.put(dynamicTexture.pixels);
//		pixels.position(0).limit(dynamicTexture.pixels.length);
//
//		dynamicTexture.bindTexture(this);
//
//		for (int xx = 0; xx < dynamicTexture.replicate; xx++)
//			for (int yy = 0; yy < dynamicTexture.replicate; yy++) {
//
//				glTexSubImage2D2(GL_TEXTURE_2D, 0,
// dynamicTexture.tex % 16 * 16 + xx * 16, dynamicTexture.tex / 16 * 16 + yy *
// 16, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
// if (MIPMAP) { 					for (int level = 1;
// level <= 4; level++) { 						int os =
// 16 >> (level - 1); 						int s = 16 >>
// level;
//
//						for (int x = 0; x < s; x++)
//							for (int y = 0; y < s;
// y++) { 								int c0 =
// pixels.getInt(((x * 2 + 0) + (y * 2 + 0) * os) * 4);
// int c1 = pixels.getInt(((x * 2 + 1) + (y * 2 + 0) * os) * 4);
// int c2 = pixels.getInt(((x * 2 + 1) + (y * 2 + 1) * os) * 4);
// int c3 = pixels.getInt(((x * 2 + 0) + (y * 2 + 1) * os) * 4);
// int col = smoothBlend(smoothBlend(c0, c1), smoothBlend(c2, c3));
// pixels.putInt((x + y * s) * 4, col);
//							}
//							glTexSubImage2D2(GL_TEXTURE_2D,
// level, dynamicTexture.tex % 16 * s, dynamicTexture.tex / 16 * s, s, s,
// GL_RGBA, GL_UNSIGNED_BYTE, pixels);
//					}
//				}
//			}
//	}
//
//	for (int i = 0; i < dynamicTextures.size(); i++) {
//		DynamicTexture dynamicTexture = dynamicTextures.get(i);
//
//		if (dynamicTexture.copyTo > 0) {
//			pixels.clear();
//			pixels.put(dynamicTexture.pixels);
//			pixels.position(0).limit(dynamicTexture.pixels.length);
//			glBindTexture2(GL_TEXTURE_2D, dynamicTexture.copyTo);
//			glTexSubImage2D2(GL_TEXTURE_2D, 0, 0, 0, 16, 16,
// GL_RGBA, GL_UNSIGNED_BYTE, pixels); 			if (MIPMAP) {
//				for (int level = 1; level <= 4; level++) {
// int os = 16 >> (level - 1); 					int s = 16 >>
// level;
//
//					for (int x = 0; x < s; x++)
//						for (int y = 0; y < s; y++) {
//							int c0 =
// pixels.getInt(((x * 2 + 0) + (y * 2 + 0) * os) * 4);
// int c1 = pixels.getInt(((x * 2 + 1) + (y * 2 + 0) * os) * 4);
// int c2 = pixels.getInt(((x * 2 + 1) + (y * 2 + 1) * os) * 4);
// int c3 = pixels.getInt(((x * 2 + 0) + (y * 2 + 1) * os) * 4);
// int col = smoothBlend(smoothBlend(c0, c1), smoothBlend(c2, c3));
// pixels.putInt((x + y * s) * 4, col);
//						}
//						glTexSubImage2D2(GL_TEXTURE_2D,
// level, 0, 0, s, s, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
//				}
//			}
//		}
//	}
// }
//   void releaseTexture(int id) {
//       loadedImages.erase(id);
//       glDeleteTextures(1, (const GLuint*)&id);
//   }
