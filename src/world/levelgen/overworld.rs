use super::super::chunk::Chunk;
use super::ChunkGenerator;
use super::biome::BiomeSource;
use super::feature::{Feature, TreeFeature};
use super::noise::PerlinNoise;
use super::random::Random;
use crate::world::block::Block;

pub struct OverworldGenerator {
    /// Lower limit 3D noise (16 octaves)
    lperlin1: PerlinNoise,
    /// Upper limit 3D noise (16 octaves)
    lperlin2: PerlinNoise,
    /// Interpolation 3D noise (8 octaves) — blends between upper/lower
    perlin1: PerlinNoise,
    /// Scale noise for biome height variation (10 octaves)
    scale_noise: PerlinNoise,
    /// Depth noise for terrain base height (16 octaves)
    depth_noise: PerlinNoise,
    /// Surface noise for varying dirt depth (4 octaves)
    surface_noise: PerlinNoise,
    /// Cave carving noise fields (two 3D noises)
    cave_noise1: PerlinNoise,
    cave_noise2: PerlinNoise,
    biome_source: BiomeSource,
    seed: i64,
}

const SEA_LEVEL: i32 = 64;

impl OverworldGenerator {
    pub fn new(seed: i64) -> Self {
        let mut random = Random::new(seed);
        Self {
            lperlin1: PerlinNoise::new(&mut random, 16),
            lperlin2: PerlinNoise::new(&mut random, 16),
            perlin1: PerlinNoise::new(&mut random, 8),
            scale_noise: PerlinNoise::new(&mut random, 10),
            depth_noise: PerlinNoise::new(&mut random, 16),
            surface_noise: PerlinNoise::new(&mut random, 4),
            cave_noise1: PerlinNoise::new(&mut random, 8),
            cave_noise2: PerlinNoise::new(&mut random, 8),
            biome_source: BiomeSource::new(seed),
            seed,
        }
    }

