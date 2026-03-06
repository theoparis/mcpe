/// Block type IDs matching the original MCPE tile IDs.
/// Only the most essential blocks for a basic flat world.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum Block {
    Air = 0,
    Stone = 1,
    Grass = 2,
    Dirt = 3,
    Cobblestone = 4,
    Planks = 5,
    Bedrock = 7,
    Water = 9,
    Lava = 11,
    Sand = 12,
    Gravel = 13,
    OakLog = 17,
    OakLeaves = 18,
    Glass = 20,
    Brick = 45,
}

impl Block {
    pub fn from_id(id: u8) -> Self {
        match id {
            0 => Block::Air,
            1 => Block::Stone,
            2 => Block::Grass,
            3 => Block::Dirt,
            4 => Block::Cobblestone,
            5 => Block::Planks,
            7 => Block::Bedrock,
            9 => Block::Water,
            11 => Block::Lava,
            12 => Block::Sand,
            13 => Block::Gravel,
            17 => Block::OakLog,
            18 => Block::OakLeaves,
            20 => Block::Glass,
            45 => Block::Brick,
            _ => Block::Air,
        }
    }

    pub fn is_air(self) -> bool {
        self == Block::Air
    }

    pub fn is_transparent(self) -> bool {
        matches!(
            self,
            Block::Air | Block::Glass | Block::Water | Block::OakLeaves
        )
    }

    pub fn hardness(self) -> f32 {
        match self {
            Block::Bedrock => -1.0, // unbreakable
            Block::Stone => 1.5,
            Block::Grass | Block::Dirt | Block::Sand | Block::Gravel => 0.5,
            Block::Cobblestone | Block::Brick => 2.0,
            Block::Planks | Block::OakLog => 2.0,
            Block::OakLeaves => 0.2,
            Block::Glass => 0.3,
            Block::Water | Block::Lava | Block::Air => 0.0,
        }
    }

    /// Returns (top, side, bottom) texture atlas indices for terrain.png.
    /// terrain.png is a 16x16 grid of 16x16 pixel tiles.
    /// Index = row * 16 + col.
    pub fn texture_indices(self) -> (u32, u32, u32) {
        match self {
            Block::Air => (0, 0, 0),
            Block::Stone => (1, 1, 1),
            Block::Grass => (0, 3, 2), // top=grass_top, side=grass_side, bottom=dirt
            Block::Dirt => (2, 2, 2),
            Block::Cobblestone => (16, 16, 16),
            Block::Planks => (4, 4, 4),
            Block::Bedrock => (17, 17, 17),
            Block::Water => (207, 207, 207), // still water
            Block::Lava => (239, 239, 239),  // still lava
            Block::Sand => (18, 18, 18),
            Block::Gravel => (19, 19, 19),
            Block::OakLog => (21, 20, 21), // top=log_top, side=log_side, bottom=log_top
            Block::OakLeaves => (52, 52, 52),
            Block::Glass => (49, 49, 49),
            Block::Brick => (7, 7, 7),
        }
    }
}
