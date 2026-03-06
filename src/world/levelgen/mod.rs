pub mod biome;
pub mod feature;
pub mod noise;
pub mod overworld;
pub mod random;

pub use overworld::OverworldGenerator;

use super::chunk::Chunk;
use super::level::Level;

pub trait ChunkGenerator {
    fn generate_chunk(&self, cx: i32, cz: i32) -> Chunk;
    fn post_process(&self, level: &mut Level, radius: i32);
}