    /// Generates the 3D height/density field that defines the terrain shape.
    /// This is the core of MCPE terrain gen — it produces a low-resolution
    /// 5×17×5 grid that gets trilinearly interpolated to fill the 16×128×16 chunk.
    fn get_heights(
        &self,
        buffer: &mut [f32],
        x: i32,
        _y: i32,
        z: i32,
        x_size: usize,
        y_size: usize,
        z_size: usize,
    ) {
        // Main noise frequency — controls the base wavelength of terrain features
        let s = 684.412;
        let hs = 684.412;

        // Allocate noise sample buffers
        let total_3d = x_size * z_size * y_size;
        let total_2d = x_size * z_size;
        let mut ar = vec![0.0f32; total_3d]; // lower-limit noise
        let mut br = vec![0.0f32; total_3d]; // upper-limit noise
        let mut pnr = vec![0.0f32; total_3d]; // interpolation noise
        let mut sr = vec![0.0f32; total_2d]; // scale noise
        let mut dr = vec![0.0f32; total_2d]; // depth noise

        // Starting positions in noise space (each density column spans 4 blocks)
        let x_start = x as f32;
        let y_start = 0.0;
        let z_start = z as f32;

        // Sample 2D noises for scale and depth
        self.scale_noise
            .get_region2d(&mut sr, x_start, z_start, x_size, z_size, 1.121, 1.121, 0.5);
        self.depth_noise
            .get_region2d(&mut dr, x_start, z_start, x_size, z_size, 200.0, 200.0, 0.5);

        // Sample 3D noises
        // The interpolation noise has 1/80th the frequency — it creates the large-scale
        // blending pattern between the two limit noises
        self.perlin1.get_region(
            &mut pnr,
            x_start,
            y_start,
            z_start,
            x_size,
            y_size,
            z_size,
            s / 80.0,
            hs / 160.0,
            s / 80.0,
        );
        // The two limit noises define the upper and lower bounds of the density
        self.lperlin1.get_region(
            &mut ar, x_start, y_start, z_start, x_size, y_size, z_size, s, hs, s,
        );
        self.lperlin2.get_region(
            &mut br, x_start, y_start, z_start, x_size, y_size, z_size, s, hs, s,
        );

        let mut p = 0; // 3D noise index
        let mut pp = 0; // 2D noise index

        let w_scale = 16 / x_size as i32;

        for _xc in 0..x_size {
            let xp = (_xc as i32 * w_scale) + w_scale / 2;

            for _zc in 0..z_size {
                let zp = (_zc as i32 * w_scale) + w_scale / 2;

                // Get biome parameters (temperature + humidity)
                let (temp, down) = self
                    .biome_source
                    .get_temp_downfall((x_start as i32) * 4 + xp, (z_start as i32) * 4 + zp);
                let downfall = down * temp;

                // Convert humidity into a height modification factor
                // Wetter biomes get taller terrain (closer to dd=1), drier get flatter
                let mut dd = 1.0 - downfall;
                dd = dd * dd;
                dd = dd * dd;
                dd = 1.0 - dd;

                // Depth noise controls the base terrain height deviation
                let mut depth = dr[pp] / 8000.0;
                let mut scale = (sr[pp] + 256.0) / 512.0;
                scale *= dd;
                if scale > 1.0 {
                    scale = 1.0;
                }

                // Negative depth = valleys/oceans, positive = hills/mountains
                if depth < 0.0 {
                    depth = -depth * 0.3;
                }
                depth = depth * 3.0 - 2.0;

                if depth < 0.0 {
                    // Below baseline — create valleys/oceans but clamp
                    depth /= 2.0;
                    if depth < -1.0 {
                        depth = -1.0;
                    }
                    depth /= 1.4;
                    depth /= 2.0;
                } else {
                    // Above baseline — create hills but limit extremes
                    if depth > 1.0 {
                        depth = 1.0;
                    }
                    depth /= 8.0;
                }

                if scale < 0.0 {
                    scale = 0.0;
                }
                scale += 0.5;

                // Convert depth to actual Y-offset in density columns
                depth = depth * y_size as f32 / 16.0;

                // The center line: density columns above this tend to be air,
                // below tend to be solid. Sea level is at y_size/2.
                let y_center = y_size as f32 / 2.0 + depth * 4.0;
                pp += 1;

                for yi in 0..y_size {
                    let mut val;

                    // y_offs is the density falloff — it increases above y_center
                    // making high-altitude blocks more likely to be air.
                    // The scale factor controls how steep/flat the terrain is.
                    let mut y_offs = (yi as f32 - y_center) * 12.0 * 128.0 / 128.0 / scale;
                    if y_offs < 0.0 {
                        y_offs *= 4.0; // steeper falloff below the center (harder to dig into)
                    }

                    // Blend between lower-limit and upper-limit noise using interpolation noise
                    let bb = ar[p] / 512.0;
                    let cc = br[p] / 512.0;
                    let v = (pnr[p] / 10.0 + 1.0) / 2.0;
                    if v < 0.0 {
                        val = bb;
                    } else if v > 1.0 {
                        val = cc;
                    } else {
                        val = bb + (cc - bb) * v;
                    }
                    val -= y_offs;

                    // Top slide: smoothly transition the top 4 columns to air
                    // to prevent terrain from reaching the world ceiling
                    if yi > y_size - 4 {
                        let slide = (yi as f32 - (y_size - 4) as f32) / 3.0;
                        val = val * (1.0 - slide) + -10.0 * slide;
                    }
                    buffer[p] = val;
                    p += 1;
                }
            }
        }
    }

