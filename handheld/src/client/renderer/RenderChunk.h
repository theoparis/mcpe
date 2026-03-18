#ifndef NET_MINECRAFT_CLIENT_RENDERER__RenderChunk_H__
#define NET_MINECRAFT_CLIENT_RENDERER__RenderChunk_H__

// package net.minecraft.client.renderer;

#include "../../App.h"
#include "../../world/phys/Vec3.h"
#include "gles.h"

class RenderChunk {
public:
  RenderChunk();
  RenderChunk(
      GLuint vboId_, int vertexCount_, GraphicsMeshHandle meshHandle_ = 0);

  GLuint vboId;
  GLsizei vertexCount;
  GraphicsMeshHandle meshHandle;
  GraphicsWorldPass pass;
  int id;
  Vec3 pos;

private:
  static int runningId;
};

#endif /*NET_MINECRAFT_CLIENT_RENDERER__RenderChunk_H__*/
