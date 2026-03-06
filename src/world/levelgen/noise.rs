use super::random::Random;
use noise_functions::{Noise, NoiseFn, Perlin};

pub struct PerlinNoise {
    seeds: Vec<i32>,
}

impl PerlinNoise {
    pub fn new(random: &mut Random, levels: usize) -> Self {
        let mut seeds = Vec::with_capacity(levels);
        for _ in 0..levels {
            seeds.push(random.next_int());
        }
        Self { seeds }
    }

    pub fn get_region(
        &self,
        buffer: &mut [f32],
        x: f32,
        y: f32,
        z: f32,
        x_size: usize,
        y_size: usize,
        z_size: usize,
        x_scale_in: f32,
        y_scale_in: f32,
        z_scale_in: f32,
    ) {
        buffer.fill(0.0);
        let mut pow = 1.0f32;
        for &seed in &self.seeds {
            let noise = Perlin.seed(seed);
            let mut idx = 0;
            let scale = 1.0 / pow;

            if y_size == 1 {
                for xi in 0..x_size {
                    let cx = (x + xi as f32) * x_scale_in * pow;
                    for zi in 0..z_size {
                        let cz = (z + zi as f32) * z_scale_in * pow;
                        // Use 2D noise for plain maps
                        buffer[idx] += noise.sample2([cx, cz]) * scale;
                        idx += 1;
                    }
                }
            } else {
                for xi in 0..x_size {
                    let cx = (x + xi as f32) * x_scale_in * pow;
                    for zi in 0..z_size {
                        let cz = (z + zi as f32) * z_scale_in * pow;
                        for yi in 0..y_size {
                            let cy = (y + yi as f32) * y_scale_in * pow;
                            buffer[idx] += noise.sample3([cx, cy, cz]) * scale;
                            idx += 1;
                        }
                    }
                }
            }
            pow /= 2.0;
        }
    }

    pub fn get_region2d(
        &self,
        buffer: &mut [f32],
        x: f32,
        z: f32,
        x_size: usize,
        z_size: usize,
        x_scale_in: f32,
        z_scale_in: f32,
        amplitude: f32,
    ) {
        buffer.fill(0.0);
        let mut pow = amplitude;
        for &seed in &self.seeds {
            let noise = Perlin.seed(seed);
            let mut idx = 0;
            let scale = 1.0 / pow;

            for xi in 0..x_size {
                let cx = (x + xi as f32) * x_scale_in * pow;
                for zi in 0..z_size {
                    let cz = (z + zi as f32) * z_scale_in * pow;
                    // Force 2D
                    buffer[idx] += noise.sample2([cx, cz]) * scale;
                    idx += 1;
                }
            }
            pow /= 2.0;
        }
    }

    pub fn get_value(&self, x: f32, y: f32, z: f32) -> f32 {
        let mut val = 0.0;
        let mut pow = 1.0f32;
        for &seed in &self.seeds {
            let noise = Perlin.seed(seed);
            val += noise.sample3([x * pow, y * pow, z * pow]) / pow;
            pow /= 2.0;
        }
        val
    }
}