    /// Carve caves using a simple Perlin noise approach.
    /// Two 3D noise fields are sampled; where both exceed a threshold,
    /// stone is removed to create cave-like voids.
    fn carve_caves(&self, blocks: &mut [u8; 16 * 128 * 16], cx: i32, cz: i32) {
        let cave_threshold = 0.80; // higher = fewer / smaller caves

        for x in 0..16 {
            for z in 0..16 {
                let wx = (cx * 16 + x) as f32;
                let wz = (cz * 16 + z) as f32;

                for y in 1..SEA_LEVEL {
                    // Don't carve within 5 blocks of the surface to avoid exposed caves
                    // Find surface height at this column
                    let mut surface_y = 0;
                    for sy in (0..128).rev() {
                        let idx = (sy * 16 * 16) + (z as usize * 16) + x as usize;
                        if blocks[idx] == Block::Stone as u8 {
                            surface_y = sy as i32;
                            break;
                        }
                    }
                    if y > surface_y - 5 {
                        continue;
                    }

                    let wy = y as f32;
                    let scale = 0.06;

                    // Two noise fields — caves form where both are positive
                    let n1 = self
                        .cave_noise1
                        .get_value(wx * scale, wy * scale * 2.0, wz * scale);
                    let n2 = self
                        .cave_noise2
                        .get_value(wx * scale, wy * scale * 2.0, wz * scale);

                    let combined = n1 * n1 + n2 * n2;
                    if combined > cave_threshold {
                        let idx = (y as usize * 16 * 16) + (z as usize * 16) + x as usize;
                        let block = blocks[idx];
                        // Only carve stone, dirt, grass, sand, gravel
                        if block == Block::Stone as u8
                            || block == Block::Dirt as u8
                            || block == Block::Grass as u8
                            || block == Block::Sand as u8
                            || block == Block::Gravel as u8
                        {
                            // Don't carve if it would expose water (above sea level check)
                            if y < SEA_LEVEL as i32 {
                                // Check if the block above is water
                                if y + 1 < 128 {
                                    let above_idx = ((y + 1) as usize * 16 * 16)
                                        + (z as usize * 16)
                                        + x as usize;
                                    if blocks[above_idx] == Block::Water as u8 {
                                        continue;
                                    }
                                }
                                // Below sea level, fill with water instead of air
                                // Actually in MCPE caves below sea level are just air
                                // but we'll fill with air only if above the column's water
                            }
                            blocks[idx] = Block::Air as u8;
                        }
                    }
                }
            }
        }
    }
}

