#include "Chunk.h"
#include "../Minecraft.h"
#include "../../util/Mth.h"
#include "../../world/Facing.h"
#include "../../world/entity/Entity.h"
#include "../../world/level/Region.h"
#include "../../world/level/chunk/LevelChunk.h"
#include "../../world/level/tile/Tile.h"
#include "Tesselator.h"
#include "TileRenderer.h"
#include "culling/Culler.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
// #include "../../platform/time.h"

/*static*/ int Chunk::updates = 0;
// static Stopwatch swRebuild;
// int* _layerChunks[3] = {0, 0, 0}; //Chunk::NumLayers];
// int _layerChunkCount[3] = {0, 0, 0};

namespace {
auto renderPassForLayer(int layer) -> GraphicsWorldPass {
  if (layer == 1) {
    return GraphicsWorldPass::AlphaTest;
  }
  if (layer == 2) {
    return GraphicsWorldPass::Blend;
  }
  return GraphicsWorldPass::Opaque;
}

constexpr int kFaceCount = 6;
constexpr int kGreedyLodStep = 2;
constexpr float kShapeEpsilon = 0.0001f;

struct GreedyFaceKey {
  uint16_t texture = 0;
  uint32_t color = 0;
  bool valid = false;

  auto operator==(const GreedyFaceKey &other) const -> bool {
    return valid == other.valid &&
        (!valid || (texture == other.texture && color == other.color));
  }
};

struct GreedyBlock {
  bool compatible = false;
  uint8_t visibleMask = 0;
  uint16_t textures[kFaceCount] = {};
  uint32_t colors[kFaceCount] = {};
};

struct GreedyLodCell {
  bool occupied = false;
  uint16_t textures[kFaceCount] = {};
  uint32_t colors[kFaceCount] = {};
};

struct GreedyMeshBuildResult {
  std::vector<GreedyBlock> blocks;
  std::vector<GraphicsMeshVertex> detailVertices;
  std::vector<GraphicsMeshVertex> lodVertices;
};

auto packColorABGR(float r, float g, float b, float a = 1.0f) -> uint32_t {
  const auto toByte = [](float component) -> uint32_t {
    const float scaled = component * 255.0f + 0.5f;
    return (uint32_t)std::clamp((int)scaled, 0, 255);
  };

  const uint32_t ri = toByte(r);
  const uint32_t gi = toByte(g);
  const uint32_t bi = toByte(b);
  const uint32_t ai = toByte(a);
  return (ai << 24) | (bi << 16) | (gi << 8) | ri;
}

auto linearIndex(int x, int y, int z, int xs, int ys, int zs) -> int {
  (void)ys;
  return (y * zs + z) * xs + x;
}

auto fullCubeShape(const Tile *tile) -> bool {
  return std::fabs(tile->xx0) <= kShapeEpsilon &&
      std::fabs(tile->yy0) <= kShapeEpsilon &&
      std::fabs(tile->zz0) <= kShapeEpsilon &&
      std::fabs(tile->xx1 - 1.0f) <= kShapeEpsilon &&
      std::fabs(tile->yy1 - 1.0f) <= kShapeEpsilon &&
      std::fabs(tile->zz1 - 1.0f) <= kShapeEpsilon;
}

void fillTileOrigin(
    uint16_t texture, float &tileOriginU, float &tileOriginV) {
  tileOriginU = float(texture & 0xf) / 16.0f;
  tileOriginV = float((texture >> 4) & 0xf) / 16.0f;
}

void appendVertex(std::vector<GraphicsMeshVertex> &vertices, float x, float y,
    float z, float u, float v, uint16_t texture, uint32_t color) {
  GraphicsMeshVertex vertex;
  vertex.position[0] = x;
  vertex.position[1] = y;
  vertex.position[2] = z;
  vertex.texCoord[0] = u;
  vertex.texCoord[1] = v;
  fillTileOrigin(texture, vertex.tileOrigin[0], vertex.tileOrigin[1]);
  vertex.color = color;
  vertices.push_back(vertex);
}

void appendQuad(std::vector<GraphicsMeshVertex> &vertices, int face, float x0,
    float x1, float y0, float y1, float z0, float z1, float repeatU,
    float repeatV, uint16_t texture, uint32_t color) {
  switch (face) {
  case Facing::DOWN:
    appendVertex(vertices, x0, y0, z1, 0.0f, repeatV, texture, color);
    appendVertex(vertices, x0, y0, z0, 0.0f, 0.0f, texture, color);
    appendVertex(vertices, x1, y0, z0, repeatU, 0.0f, texture, color);
    appendVertex(vertices, x0, y0, z1, 0.0f, repeatV, texture, color);
    appendVertex(vertices, x1, y0, z0, repeatU, 0.0f, texture, color);
    appendVertex(vertices, x1, y0, z1, repeatU, repeatV, texture, color);
    break;
  case Facing::UP:
    appendVertex(vertices, x1, y1, z1, repeatU, repeatV, texture, color);
    appendVertex(vertices, x1, y1, z0, repeatU, 0.0f, texture, color);
    appendVertex(vertices, x0, y1, z0, 0.0f, 0.0f, texture, color);
    appendVertex(vertices, x1, y1, z1, repeatU, repeatV, texture, color);
    appendVertex(vertices, x0, y1, z0, 0.0f, 0.0f, texture, color);
    appendVertex(vertices, x0, y1, z1, 0.0f, repeatV, texture, color);
    break;
  case Facing::NORTH:
    appendVertex(vertices, x0, y1, z0, repeatU, 0.0f, texture, color);
    appendVertex(vertices, x1, y1, z0, 0.0f, 0.0f, texture, color);
    appendVertex(vertices, x1, y0, z0, 0.0f, repeatV, texture, color);
    appendVertex(vertices, x0, y1, z0, repeatU, 0.0f, texture, color);
    appendVertex(vertices, x1, y0, z0, 0.0f, repeatV, texture, color);
    appendVertex(vertices, x0, y0, z0, repeatU, repeatV, texture, color);
    break;
  case Facing::SOUTH:
    appendVertex(vertices, x0, y1, z1, 0.0f, 0.0f, texture, color);
    appendVertex(vertices, x0, y0, z1, 0.0f, repeatV, texture, color);
    appendVertex(vertices, x1, y0, z1, repeatU, repeatV, texture, color);
    appendVertex(vertices, x0, y1, z1, 0.0f, 0.0f, texture, color);
    appendVertex(vertices, x1, y0, z1, repeatU, repeatV, texture, color);
    appendVertex(vertices, x1, y1, z1, repeatU, 0.0f, texture, color);
    break;
  case Facing::WEST:
    appendVertex(vertices, x0, y1, z1, repeatU, 0.0f, texture, color);
    appendVertex(vertices, x0, y1, z0, 0.0f, 0.0f, texture, color);
    appendVertex(vertices, x0, y0, z0, 0.0f, repeatV, texture, color);
    appendVertex(vertices, x0, y1, z1, repeatU, 0.0f, texture, color);
    appendVertex(vertices, x0, y0, z0, 0.0f, repeatV, texture, color);
    appendVertex(vertices, x0, y0, z1, repeatU, repeatV, texture, color);
    break;
  case Facing::EAST:
    appendVertex(vertices, x1, y0, z1, 0.0f, repeatV, texture, color);
    appendVertex(vertices, x1, y0, z0, repeatU, repeatV, texture, color);
    appendVertex(vertices, x1, y1, z0, repeatU, 0.0f, texture, color);
    appendVertex(vertices, x1, y0, z1, 0.0f, repeatV, texture, color);
    appendVertex(vertices, x1, y1, z0, repeatU, 0.0f, texture, color);
    appendVertex(vertices, x1, y1, z1, 0.0f, 0.0f, texture, color);
    break;
  default:
    break;
  }
}

template <typename KeyFn, typename EmitFn>
void appendGreedyFaces(
    int sliceCount, int rowCount, int bitCount, KeyFn keyFor, EmitFn emitFace) {
  std::vector<uint32_t> rowMasks((size_t)rowCount, 0);
  std::vector<GreedyFaceKey> keys((size_t)rowCount * (size_t)bitCount);

  for (int slice = 0; slice < sliceCount; ++slice) {
    for (int row = 0; row < rowCount; ++row) {
      uint32_t mask = 0;
      for (int bit = 0; bit < bitCount; ++bit) {
        const GreedyFaceKey key = keyFor(slice, row, bit);
        keys[(size_t)row * (size_t)bitCount + (size_t)bit] = key;
        if (key.valid) {
          mask |= (1u << bit);
        }
      }
      rowMasks[(size_t)row] = mask;
    }

    for (int row = 0; row < rowCount; ++row) {
      while (rowMasks[(size_t)row] != 0) {
        const int bit = __builtin_ctz(rowMasks[(size_t)row]);
        const GreedyFaceKey key =
            keys[(size_t)row * (size_t)bitCount + (size_t)bit];

        int width = 1;
        while (bit + width < bitCount &&
            (rowMasks[(size_t)row] & (1u << (bit + width))) != 0 &&
            keys[(size_t)row * (size_t)bitCount + (size_t)(bit + width)] ==
                key) {
          ++width;
        }

        const uint32_t runMask =
            ((uint32_t(1) << width) - 1u) << (uint32_t)bit;
        int height = 1;
        while (row + height < rowCount) {
          if ((rowMasks[(size_t)(row + height)] & runMask) != runMask) {
            break;
          }

          bool same = true;
          for (int offset = 0; offset < width; ++offset) {
            if (!(keys[(size_t)(row + height) * (size_t)bitCount +
                     (size_t)(bit + offset)] == key)) {
              same = false;
              break;
            }
          }
          if (!same) {
            break;
          }
          ++height;
        }

        emitFace(slice, bit, row, width, height, key);
        for (int clearRow = 0; clearRow < height; ++clearRow) {
          rowMasks[(size_t)(row + clearRow)] &= ~runMask;
        }
      }
    }
  }
}

auto faceBrightness(Tile *tile, Region &region, int x, int y, int z, int face)
    -> float {
  switch (face) {
  case Facing::DOWN:
    return tile->getBrightness(&region, x, y - 1, z);
  case Facing::UP:
    return tile->getBrightness(&region, x, y + 1, z);
  case Facing::NORTH:
    return tile->getBrightness(&region, x, y, z - 1);
  case Facing::SOUTH:
    return tile->getBrightness(&region, x, y, z + 1);
  case Facing::WEST:
    return tile->getBrightness(&region, x - 1, y, z);
  case Facing::EAST:
    return tile->getBrightness(&region, x + 1, y, z);
  default:
    return 1.0f;
  }
}

void buildGreedyLodCells(const std::vector<GreedyBlock> &blocks, int xs, int ys,
    int zs, std::vector<GreedyLodCell> &cells, int &lodXs, int &lodYs,
    int &lodZs) {
  lodXs = xs / kGreedyLodStep;
  lodYs = ys / kGreedyLodStep;
  lodZs = zs / kGreedyLodStep;
  cells.clear();
  if (lodXs <= 0 || lodYs <= 0 || lodZs <= 0) {
    return;
  }

  cells.resize((size_t)lodXs * (size_t)lodYs * (size_t)lodZs);
  for (int cy = 0; cy < lodYs; ++cy) {
    for (int cz = 0; cz < lodZs; ++cz) {
      for (int cx = 0; cx < lodXs; ++cx) {
        GreedyLodCell &cell =
            cells[(size_t)linearIndex(cx, cy, cz, lodXs, lodYs, lodZs)];

        for (int dy = 0; dy < kGreedyLodStep; ++dy) {
          for (int dz = 0; dz < kGreedyLodStep; ++dz) {
            for (int dx = 0; dx < kGreedyLodStep; ++dx) {
              const GreedyBlock &block = blocks[(size_t)linearIndex(
                  cx * kGreedyLodStep + dx, cy * kGreedyLodStep + dy,
                  cz * kGreedyLodStep + dz, xs, ys, zs)];
              if (block.compatible) {
                cell.occupied = true;
              }
            }
          }
        }

        if (!cell.occupied) {
          continue;
        }

        for (int face = 0; face < kFaceCount; ++face) {
          bool found = false;
          for (int primary = 0; primary < kGreedyLodStep && !found; ++primary) {
            const int dxStart = face == Facing::EAST
                ? (kGreedyLodStep - 1 - primary)
                : face == Facing::WEST ? primary : 0;
            const int dyStart = face == Facing::UP
                ? (kGreedyLodStep - 1 - primary)
                : face == Facing::DOWN ? primary : 0;
            const int dzStart = face == Facing::SOUTH
                ? (kGreedyLodStep - 1 - primary)
                : face == Facing::NORTH ? primary : 0;
            const int dxEnd = face == Facing::EAST || face == Facing::WEST
                ? dxStart + 1
                : kGreedyLodStep;
            const int dyEnd = face == Facing::UP || face == Facing::DOWN
                ? dyStart + 1
                : kGreedyLodStep;
            const int dzEnd = face == Facing::SOUTH || face == Facing::NORTH
                ? dzStart + 1
                : kGreedyLodStep;

            for (int dy = dyStart; dy < dyEnd && !found; ++dy) {
              for (int dz = dzStart; dz < dzEnd && !found; ++dz) {
                for (int dx = dxStart; dx < dxEnd; ++dx) {
                  const GreedyBlock &block = blocks[(size_t)linearIndex(
                      cx * kGreedyLodStep + dx, cy * kGreedyLodStep + dy,
                      cz * kGreedyLodStep + dz, xs, ys, zs)];
                  if (!block.compatible) {
                    continue;
                  }

                  cell.textures[face] = block.textures[face];
                  cell.colors[face] = block.colors[face];
                  found = true;
                  break;
                }
              }
            }
          }
        }
      }
    }
  }
}

void buildGreedyChunkMeshes(Region &region, int chunkX, int chunkY, int chunkZ,
    int xs, int ys, int zs, GreedyMeshBuildResult &out) {
  out.blocks.assign((size_t)xs * (size_t)ys * (size_t)zs, GreedyBlock{});
  out.detailVertices.clear();
  out.lodVertices.clear();

  if (Minecraft::useAmbientOcclusion) {
    return;
  }

  constexpr float faceShade[kFaceCount] = {0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f};

  for (int localY = 0; localY < ys; ++localY) {
    for (int localZ = 0; localZ < zs; ++localZ) {
      for (int localX = 0; localX < xs; ++localX) {
        const int worldX = chunkX + localX;
        const int worldY = chunkY + localY;
        const int worldZ = chunkZ + localZ;
        const int tileId = region.getTile(worldX, worldY, worldZ);
        if (tileId <= 0) {
          continue;
        }

        Tile *tile = Tile::tiles[tileId];
        if (tile == nullptr || tile->getRenderLayer() != 0 ||
            tile->getRenderShape() != Tile::SHAPE_BLOCK ||
            !tile->isSolidRender()) {
          continue;
        }

        tile->updateShape(&region, worldX, worldY, worldZ);
        if (!fullCubeShape(tile)) {
          continue;
        }

        GreedyBlock &block = out.blocks[(size_t)linearIndex(
            localX, localY, localZ, xs, ys, zs)];
        block.compatible = true;

        int col = tile->getColor(&region, worldX, worldY, worldZ);
        float r = ((col >> 16) & 0xff) / 255.0f;
        float g = ((col >> 8) & 0xff) / 255.0f;
        float b = (col & 0xff) / 255.0f;

        for (int face = 0; face < kFaceCount; ++face) {
          const float brightness =
              faceBrightness(tile, region, worldX, worldY, worldZ, face);
          block.textures[face] =
              (uint16_t)tile->getTexture(&region, worldX, worldY, worldZ, face);
          block.colors[face] = packColorABGR(
              r * faceShade[face] * brightness,
              g * faceShade[face] * brightness,
              b * faceShade[face] * brightness);
        }

        if (tile->shouldRenderFace(
                &region, worldX, worldY - 1, worldZ, Facing::DOWN)) {
          block.visibleMask |= 1u << Facing::DOWN;
        }
        if (tile->shouldRenderFace(
                &region, worldX, worldY + 1, worldZ, Facing::UP)) {
          block.visibleMask |= 1u << Facing::UP;
        }
        if (tile->shouldRenderFace(
                &region, worldX, worldY, worldZ - 1, Facing::NORTH)) {
          block.visibleMask |= 1u << Facing::NORTH;
        }
        if (tile->shouldRenderFace(
                &region, worldX, worldY, worldZ + 1, Facing::SOUTH)) {
          block.visibleMask |= 1u << Facing::SOUTH;
        }
        if (tile->shouldRenderFace(
                &region, worldX - 1, worldY, worldZ, Facing::WEST)) {
          block.visibleMask |= 1u << Facing::WEST;
        }
        if (tile->shouldRenderFace(
                &region, worldX + 1, worldY, worldZ, Facing::EAST)) {
          block.visibleMask |= 1u << Facing::EAST;
        }
      }
    }
  }

  out.detailVertices.reserve((size_t)xs * (size_t)ys * (size_t)zs);
  appendGreedyFaces(ys, zs, xs,
      [&](int slice, int row, int bit) -> GreedyFaceKey {
        const GreedyBlock &block = out.blocks[(size_t)linearIndex(
            bit, slice, row, xs, ys, zs)];
        if (!block.compatible || (block.visibleMask & (1u << Facing::DOWN)) == 0) {
          return {};
        }
        return {block.textures[Facing::DOWN], block.colors[Facing::DOWN], true};
      },
      [&](int slice, int bit, int row, int width, int height,
          const GreedyFaceKey &key) {
        appendQuad(out.detailVertices, Facing::DOWN, (float)bit,
            (float)(bit + width), (float)slice, (float)slice, (float)row,
            (float)(row + height), (float)width, (float)height, key.texture,
            key.color);
      });
  appendGreedyFaces(ys, zs, xs,
      [&](int slice, int row, int bit) -> GreedyFaceKey {
        const GreedyBlock &block = out.blocks[(size_t)linearIndex(
            bit, slice, row, xs, ys, zs)];
        if (!block.compatible || (block.visibleMask & (1u << Facing::UP)) == 0) {
          return {};
        }
        return {block.textures[Facing::UP], block.colors[Facing::UP], true};
      },
      [&](int slice, int bit, int row, int width, int height,
          const GreedyFaceKey &key) {
        appendQuad(out.detailVertices, Facing::UP, (float)bit,
            (float)(bit + width), (float)slice, (float)(slice + 1), (float)row,
            (float)(row + height), (float)width, (float)height, key.texture,
            key.color);
      });
  appendGreedyFaces(zs, ys, xs,
      [&](int slice, int row, int bit) -> GreedyFaceKey {
        const GreedyBlock &block = out.blocks[(size_t)linearIndex(
            bit, row, slice, xs, ys, zs)];
        if (!block.compatible ||
            (block.visibleMask & (1u << Facing::NORTH)) == 0) {
          return {};
        }
        return {block.textures[Facing::NORTH], block.colors[Facing::NORTH],
            true};
      },
      [&](int slice, int bit, int row, int width, int height,
          const GreedyFaceKey &key) {
        appendQuad(out.detailVertices, Facing::NORTH, (float)bit,
            (float)(bit + width), (float)row, (float)(row + height),
            (float)slice, (float)slice, (float)width, (float)height,
            key.texture, key.color);
      });
  appendGreedyFaces(zs, ys, xs,
      [&](int slice, int row, int bit) -> GreedyFaceKey {
        const GreedyBlock &block = out.blocks[(size_t)linearIndex(
            bit, row, slice, xs, ys, zs)];
        if (!block.compatible ||
            (block.visibleMask & (1u << Facing::SOUTH)) == 0) {
          return {};
        }
        return {block.textures[Facing::SOUTH], block.colors[Facing::SOUTH],
            true};
      },
      [&](int slice, int bit, int row, int width, int height,
          const GreedyFaceKey &key) {
        appendQuad(out.detailVertices, Facing::SOUTH, (float)bit,
            (float)(bit + width), (float)row, (float)(row + height),
            (float)slice, (float)(slice + 1), (float)width, (float)height,
            key.texture, key.color);
      });
  appendGreedyFaces(xs, ys, zs,
      [&](int slice, int row, int bit) -> GreedyFaceKey {
        const GreedyBlock &block = out.blocks[(size_t)linearIndex(
            slice, row, bit, xs, ys, zs)];
        if (!block.compatible || (block.visibleMask & (1u << Facing::WEST)) == 0) {
          return {};
        }
        return {block.textures[Facing::WEST], block.colors[Facing::WEST], true};
      },
      [&](int slice, int bit, int row, int width, int height,
          const GreedyFaceKey &key) {
        appendQuad(out.detailVertices, Facing::WEST, (float)slice, (float)slice,
            (float)row, (float)(row + height), (float)bit,
            (float)(bit + width), (float)width, (float)height, key.texture,
            key.color);
      });
  appendGreedyFaces(xs, ys, zs,
      [&](int slice, int row, int bit) -> GreedyFaceKey {
        const GreedyBlock &block = out.blocks[(size_t)linearIndex(
            slice, row, bit, xs, ys, zs)];
        if (!block.compatible || (block.visibleMask & (1u << Facing::EAST)) == 0) {
          return {};
        }
        return {block.textures[Facing::EAST], block.colors[Facing::EAST], true};
      },
      [&](int slice, int bit, int row, int width, int height,
          const GreedyFaceKey &key) {
        appendQuad(out.detailVertices, Facing::EAST, (float)slice,
            (float)(slice + 1), (float)row, (float)(row + height), (float)bit,
            (float)(bit + width), (float)width, (float)height, key.texture,
            key.color);
      });

  std::vector<GreedyLodCell> cells;
  int lodXs = 0;
  int lodYs = 0;
  int lodZs = 0;
  buildGreedyLodCells(out.blocks, xs, ys, zs, cells, lodXs, lodYs, lodZs);
  if (cells.empty()) {
    return;
  }

  const auto cellOccupied = [&](int x, int y, int z) -> bool {
    if (x < 0 || y < 0 || z < 0 || x >= lodXs || y >= lodYs || z >= lodZs) {
      return false;
    }
    return cells[(size_t)linearIndex(x, y, z, lodXs, lodYs, lodZs)].occupied;
  };

  out.lodVertices.reserve((size_t)lodXs * (size_t)lodYs * (size_t)lodZs);
  appendGreedyFaces(lodYs, lodZs, lodXs,
      [&](int slice, int row, int bit) -> GreedyFaceKey {
        const GreedyLodCell &cell = cells[(size_t)linearIndex(
            bit, slice, row, lodXs, lodYs, lodZs)];
        if (!cell.occupied || cellOccupied(bit, slice - 1, row)) {
          return {};
        }
        return {cell.textures[Facing::DOWN], cell.colors[Facing::DOWN], true};
      },
      [&](int slice, int bit, int row, int width, int height,
          const GreedyFaceKey &key) {
        appendQuad(out.lodVertices, Facing::DOWN,
            float(bit * kGreedyLodStep),
            float((bit + width) * kGreedyLodStep),
            float(slice * kGreedyLodStep), float(slice * kGreedyLodStep),
            float(row * kGreedyLodStep),
            float((row + height) * kGreedyLodStep),
            float(width * kGreedyLodStep), float(height * kGreedyLodStep),
            key.texture, key.color);
      });
  appendGreedyFaces(lodYs, lodZs, lodXs,
      [&](int slice, int row, int bit) -> GreedyFaceKey {
        const GreedyLodCell &cell = cells[(size_t)linearIndex(
            bit, slice, row, lodXs, lodYs, lodZs)];
        if (!cell.occupied || cellOccupied(bit, slice + 1, row)) {
          return {};
        }
        return {cell.textures[Facing::UP], cell.colors[Facing::UP], true};
      },
      [&](int slice, int bit, int row, int width, int height,
          const GreedyFaceKey &key) {
        appendQuad(out.lodVertices, Facing::UP,
            float(bit * kGreedyLodStep),
            float((bit + width) * kGreedyLodStep),
            float(slice * kGreedyLodStep),
            float((slice + 1) * kGreedyLodStep),
            float(row * kGreedyLodStep),
            float((row + height) * kGreedyLodStep),
            float(width * kGreedyLodStep), float(height * kGreedyLodStep),
            key.texture, key.color);
      });
  appendGreedyFaces(lodZs, lodYs, lodXs,
      [&](int slice, int row, int bit) -> GreedyFaceKey {
        const GreedyLodCell &cell = cells[(size_t)linearIndex(
            bit, row, slice, lodXs, lodYs, lodZs)];
        if (!cell.occupied || cellOccupied(bit, row, slice - 1)) {
          return {};
        }
        return {cell.textures[Facing::NORTH], cell.colors[Facing::NORTH], true};
      },
      [&](int slice, int bit, int row, int width, int height,
          const GreedyFaceKey &key) {
        appendQuad(out.lodVertices, Facing::NORTH,
            float(bit * kGreedyLodStep),
            float((bit + width) * kGreedyLodStep),
            float(row * kGreedyLodStep),
            float((row + height) * kGreedyLodStep),
            float(slice * kGreedyLodStep), float(slice * kGreedyLodStep),
            float(width * kGreedyLodStep), float(height * kGreedyLodStep),
            key.texture, key.color);
      });
  appendGreedyFaces(lodZs, lodYs, lodXs,
      [&](int slice, int row, int bit) -> GreedyFaceKey {
        const GreedyLodCell &cell = cells[(size_t)linearIndex(
            bit, row, slice, lodXs, lodYs, lodZs)];
        if (!cell.occupied || cellOccupied(bit, row, slice + 1)) {
          return {};
        }
        return {cell.textures[Facing::SOUTH], cell.colors[Facing::SOUTH], true};
      },
      [&](int slice, int bit, int row, int width, int height,
          const GreedyFaceKey &key) {
        appendQuad(out.lodVertices, Facing::SOUTH,
            float(bit * kGreedyLodStep),
            float((bit + width) * kGreedyLodStep),
            float(row * kGreedyLodStep),
            float((row + height) * kGreedyLodStep),
            float(slice * kGreedyLodStep),
            float((slice + 1) * kGreedyLodStep),
            float(width * kGreedyLodStep), float(height * kGreedyLodStep),
            key.texture, key.color);
      });
  appendGreedyFaces(lodXs, lodYs, lodZs,
      [&](int slice, int row, int bit) -> GreedyFaceKey {
        const GreedyLodCell &cell = cells[(size_t)linearIndex(
            slice, row, bit, lodXs, lodYs, lodZs)];
        if (!cell.occupied || cellOccupied(slice - 1, row, bit)) {
          return {};
        }
        return {cell.textures[Facing::WEST], cell.colors[Facing::WEST], true};
      },
      [&](int slice, int bit, int row, int width, int height,
          const GreedyFaceKey &key) {
        appendQuad(out.lodVertices, Facing::WEST,
            float(slice * kGreedyLodStep), float(slice * kGreedyLodStep),
            float(row * kGreedyLodStep),
            float((row + height) * kGreedyLodStep),
            float(bit * kGreedyLodStep),
            float((bit + width) * kGreedyLodStep),
            float(width * kGreedyLodStep), float(height * kGreedyLodStep),
            key.texture, key.color);
      });
  appendGreedyFaces(lodXs, lodYs, lodZs,
      [&](int slice, int row, int bit) -> GreedyFaceKey {
        const GreedyLodCell &cell = cells[(size_t)linearIndex(
            slice, row, bit, lodXs, lodYs, lodZs)];
        if (!cell.occupied || cellOccupied(slice + 1, row, bit)) {
          return {};
        }
        return {cell.textures[Facing::EAST], cell.colors[Facing::EAST], true};
      },
      [&](int slice, int bit, int row, int width, int height,
          const GreedyFaceKey &key) {
        appendQuad(out.lodVertices, Facing::EAST,
            float(slice * kGreedyLodStep),
            float((slice + 1) * kGreedyLodStep),
            float(row * kGreedyLodStep),
            float((row + height) * kGreedyLodStep),
            float(bit * kGreedyLodStep),
            float((bit + width) * kGreedyLodStep),
            float(width * kGreedyLodStep), float(height * kGreedyLodStep),
            key.texture, key.color);
      });
}
}

