use super::block::Block;
use std::io::{Read, Write};

/// Chunk dimensions matching the original MCPE.
pub const CHUNK_WIDTH: usize = 16;
pub const CHUNK_HEIGHT: usize = 128;

use std::sync::RwLock;

/// A 16x128x16 column of blocks, matching the original C++ LevelChunk.
pub struct Chunk {
    /// Compressed block data.
    pub compressed_blocks: Vec<u8>,
    /// Cache of decompressed blocks (only when active).
    pub blocks: RwLock<Option<Box<[u8; CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_WIDTH]>>>,
    /// Chunk position in chunk coordinates.
    pub cx: i32,
    pub cz: i32,
    /// Whether this chunk's mesh needs rebuilding.
    pub dirty: bool,
    /// Level of detail data (for distant rendering). Simplified 4x4 per-chunk grid.
    pub lod_blocks: Option<Box<[u8; 4 * 4 * 128]>>,
}

impl Chunk {
    pub fn new(cx: i32, cz: i32) -> Self {
        Self {
            compressed_blocks: Vec::new(),
            blocks: RwLock::new(Some(Box::new(
                [0u8; CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_WIDTH],
            ))),
            cx,
            cz,
            dirty: true,
            lod_blocks: None,
        }
    }

    pub fn compress(&mut self) {
        if let Some(blocks) = self.blocks.get_mut().unwrap().take() {
            let mut frame_info = lz4_flex::frame::FrameInfo::new();
            frame_info.block_size = lz4_flex::frame::BlockSize::Max64KB;

            let mut writer = lz4_flex::frame::FrameEncoder::with_compression_level(
                frame_info,
                Vec::new(),
                9, // HC level 9
            );
            writer.write_all(&*blocks).unwrap();
            self.compressed_blocks = writer.finish().unwrap();
        }
    }

    pub fn decompress(&self) {
        {
            let blocks = self.blocks.read().unwrap();
            if blocks.is_some() {
                return;
            }
        }
        if !self.compressed_blocks.is_empty() {
            let mut reader = lz4_flex::frame::FrameDecoder::new(&self.compressed_blocks[..]);
            let mut buf = Box::new([0u8; CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_WIDTH]);
            reader.read_exact(&mut *buf).unwrap();
            let mut blocks = self.blocks.write().unwrap();
            *blocks = Some(buf);
        }
    }

    #[inline]
    pub fn index(x: usize, y: usize, z: usize) -> usize {
        (y * CHUNK_WIDTH * CHUNK_WIDTH) + (z * CHUNK_WIDTH) + x
    }

    pub fn get_block(&self, x: usize, y: usize, z: usize) -> Block {
        if x >= CHUNK_WIDTH || y >= CHUNK_HEIGHT || z >= CHUNK_WIDTH {
            return Block::Air;
        }
        self.decompress();
        let blocks = self.blocks.read().unwrap();
        if let Some(blocks) = &*blocks {
            Block::from_id(blocks[Self::index(x, y, z)])
        } else {
            Block::Air
        }
    }

    pub fn set_block(&mut self, x: usize, y: usize, z: usize, block: Block) {
        if x >= CHUNK_WIDTH || y >= CHUNK_HEIGHT || z >= CHUNK_WIDTH {
            return;
        }
        self.decompress();
        let mut blocks = self.blocks.write().unwrap();
        if let Some(blocks) = &mut *blocks {
            blocks[Self::index(x, y, z)] = block as u8;
            self.dirty = true;
        }
    }

    /// Generates a simplified LoD (Level of Detail) version of the chunk.
    /// Combines 4x4 columns of blocks into a single block (taking the most frequent if possible, or just the sample).
    pub fn generate_lod(&mut self) {
        self.decompress();
        let mut lod = Box::new([0u8; 4 * 4 * 128]);
        let blocks_lock = self.blocks.read().unwrap();
        if let Some(blocks) = &*blocks_lock {
            for y in 0..CHUNK_HEIGHT {
                for lz in 0..4 {
                    for lx in 0..4 {
                        // Sample 2x2 in the middle of the 4x4 area to pick the most representative block
                        let mut best_id = 0;
                        let mut max_count = 0;
                        let ids = [
                            blocks[Self::index(lx * 4 + 1, y, lz * 4 + 1)],
                            blocks[Self::index(lx * 4 + 1, y, lz * 4 + 2)],
                            blocks[Self::index(lx * 4 + 2, y, lz * 4 + 1)],
                            blocks[Self::index(lx * 4 + 2, y, lz * 4 + 2)],
                        ];

                        for i in 0..4 {
                            let id = ids[i];
                            if id == 0 {
                                continue;
                            }
                            let mut count = 0;
                            for id_j in ids {
                                if id_j == id {
                                    count += 1;
                                }
                            }
                            if count > max_count {
                                max_count = count;
                                best_id = id;
                            }
                        }

                        // Fallback if all 4 are air
                        if best_id == 0 {
                            best_id = blocks[Self::index(lx * 4 + 2, y, lz * 4 + 2)];
                        }

                        lod[(y * 4 * 4) + (lz * 4) + lx] = best_id;
                    }
                }
            }
        }
        self.lod_blocks = Some(lod);
    }
}