impl ChunkGenerator for OverworldGenerator {
    fn generate_chunk(&self, cx: i32, cz: i32) -> Chunk {
        let mut chunk = Chunk::new(cx, cz);
        let mut height_buffer = vec![0.0f32; 5 * 17 * 5];
        self.get_heights(&mut height_buffer, cx * 4, 0, cz * 4, 5, 17, 5);

        // Sample surface noise for this chunk (randomizes dirt/sand layer depth)
        let mut surface_depth_noise = vec![0.0f32; 16 * 16];
        self.surface_noise.get_region2d(
            &mut surface_depth_noise,
            (cx * 16) as f32,
            (cz * 16) as f32,
            16,
            16,
            0.0625,
            0.0625,
            1.0,
        );

        {
            chunk.decompress();
            let mut blocks = chunk.blocks.write().unwrap();
            let blocks = blocks.as_mut().unwrap();

            // Phase 1: Generate base terrain from density field
            // The 5×17×5 density grid is trilinearly interpolated to fill 16×128×16
            for xc in 0..4 {
                for zc in 0..4 {
                    for yc in 0..16 {
                        // Sample corner densities from the height buffer
                        let s0 = height_buffer[(xc * 5 + zc) * 17 + yc];
                        let s1 = height_buffer[(xc * 5 + zc + 1) * 17 + yc];
                        let s2 = height_buffer[((xc + 1) * 5 + zc) * 17 + yc];
                        let s3 = height_buffer[((xc + 1) * 5 + (zc + 1)) * 17 + yc];

                        // Vertical interpolation steps (8 blocks per density cell)
                        let s0a = (height_buffer[(xc * 5 + zc) * 17 + yc + 1] - s0) / 8.0;
                        let s1a = (height_buffer[(xc * 5 + zc + 1) * 17 + yc + 1] - s1) / 8.0;
                        let s2a = (height_buffer[((xc + 1) * 5 + zc) * 17 + yc + 1] - s2) / 8.0;
                        let s3a =
                            (height_buffer[((xc + 1) * 5 + (zc + 1)) * 17 + yc + 1] - s3) / 8.0;

                        let mut ts0 = s0;
                        let mut ts1 = s1;
                        let mut ts2 = s2;
                        let mut ts3 = s3;

                        for y in 0..8 {
                            let mut tts0 = ts0;
                            let mut tts1 = ts2;
                            let tts0a = (ts1 - ts0) / 4.0;
                            let tts1a = (ts3 - ts2) / 4.0;

                            for x in 0..4 {
                                let mut ttts0 = tts0;
                                let ttts0a = (tts1 - tts0) / 4.0;
                                for z in 0..4 {
                                    let mut block = Block::Air;
                                    let by = yc * 8 + y;

                                    if ttts0 > 0.0 {
                                        block = Block::Stone;
                                    } else if by < SEA_LEVEL as usize {
                                        block = Block::Water;
                                    }

                                    let bx = xc * 4 + x;
                                    let bz = zc * 4 + z;
                                    blocks[(by * 16 * 16) + (bz * 16) + bx] = block as u8;

                                    ttts0 += ttts0a;
                                }
                                tts0 += tts0a;
                                tts1 += tts1a;
                            }
                            ts0 += s0a;
                            ts1 += s1a;
                            ts2 += s2a;
                            ts3 += s3a;
                        }
                    }
                }
            }

            // Phase 2: Build surfaces — replace top stone with biome-appropriate blocks
            // This creates grass/dirt layers, sand deserts, beaches, etc.
            let mut random = Random::new(
                (cx as i64)
                    .wrapping_mul(341873128712)
                    .wrapping_add((cz as i64).wrapping_mul(132897987541)),
            );

            for x in 0..16 {
                for z in 0..16 {
                    let biome = self
                        .biome_source
                        .get_biome(cx * 16 + x as i32, cz * 16 + z as i32);

                    // Surface depth from noise — varies between 0-5 to create natural variation
                    let noise_val = surface_depth_noise[x * 16 + z];
                    let surface_depth = (noise_val / 3.0 + 3.0 + random.next_float() * 0.25) as i32;

                    let top_block = biome.top_material();
                    let filler_block = biome.material();

                    let mut current_top = top_block;
                    let mut current_filler = filler_block;
                    let mut stone_count = -1i32;

                    for y in (0..128).rev() {
                        let idx = (y * 16 * 16) + (z * 16) + x;

                        // Bedrock layer: solid at y=0, scattered up to y=4
                        if y <= 4 {
                            if y == 0 || random.next_int_n(y as i32 + 1) == 0 {
                                blocks[idx] = Block::Bedrock as u8;
                                continue;
                            }
                        }

                        let b = blocks[idx];
                        if b == Block::Air as u8 || b == Block::Water as u8 {
                            stone_count = -1;

                            // If we just went from solid to air below sea level,
                            // the next surface block should be sand (beach/ocean floor)
                            if b == Block::Water as u8 && stone_count == -1 {
                                current_top = Block::Sand;
                                current_filler = Block::Sand;
                            }
                        } else if b == Block::Stone as u8 {
                            if stone_count == -1 {
                                if surface_depth <= 0 {
                                    // Very thin surface — expose stone
                                    current_top = Block::Air;
                                    current_filler = Block::Stone;
                                } else if y >= SEA_LEVEL as usize - 4 && y <= SEA_LEVEL as usize + 1
                                {
                                    // Near sea level — use biome materials or sand (beach)
                                    current_top = top_block;
                                    current_filler = filler_block;

                                    // Create sand beaches near water
                                    if y <= SEA_LEVEL as usize {
                                        current_top = Block::Sand;
                                        current_filler = Block::Sand;
                                    }
                                }

                                stone_count = surface_depth;

                                if y >= SEA_LEVEL as usize - 1 {
                                    blocks[idx] = current_top as u8;
                                } else {
                                    blocks[idx] = current_filler as u8;
                                }
                            } else if stone_count > 0 {
                                stone_count -= 1;
                                blocks[idx] = current_filler as u8;

                                // Add sandstone under sand (like MCPE)
                                if stone_count == 0 && current_filler == Block::Sand {
                                    stone_count = random.next_int_n(4);
                                    current_filler = Block::Sand; // MCPE uses Sandstone here, using Sand as substitute
                                }
                            }
                        }
                    }
                }
            }

            // Phase 3: Carve caves
            self.carve_caves(blocks, cx, cz);
        }

        chunk.dirty = true;
        chunk
    }

