use super::noise::PerlinNoise;
use super::random::Random;
use crate::world::block::Block;

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Biome {
    RainForest,
    Swampland,
    SeasonalForest,
    Forest,
    Savanna,
    Shrubland,
    Taiga,
    Desert,
    Plains,
    IceDesert,
    Tundra,
}

impl Biome {
    pub fn get_biome(temperature: f32, downfall: f32) -> Biome {
        let downfall = downfall * temperature;
        if temperature < 0.10 {
            Biome::Tundra
        } else if downfall < 0.20 {
            if temperature < 0.50 {
                Biome::Tundra
            } else if temperature < 0.95 {
                Biome::Savanna
            } else {
                Biome::Desert
            }
        } else if downfall > 0.5 && temperature < 0.7 {
            Biome::Swampland
        } else if temperature < 0.50 {
            Biome::Taiga
        } else if temperature < 0.97 {
            if downfall < 0.35 {
                Biome::Shrubland
            } else {
                Biome::Forest
            }
        } else {
            if downfall < 0.45 {
                Biome::Plains
            } else if downfall < 0.90 {
                Biome::SeasonalForest
            } else {
                Biome::RainForest
            }
        }
    }

    pub fn top_material(&self) -> Block {
        match self {
            Biome::Desert | Biome::IceDesert => Block::Sand,
            _ => Block::Grass,
        }
    }

    pub fn material(&self) -> Block {
        match self {
            Biome::Desert | Biome::IceDesert => Block::Sand,
            _ => Block::Dirt,
        }
    }
}

pub struct BiomeSource {
    temperature_map: PerlinNoise,
    downfall_map: PerlinNoise,
    noise_map: PerlinNoise,
}

impl BiomeSource {
    pub fn new(seed: i64) -> Self {
        // Use a separate Random seeded specially for biome noise
        // to not interfere with the main terrain noise sequence
        let mut random = Random::new(seed.wrapping_mul(9871));
        let temperature_map = PerlinNoise::new(&mut random, 4);
        let mut random = Random::new(seed.wrapping_mul(39811));
        let downfall_map = PerlinNoise::new(&mut random, 4);
        let mut random = Random::new(seed.wrapping_mul(543321));
        let noise_map = PerlinNoise::new(&mut random, 2);
        Self {
            temperature_map,
            downfall_map,
            noise_map,
        }
    }

    pub fn get_temp_downfall(&self, x: i32, z: i32) -> (f32, f32) {
        let mut temp_buf = [0.0f32; 1];
        let mut down_buf = [0.0f32; 1];

        // MCPE-accurate scale factors for biome noise
        // These produce biome regions of appropriate size (~256-512 blocks)
        self.temperature_map.get_region(
            &mut temp_buf,
            x as f32,
            0.0,
            z as f32,
            1,
            1,
            1,
            0.02500,
            0.02500,
            0.02500,
        );
        self.downfall_map.get_region(
            &mut down_buf,
            x as f32,
            0.0,
            z as f32,
            1,
            1,
            1,
            0.05000,
            0.05000,
            0.05000,
        );

        // Wider range so we actually get varied biomes
        let temp = (temp_buf[0] * 1.5 + 0.5).clamp(0.0, 1.0);
        let down = (down_buf[0] * 1.5 + 0.5).clamp(0.0, 1.0);
        (temp, down)
    }

    pub fn get_biome(&self, x: i32, z: i32) -> Biome {
        let (temp, down) = self.get_temp_downfall(x, z);
        Biome::get_biome(temp, down)
    }

    pub fn get_biomes_for_chunk(&self, cx: i32, cz: i32, biomes: &mut [Biome; 16 * 16]) {
        let x_start = cx * 16;
        let z_start = cz * 16;

        for z in 0..16 {
            for x in 0..16 {
                biomes[z * 16 + x] = self.get_biome(x_start + x as i32, z_start + z as i32);
            }
        }
    }
}
