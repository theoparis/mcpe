use super::block::Block;
use super::chunk::{CHUNK_HEIGHT, CHUNK_WIDTH, Chunk};
use std::collections::HashMap;

/// The game level — a collection of chunks.
/// Corresponds to the C++ `Level` class.
pub struct Level {
    pub seed: i64,
    pub name: String,
    pub time: i64, // ticks
    pub spawn_x: i32,
    pub spawn_y: i32,
    pub spawn_z: i32,
    chunks: HashMap<(i32, i32), Chunk>,
}

impl Level {
    pub fn new() -> Self {
        Self {
            seed: 0,
            name: "World".to_string(),
            time: 0,
            spawn_x: 8,
            spawn_y: 64,
            spawn_z: 8,
            chunks: HashMap::new(),
        }
    }

    pub fn generate(&mut self, radius: i32, generator: &dyn super::levelgen::ChunkGenerator) {
        for cx in -radius..=radius {
            for cz in -radius..=radius {
                let mut chunk = generator.generate_chunk(cx, cz);
                chunk.compress(); // Compress for in-memory storage
                self.chunks.insert((cx, cz), chunk);
            }
        }

        generator.post_process(self, radius);

        // Calculate and build lods
        for chunk in self.chunks.values_mut() {
            chunk.generate_lod();
            chunk.compress();
        }
    }

    pub fn get_chunk(&self, cx: i32, cz: i32) -> Option<&Chunk> {
        self.chunks.get(&(cx, cz))
    }

    pub fn get_chunk_mut(&mut self, cx: i32, cz: i32) -> Option<&mut Chunk> {
        self.chunks.get_mut(&(cx, cz))
    }

    /// Get the block at absolute world coordinates.
    pub fn get_block(&self, x: i32, y: i32, z: i32) -> Block {
        if y < 0 || y >= CHUNK_HEIGHT as i32 {
            return Block::Air;
        }
        let cx = x.div_euclid(CHUNK_WIDTH as i32);
        let cz = z.div_euclid(CHUNK_WIDTH as i32);
        let lx = x.rem_euclid(CHUNK_WIDTH as i32) as usize;
        let lz = z.rem_euclid(CHUNK_WIDTH as i32) as usize;
        match self.get_chunk(cx, cz) {
            Some(chunk) => chunk.get_block(lx, y as usize, lz),
            None => Block::Air,
        }
    }

    /// Set a block at absolute world coordinates.
    pub fn set_block(&mut self, x: i32, y: i32, z: i32, block: Block) {
        if y < 0 || y >= CHUNK_HEIGHT as i32 {
            return;
        }
        let cx = x.div_euclid(CHUNK_WIDTH as i32);
        let cz = z.div_euclid(CHUNK_WIDTH as i32);
        let lx = x.rem_euclid(CHUNK_WIDTH as i32) as usize;
        let lz = z.rem_euclid(CHUNK_WIDTH as i32) as usize;
        if let Some(chunk) = self.get_chunk_mut(cx, cz) {
            chunk.set_block(lx, y as usize, lz, block);
        }

        // Mark neighbor chunks dirty if we touched a boundary so their faces update.
        if lx == 0 {
            if let Some(chunk) = self.get_chunk_mut(cx - 1, cz) {
                chunk.dirty = true;
            }
        } else if lx + 1 == CHUNK_WIDTH {
            if let Some(chunk) = self.get_chunk_mut(cx + 1, cz) {
                chunk.dirty = true;
            }
        }

        if lz == 0 {
            if let Some(chunk) = self.get_chunk_mut(cx, cz - 1) {
                chunk.dirty = true;
            }
        } else if lz + 1 == CHUNK_WIDTH {
            if let Some(chunk) = self.get_chunk_mut(cx, cz + 1) {
                chunk.dirty = true;
            }
        }
    }

    pub fn get_top_block(&self, x: i32, z: i32) -> i32 {
        for y in (0..CHUNK_HEIGHT as i32).rev() {
            let block = self.get_block(x, y, z);
            if !block.is_air() && block != Block::Water {
                return y;
            }
        }
        0
    }

    pub fn chunks(&self) -> impl Iterator<Item = &Chunk> {
        self.chunks.values()
    }

    pub fn chunks_iter(&self) -> impl Iterator<Item = (&(i32, i32), &Chunk)> {
        self.chunks.iter()
    }

    pub fn add_chunk(&mut self, chunk: Chunk) {
        self.chunks.insert((chunk.cx, chunk.cz), chunk);
    }
}