    fn post_process(&self, level: &mut crate::world::level::Level, radius: i32) {
        let mut random = Random::new(self.seed);

        for cx in -radius..=radius {
            for cz in -radius..=radius {
                let xo = cx * 16;
                let zo = cz * 16;

                let x_scale = random.next_int() / 2 * 2 + 1;
                let z_scale = random.next_int() / 2 * 2 + 1;
                let combined = cx
                    .wrapping_mul(x_scale)
                    .wrapping_add(cz.wrapping_mul(z_scale)) as i64;
                random.set_seed(combined ^ self.seed);

                let biome = self.biome_source.get_biome(xo + 8, zo + 8);

                // Tree count based on biome (matching MCPE counts)
                let tree_count =
                    match biome {
                        crate::world::levelgen::biome::Biome::RainForest => 4,
                        crate::world::levelgen::biome::Biome::Forest => 3,
                        crate::world::levelgen::biome::Biome::SeasonalForest => 2,
                        crate::world::levelgen::biome::Biome::Taiga => 2,
                        crate::world::levelgen::biome::Biome::Swampland => 2,
                        crate::world::levelgen::biome::Biome::Shrubland => 1,
                        crate::world::levelgen::biome::Biome::Savanna => 0,
                        crate::world::levelgen::biome::Biome::Plains => {
                            if random.next_int_n(10) == 0 { 1 } else { 0 }
                        }
                        crate::world::levelgen::biome::Biome::Desert
                        | crate::world::levelgen::biome::Biome::IceDesert => 0,
                        crate::world::levelgen::biome::Biome::Tundra => 0,
                    };

                for _ in 0..tree_count {
                    let x = xo + random.next_int_n(16) + 8;
                    let z = zo + random.next_int_n(16) + 8;
                    let y = level.get_top_block(x, z);
                    if y > 0 {
                        let tree = TreeFeature::new(0);
                        tree.place(level, &mut random, x, y + 1, z);
                    }
                }

                // Place some random sand/gravel patches on ocean and lake floors
                for _ in 0..3 {
                    let x = xo + random.next_int_n(16) + 8;
                    let z = zo + random.next_int_n(16) + 8;
                    let y = level.get_top_block(x, z);
                    if y > 0 && y < SEA_LEVEL {
                        // Place sand on ocean/lake floor
                        let block = level.get_block(x, y, z);
                        if block == Block::Dirt || block == Block::Grass {
                            level.set_block(x, y, z, Block::Sand);
                        }
                    }
                }

                // Place gravel patches (like MCPE)
                for _ in 0..1 {
                    let x = xo + random.next_int_n(16) + 8;
                    let z = zo + random.next_int_n(16) + 8;
                    let y = random.next_int_n(128);
                    // Place small gravel disk
                    for dx in -1..=1 {
                        for dz in -1..=1 {
                            let block = level.get_block(x + dx, y, z + dz);
                            if block == Block::Dirt || block == Block::Stone {
                                level.set_block(x + dx, y, z + dz, Block::Gravel);
                            }
                        }
                    }
                }
            }
        }
    }
}
