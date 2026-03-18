#ifndef NET_MINECRAFT_CLIENT_RENDERER__RenderList_H__
#define NET_MINECRAFT_CLIENT_RENDERER__RenderList_H__

// package net.minecraft.client.renderer;

class GraphicsBackend;
class RenderChunk;

class RenderList {
  static const int MAX_NUM_OBJECTS = 1024 * 6;

public:
  RenderList();
  ~RenderList();

  void init(float xOff, float yOff, float zOff, GraphicsBackend *graphicsBackend);

  void add(int list);
  void addR(const RenderChunk &chunk);

  __inline void next() { ++listIndex; }

  void render();
  void renderChunks();

  void clear();

  float xOff, yOff, zOff;
  int *lists;
  RenderChunk *rlists;

  int listIndex;
  bool inited;
  bool rendered;

private:
  int bufferLimit;
  GraphicsBackend *graphicsBackend;
};

#endif /*NET_MINECRAFT_CLIENT_RENDERER__RenderList_H__*/