Chunk::Chunk(Level *level_, int x, int y, int z, int size, int lists_,
    GLuint *ptrBuf /*= NULL*/)
    : level(level_), visible(false), compiled(false), _empty(true), xs(size),
      ys(size), zs(size), dirty(false), occlusion_visible(true),
      occlusion_querying(false), lists(lists_), vboBuffers(ptrBuf),
      bb(0, 0, 0, 1, 1, 1), t(Tesselator::instance) {
  for (int l = 0; l < NumLayers; l++) {
    empty[l] = false;
  }

  radius = Mth::sqrt((float)(xs * xs + ys * ys + zs * zs)) * 0.5f;

  this->x = -999;
  setPos(x, y, z);
}

Chunk::~Chunk() {
  for (int layer = 0; layer < NumLayers; ++layer) {
    if (renderChunk[layer].meshHandle != 0) {
      t.destroyMesh(renderChunk[layer].meshHandle);
      renderChunk[layer].meshHandle = 0;
    }
  }
  if (greedyRenderChunk.meshHandle != 0) {
    t.destroyMesh(greedyRenderChunk.meshHandle);
    greedyRenderChunk.meshHandle = 0;
  }
  if (lodRenderChunk.meshHandle != 0) {
    t.destroyMesh(lodRenderChunk.meshHandle);
    lodRenderChunk.meshHandle = 0;
  }
}

