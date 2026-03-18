#include "RenderChunk.h"

int RenderChunk::runningId = 0;

RenderChunk::RenderChunk()
    : vboId(-1), vertexCount(0), meshHandle(0), pass(GraphicsWorldPass::Opaque) {
  id = ++runningId;
}

RenderChunk::RenderChunk(
    GLuint vboId_, int vertexCount_, GraphicsMeshHandle meshHandle_)
    : vboId(vboId_), vertexCount(vertexCount_), meshHandle(meshHandle_),
      pass(GraphicsWorldPass::Opaque) {
  id = ++runningId;
}
