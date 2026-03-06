use crate::world::level::Level;
use crate::world::nbt::Tag;
use std::fs::{self, File};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

/// Handles saving and loading levels from disk.
pub struct LevelStorage {
    path: PathBuf,
}

impl LevelStorage {
    pub fn new(path: &Path) -> Self {
        fs::create_dir_all(path).ok();
        Self {
            path: path.to_owned(),
        }
    }

    /// Saves the level data (level.dat) and all currently loaded chunks.
    pub fn save_level(&self, level: &Level) -> io::Result<()> {
        // 1. Save level.dat (NBT)
        let mut root = Tag::new_compound();
        root.put_long("RandomSeed", level.seed);
        root.put_int("SpawnX", level.spawn_x);
        root.put_int("SpawnY", level.spawn_y);
        root.put_int("SpawnZ", level.spawn_z);
        root.put_long("Time", level.time);
        root.put_string("LevelName", level.name.clone());
        root.put_int("StorageVersion", 4);

        let dat_path = self.path.join("level.dat");
        let mut file = File::create(dat_path)?;

        // MCPE 0.1.x header: [Version(i32)][Size(i32)] Little Endian
        let mut nbt_data = Vec::new();
        Tag::write_named(&root, "", &mut nbt_data)?;

        file.write_all(&4i32.to_le_bytes())?; // Version
        file.write_all(&(nbt_data.len() as i32).to_le_bytes())?; // Size
        file.write_all(&nbt_data)?;

        // 2. Save chunks.dat (Simplified Region File)
        self.save_chunks(level)?;

        Ok(())
    }

    /// Simplified RegionFile implementation for chunks.dat.
    fn save_chunks(&self, level: &Level) -> io::Result<()> {
        let chunks_path = self.path.join("chunks.dat");
        let mut file = File::create(chunks_path)?;

        // Header: 1024 ints (32x32) mapping (lx, lz) to [sector_num(24)][count(8)]
        let mut offsets = vec![0i32; 1024];
        file.write_all(bytemuck::cast_slice(&offsets))?;

        let mut current_sector = 1;

        // In early MCPE, the world was fixed 256x256x128.
        // Chunks were indexed from 0..15 in X and Z.
        // We map our arbitrary (cx, cz) to 0..31 indexed by +16 offset for simplicity.
        for ((cx, cz), chunk) in level.chunks_iter() {
            let lx = (cx + 16) as usize;
            let lz = (cz + 16) as usize;
            if lx >= 32 || lz >= 32 {
                continue;
            }

            let index = lz * 32 + lx;

            chunk.decompress(); // Ensure blocks are available
            let blocks_lock = chunk.blocks.read().unwrap();
            let Some(blocks) = blocks_lock.as_ref() else {
                continue;
            };

            let data: Vec<u8> = blocks.to_vec();
            let compressed = lz4_flex::compress_prepend_size(&data);

            // Format: [DataSize(i32)][Blocks(..)]
            let chunk_data = &compressed;
            let size = chunk_data.len() as i32 + 4; // Size includes the 4 bytes for itself
            let sectors_needed = (size + 4095) / 4096;

            file.seek(SeekFrom::Start((current_sector * 4096) as u64))?;
            file.write_all(&size.to_le_bytes())?;
            file.write_all(chunk_data)?;

            // Pad to sector
            let padding = (sectors_needed * 4096) as usize - size as usize;
            if padding > 0 {
                file.write_all(&vec![0u8; padding])?;
            }

            offsets[index] = (current_sector << 8) | (sectors_needed & 0xff);
            current_sector += sectors_needed;
        }

        // Finalize header
        file.seek(SeekFrom::Start(0))?;
        file.write_all(bytemuck::cast_slice(&offsets))?;

        Ok(())
    }

    pub fn load_level(&self) -> io::Result<Level> {
        let dat_path = self.path.join("level.dat");
        let mut file = File::open(dat_path)?;

        let mut version_buf = [0u8; 4];
        file.read_exact(&mut version_buf)?;

        let mut size_buf = [0u8; 4];
        file.read_exact(&mut size_buf)?;
        let size = i32::from_le_bytes(size_buf) as usize;

        let mut nbt_data = vec![0u8; size];
        file.read_exact(&mut nbt_data)?;

        let mut cursor = io::Cursor::new(nbt_data);
        let (_name, root) = Tag::read_named(&mut cursor)?;

        let mut level = Level::new();
        level.seed = root.get("RandomSeed").map(|t| t.as_i64()).unwrap_or(0);
        level.spawn_x = root.get("SpawnX").map(|t| t.as_i32()).unwrap_or(8);
        level.spawn_y = root.get("SpawnY").map(|t| t.as_i32()).unwrap_or(64);
        level.spawn_z = root.get("SpawnZ").map(|t| t.as_i32()).unwrap_or(8);
        level.time = root.get("Time").map(|t| t.as_i64()).unwrap_or(0);
        level.name = root
            .get("LevelName")
            .map(|t| t.as_str().to_string())
            .unwrap_or_else(|| "World".to_string());

        // Load chunks
        self.load_chunks(&mut level)?;

        Ok(level)
    }

    fn load_chunks(&self, level: &mut Level) -> io::Result<()> {
        let chunks_path = self.path.join("chunks.dat");
        if !chunks_path.exists() {
            return Ok(());
        }

        let mut file = File::open(chunks_path)?;
        let mut offsets = vec![0i32; 1024];
        file.read_exact(bytemuck::cast_slice_mut(&mut offsets))?;

        for (index, &offset) in offsets.iter().enumerate() {
            if offset == 0 {
                continue;
            }

            let sector_num = (offset >> 8) as u64;
            // let sector_count = (offset & 0xff) as usize;

            let lx = (index % 32) as i32;
            let lz = (index / 32) as i32;
            let cx = lx - 16;
            let cz = lz - 16;

            file.seek(SeekFrom::Start(sector_num * 4096))?;
            let mut size_buf = [0u8; 4];
            file.read_exact(&mut size_buf)?;
            let size = i32::from_le_bytes(size_buf) as usize;

            let data_size = size - 4;
            let mut data = vec![0u8; data_size];
            file.read_exact(&mut data)?;

            let decompressed = match lz4_flex::decompress_size_prepended(&data) {
                Ok(d) => d,
                Err(e) => {
                    log::error!("Failed to decompress chunk ({}, {}): {}", cx, cz, e);
                    continue;
                }
            };

            let mut chunk = crate::world::chunk::Chunk::new(cx, cz);
            {
                let mut blocks_lock = chunk.blocks.write().unwrap();
                let blocks = blocks_lock.as_mut().unwrap();
                let copy_len = decompressed.len().min(blocks.len());
                blocks[..copy_len].copy_from_slice(&decompressed[..copy_len]);
            }
            chunk.dirty = true;
            chunk.generate_lod();
            chunk.compress();

            level.add_chunk(chunk);
        }

        Ok(())
    }
}