void Chunk::setPos(int x, int y, int z) {
  if (x == this->x && y == this->y && z == this->z)
    return;

  reset();
  this->x = x;
  this->y = y;
  this->z = z;
  xm = x + xs / 2;
  ym = y + ys / 2;
  zm = z + zs / 2;

  const float xzg = 1.0f;
  const float yp = 2.0f;
  const float yn = 0.0f;
  bb.set(x - xzg, y - yn, z - xzg, x + xs + xzg, y + ys + yp, z + zs + xzg);

  // glNewList(lists + 2, GL_COMPILE);
  // ItemRenderer.renderFlat(AABB.newTemp(xRenderOffs - g, yRenderOffs - g,
  // zRenderOffs - g, xRenderOffs + xs + g, yRenderOffs + ys + g, zRenderOffs +
  // zs + g)); glEndList();
  setDirty();
}

void Chunk::translateToPos() { glTranslatef2((float)x, (float)y, (float)z); }

void Chunk::rebuild() {
  if (!dirty)
    return;
  // if (!visible) return;
  updates++;

  // if (!_layerChunks[0]) {
  //     for (int i = 0; i < NumLayers; ++i)
  //         _layerChunks[i] = new int[xs * ys * zs];
  // }
  // for (int i = 0; i < NumLayers; ++i)
  //     _layerChunkCount[i] = 0;

  // Stopwatch& sw = swRebuild;
  // sw.start();

  int x0 = x;
  int y0 = y;
  int z0 = z;
  int x1 = x + xs;
  int y1 = y + ys;
  int z1 = z + zs;
  for (int l = 0; l < NumLayers; l++) {
    empty[l] = true;
  }
  _empty = true;

  LevelChunk::touchedSky = false;

  int r = 1;
  Region region(level, x0 - r, y0 - r, z0 - r, x1 + r, y1 + r, z1 + r);
  TileRenderer tileRenderer(&region);
  GreedyMeshBuildResult greedyMeshes;
  buildGreedyChunkMeshes(region, x0, y0, z0, xs, ys, zs, greedyMeshes);

  const auto updateUploadedChunk = [&](RenderChunk &chunk,
                                     const std::vector<GraphicsMeshVertex> &vertices)
      -> bool {
    RenderChunk uploaded =
        t.uploadRetainedMesh(vertices, chunk.meshHandle);
    if (uploaded.meshHandle == 0) {
      if (chunk.meshHandle != 0) {
        t.destroyMesh(chunk.meshHandle);
      }
      chunk = RenderChunk();
      return false;
    }

    chunk = uploaded;
    chunk.pos.x = (float)this->x;
    chunk.pos.y = (float)this->y;
    chunk.pos.z = (float)this->z;
    chunk.pass = GraphicsWorldPass::Opaque;
    return true;
  };

  const bool hasGreedyMesh =
      updateUploadedChunk(greedyRenderChunk, greedyMeshes.detailVertices);
  const bool hasLodMesh =
      updateUploadedChunk(lodRenderChunk, greedyMeshes.lodVertices);

  bool doRenderLayer[NumLayers] = {true, false, false};
  for (int l = 0; l < NumLayers; l++) {
    if (!doRenderLayer[l])
      continue;
    bool renderNextLayer = false;
    bool rendered = false;

    bool started = false;
    int cindex = -1;

    for (int y = y0; y < y1; y++) {
      for (int z = z0; z < z1; z++) {
        for (int x = x0; x < x1; x++) {
          ++cindex;
          // if (l > 0 && cindex != _layerChunks[_layerChunkCount[l]])
          int tileId = region.getTile(x, y, z);
          if (tileId > 0) {
            if (!started) {
              started = true;

#ifndef USE_VBO
              glNewList(lists + l, GL_COMPILE);
              glPushMatrix2();
              translateToPos();
              float ss = 1.000001f;
              glTranslatef2(-zs / 2.0f, -ys / 2.0f, -zs / 2.0f);
              glScalef2(ss, ss, ss);
              glTranslatef2(zs / 2.0f, ys / 2.0f, zs / 2.0f);
#endif
              t.begin();
              // printf(".");
              // printf("Tesselator::offset : %d, %d, %d\n", this->x, this->y,
              // this->z);
              t.offset((float)(-this->x), (float)(-this->y), (float)(-this->z));
              // printf("Tesselator::offset : %f, %f, %f\n", this->x, this->y,
              // this->z);
            }

            Tile *tile = Tile::tiles[tileId];
            int renderLayer = tile->getRenderLayer();

            //                        if (renderLayer == l)
            //                            rendered |=
            //                            tileRenderer.tesselateInWorld(tile, x,
            //                            y, z);
            //                        else {
            //                            _layerChunks[_layerChunkCount[renderLayer]++]
            //                            = cindex;
            //                        }

            if (renderLayer > l) {
              renderNextLayer = true;
              doRenderLayer[renderLayer] = true;
            } else if (renderLayer == l) {
              if (l == 0 && hasGreedyMesh) {
                const GreedyBlock &block = greedyMeshes.blocks[(size_t)linearIndex(
                    x - x0, y - y0, z - z0, xs, ys, zs)];
                if (block.compatible) {
                  continue;
                }
              }
              rendered |= tileRenderer.tesselateInWorld(tile, x, y, z);
            }
          }
        }
      }
    }

    if (started) {

#ifdef USE_VBO
      renderChunk[l] =
          t.end(true, vboBuffers[l], true, renderChunk[l].meshHandle);
      renderChunk[l].pos.x = (float)this->x;
      renderChunk[l].pos.y = (float)this->y;
      renderChunk[l].pos.z = (float)this->z;
      renderChunk[l].pass = renderPassForLayer(l);
#else
      t.end(false, -1);
      glPopMatrix2();
      glEndList();
#endif
      t.offset(0, 0, 0);
    } else {
      rendered = false;
    }

    if (!rendered && renderChunk[l].meshHandle != 0) {
      t.destroyMesh(renderChunk[l].meshHandle);
      renderChunk[l].meshHandle = 0;
    }

    if (rendered) {
      empty[l] = false;
      _empty = false;
    } else if (l == 0 && (hasGreedyMesh || hasLodMesh)) {
      empty[l] = false;
      _empty = false;
    }
    if (!renderNextLayer)
      break;
  }

  // sw.stop();
  // sw.printEvery(1, "rebuild-");
  skyLit = LevelChunk::touchedSky;
  compiled = true;
  return;
}

