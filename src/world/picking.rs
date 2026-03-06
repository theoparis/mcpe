use crate::world::level::Level;
use glam::{IVec3, Vec3};

#[derive(Debug, Clone, Copy)]
pub struct PickingResult {
    pub block_pos: IVec3,
    pub face_normal: IVec3,
}

/// A simple DDA (Digital Differential Analyzer) raycaster for block picking.
/// This matches the logic used in the original Minecraft (pick()).
pub fn pick(level: &Level, origin: Vec3, direction: Vec3, max_dist: f32) -> Option<PickingResult> {
    let mut x = origin.x.floor() as i32;
    let mut y = origin.y.floor() as i32;
    let mut z = origin.z.floor() as i32;

    let step_x = if direction.x > 0.0 { 1 } else { -1 };
    let step_y = if direction.y > 0.0 { 1 } else { -1 };
    let step_z = if direction.z > 0.0 { 1 } else { -1 };

    let t_delta_x = (1.0 / direction.x).abs();
    let t_delta_y = (1.0 / direction.y).abs();
    let t_delta_z = (1.0 / direction.z).abs();

    let mut t_max_x = if direction.x > 0.0 {
        (x as f32 + 1.0 - origin.x) * t_delta_x
    } else {
        (origin.x - x as f32) * t_delta_x
    };
    let mut t_max_y = if direction.y > 0.0 {
        (y as f32 + 1.0 - origin.y) * t_delta_y
    } else {
        (origin.y - y as f32) * t_delta_y
    };
    let mut t_max_z = if direction.z > 0.0 {
        (z as f32 + 1.0 - origin.z) * t_delta_z
    } else {
        (origin.z - z as f32) * t_delta_z
    };

    loop {
        let t = t_max_x.min(t_max_y).min(t_max_z);
        if t > max_dist {
            return None;
        }

        let face_normal;
        if t_max_x < t_max_y && t_max_x < t_max_z {
            x += step_x;
            t_max_x += t_delta_x;
            face_normal = IVec3::new(-step_x, 0, 0);
        } else if t_max_y < t_max_z {
            y += step_y;
            t_max_y += t_delta_y;
            face_normal = IVec3::new(0, -step_y, 0);
        } else {
            z += step_z;
            t_max_z += t_delta_z;
            face_normal = IVec3::new(0, 0, -step_z);
        }

        let block = level.get_block(x, y, z);
        if !block.is_air() {
            return Some(PickingResult {
                block_pos: IVec3::new(x, y, z),
                face_normal,
            });
        }
    }
}
