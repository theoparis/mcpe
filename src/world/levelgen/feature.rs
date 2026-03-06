use super::super::block::Block;
use super::super::level::Level;
use super::random::Random;

pub trait Feature {
    fn place(&self, level: &mut Level, random: &mut Random, x: i32, y: i32, z: i32) -> bool;
}

pub struct TreeFeature {
    trunk_type: u8,
}

impl TreeFeature {
    pub fn new(trunk_type: u8) -> Self {
        Self { trunk_type }
    }
}

impl Feature for TreeFeature {
    fn place(&self, level: &mut Level, random: &mut Random, x: i32, y: i32, z: i32) -> bool {
        let tree_height = random.next_int_n(3) + 4;

        let mut free = true;
        if y < 1 || y + tree_height + 1 > 128 {
            return false;
        }

        for yy in y..=(y + 1 + tree_height) {
            let mut r = 1;
            if yy == y {
                r = 0;
            }
            if yy >= y + 1 + tree_height - 2 {
                r = 2;
            }
            for xx in (x - r)..=(x + r) {
                if !free {
                    break;
                }
                for zz in (z - r)..=(z + r) {
                    if !free {
                        break;
                    }
                    if yy >= 0 && yy < 128 {
                        let tt = level.get_block(xx, yy, zz);
                        if !tt.is_air() && tt != Block::OakLeaves {
                            free = false;
                        }
                    } else {
                        free = false;
                    }
                }
            }
        }

        if !free {
            return false;
        }

        let below_tile = level.get_block(x, y - 1, z);
        if (below_tile != Block::Grass && below_tile != Block::Dirt) || y >= 128 - tree_height - 1 {
            return false;
        }

        level.set_block(x, y - 1, z, Block::Dirt);

        for yy in (y - 3 + tree_height)..=(y + tree_height) {
            let yo = yy - (y + tree_height);
            let offs = 1 - yo / 2;
            for xx in (x - offs)..=(x + offs) {
                let xo = xx - x;
                for zz in (z - offs)..=(z + offs) {
                    let zo = zz - z;
                    if (xo.abs() == offs)
                        && (zo.abs() == offs)
                        && (random.next_int_n(2) == 0 || yo == 0)
                    {
                        continue;
                    }
                    let t = level.get_block(xx, yy, zz);
                    if t.is_air() || t.is_transparent() {
                        level.set_block(xx, yy, zz, Block::OakLeaves);
                    }
                }
            }
        }

        for hh in 0..tree_height {
            let t = level.get_block(x, y + hh, z);
            if t.is_air() || t == Block::OakLeaves {
                level.set_block(x, y + hh, z, Block::OakLog); // Assuming trunk_type 0 is OakWood
            }
        }
        true
    }
}

pub struct FlowerFeature {
    block: Block,
}

impl FlowerFeature {
    pub fn new(block: Block) -> Self {
        Self { block }
    }
}

impl Feature for FlowerFeature {
    fn place(&self, level: &mut Level, random: &mut Random, x: i32, y: i32, z: i32) -> bool {
        for _ in 0..64 {
            let x2 = x + random.next_int_n(8) - random.next_int_n(8);
            let y2 = y + random.next_int_n(4) - random.next_int_n(4);
            let z2 = z + random.next_int_n(8) - random.next_int_n(8);

            if level.get_block(x2, y2, z2).is_air() {
                let below = level.get_block(x2, y2 - 1, z2);
                if below == Block::Grass || below == Block::Dirt {
                    level.set_block(x2, y2, z2, self.block);
                }
            }
        }
        true
    }
}