float Chunk::distanceToSqr(const Entity *player) const {
  float xd = (float)(player->x - xm);
  float yd = (float)(player->y - ym);
  float zd = (float)(player->z - zm);
  return xd * xd + yd * yd + zd * zd;
}

float Chunk::squishedDistanceToSqr(const Entity *player) const {
  float xd = (float)(player->x - xm);
  float yd = (float)(player->y - ym) * 2;
  float zd = (float)(player->z - zm);
  return xd * xd + yd * yd + zd * zd;
}

void Chunk::reset() {
  for (int i = 0; i < NumLayers; i++) {
    empty[i] = true;
  }
  visible = false;
  compiled = false;
  _empty = true;
}

int Chunk::getList(int layer) {
  if (!visible)
    return -1;
  if (!empty[layer])
    return lists + layer;
  return -1;
}

RenderChunk &Chunk::getRenderChunk(int layer) { return renderChunk[layer]; }

RenderChunk &Chunk::getGreedyRenderChunk() { return greedyRenderChunk; }

RenderChunk &Chunk::getLodRenderChunk() { return lodRenderChunk; }

int Chunk::getAllLists(int displayLists[], int p, int layer) {
  if (!visible)
    return p;
  if (!empty[layer])
    displayLists[p++] = (lists + layer);
  return p;
}

void Chunk::cull(Culler *culler) { visible = culler->isVisible(bb); }

void Chunk::renderBB() {
  // glCallList(lists + 2);
}

bool Chunk::isEmpty() {
  return compiled && _empty; // empty[0] && empty[1] && empty[2];
  //	if (!compiled) return false;
  //	return empty[0] && empty[1];
}

void Chunk::setDirty() { dirty = true; }

void Chunk::setClean() { dirty = false; }

bool Chunk::isDirty() { return dirty; }

void Chunk::resetUpdates() {
  updates = 0;
  // swRebuild.reset();
}
