use glam::{Mat4, Vec3};

/// Player eye height above the block they stand on (matching MC 1.62 blocks).
const EYE_HEIGHT: f32 = 1.62;
/// Gravity in blocks per tick² (matching MC ~0.08).
const GRAVITY: f32 = 0.08;
/// Jump velocity in blocks per tick (matching MC ~0.42).
const JUMP_VELOCITY: f32 = 0.42;

/// First-person camera with ground-based FPS movement.
pub struct Camera {
    /// Player foot position (bottom of player hitbox).
    pub foot_x: f32,
    pub foot_y: f32,
    pub foot_z: f32,
    /// Vertical velocity for jumping/falling.
    pub vel_y: f32,
    /// Whether the player is on the ground.
    pub on_ground: bool,

    /// Yaw in degrees (0 = +Z).
    pub yaw: f32,
    /// Pitch in degrees (positive = looking down).
    pub pitch: f32,

    pub fov: f32,
    pub aspect: f32,
    pub near: f32,
    pub far: f32,
}

impl Camera {
    pub fn new() -> Self {
        Self {
            foot_x: 8.0,
            foot_y: 6.0, // Start on top of the grass (y=5) + a little
            foot_z: 8.0,
            vel_y: 0.0,
            on_ground: false,
            yaw: 0.0,
            pitch: 0.0,
            fov: 60.0_f32.to_radians(),
            aspect: 16.0 / 9.0,
            near: 0.05,
            far: 256.0,
        }
    }

    /// Eye position (camera position).
    pub fn eye_position(&self) -> Vec3 {
        Vec3::new(self.foot_x, self.foot_y + EYE_HEIGHT, self.foot_z)
    }

    /// Apply mouse look rotation.
    pub fn turn(&mut self, dx: f32, dy: f32) {
        self.yaw += dx * 0.15;
        self.pitch += dy * 0.15;
        self.pitch = self.pitch.clamp(-89.9, 89.9);
    }

    /// Get the look direction from yaw/pitch.
    pub fn forward_look(&self) -> Vec3 {
        let yaw_rad = self.yaw.to_radians();
        let pitch_rad = self.pitch.to_radians();
        let cos_pitch = pitch_rad.cos();
        Vec3::new(
            -yaw_rad.sin() * cos_pitch,
            -pitch_rad.sin(),
            yaw_rad.cos() * cos_pitch,
        )
    }

    /// Movement direction on the XZ plane (ignores pitch).
    fn forward_move(&self) -> Vec3 {
        let yaw_rad = self.yaw.to_radians();
        Vec3::new(-yaw_rad.sin(), 0.0, yaw_rad.cos())
    }

    fn right_move(&self) -> Vec3 {
        let yaw_rad = (self.yaw + 90.0).to_radians();
        Vec3::new(-yaw_rad.sin(), 0.0, yaw_rad.cos())
    }

    /// Process movement input each tick — ground-based FPS style with collision.
    pub fn tick(
        &mut self,
        forward: f32,
        strafe: f32,
        jump: bool,
        level: &crate::world::level::Level,
    ) {
        let block_at_feet = level.get_block(
            self.foot_x.floor() as i32,
            self.foot_y.floor() as i32,
            self.foot_z.floor() as i32,
        );
        let block_at_eyes = level.get_block(
            self.foot_x.floor() as i32,
            (self.foot_y + EYE_HEIGHT).floor() as i32,
            self.foot_z.floor() as i32,
        );
        let in_water = block_at_feet == crate::world::block::Block::Water
            || block_at_eyes == crate::world::block::Block::Water;

        let speed = if in_water { 0.08 } else { 0.2 }; // blocks per tick

        // Horizontal movement (XZ plane only)
        let fwd = self.forward_move();
        let rgt = self.right_move();
        let move_dir = (fwd * forward + rgt * strafe).normalize_or_zero();

        // Physics constants
        let player_width = 0.6;
        let player_height = 1.8;
        let radius = player_width / 2.0;

        // X movement
        let next_x = self.foot_x + move_dir.x * speed;
        if !self.collides(
            next_x,
            self.foot_y,
            self.foot_z,
            radius,
            player_height,
            level,
        ) {
            self.foot_x = next_x;
        }

        // Z movement
        let next_z = self.foot_z + move_dir.z * speed;
        if !self.collides(
            self.foot_x,
            self.foot_y,
            next_z,
            radius,
            player_height,
            level,
        ) {
            self.foot_z = next_z;
        }

        // Gravity & jumping
        if in_water {
            if jump {
                self.vel_y += 0.04;
                if self.vel_y > 0.15 {
                    self.vel_y = 0.15;
                }
            } else {
                self.vel_y -= 0.02; // Slower gravity in water
            }
            if self.vel_y < -0.15 {
                self.vel_y = -0.15; // Terminal velocity in water
            }
            self.on_ground = false;
        } else if self.on_ground {
            self.vel_y = 0.0;
            if jump {
                self.vel_y = JUMP_VELOCITY;
                self.on_ground = false;
            }
        } else {
            self.vel_y -= GRAVITY;
        }

        // Y movement
        let next_y = self.foot_y + self.vel_y;
        if self.vel_y < 0.0 {
            // Falling
            if self.collides(
                self.foot_x,
                next_y,
                self.foot_z,
                radius,
                player_height,
                level,
            ) {
                // Landed
                self.foot_y = next_y.ceil(); // Snap to top of block
                self.vel_y = 0.0;
                self.on_ground = true;
            } else {
                self.foot_y = next_y;
                self.on_ground = false;
            }
        } else if self.vel_y > 0.0 {
            // Jumping/Rising
            if self.collides(
                self.foot_x,
                next_y,
                self.foot_z,
                radius,
                player_height,
                level,
            ) {
                // Hit head
                self.vel_y = 0.0;
            } else {
                self.foot_y = next_y;
                self.on_ground = false;
            }
        } else if !self.on_ground {
            // Just move if not on ground and no velocity (edge case)
            self.foot_y = next_y;
        }

        // Final sanity check for "hanging" off edges
        if self.on_ground
            && !self.collides(
                self.foot_x,
                self.foot_y - 0.1,
                self.foot_z,
                radius,
                player_height,
                level,
            )
        {
            self.on_ground = false;
        }
    }

    fn collides(
        &self,
        x: f32,
        y: f32,
        z: f32,
        radius: f32,
        height: f32,
        level: &crate::world::level::Level,
    ) -> bool {
        let min_x = (x - radius).floor() as i32;
        let max_x = (x + radius).floor() as i32;
        let min_y = y.floor() as i32;
        let max_y = (y + height).floor() as i32;
        let min_z = (z - radius).floor() as i32;
        let max_z = (z + radius).floor() as i32;

        for bx in min_x..=max_x {
            for by in min_y..=max_y {
                for bz in min_z..=max_z {
                    let block = level.get_block(bx, by, bz);
                    if block != crate::world::block::Block::Air
                        && block != crate::world::block::Block::Water
                    {
                        return true;
                    }
                }
            }
        }
        false
    }

    /// Combined view-projection matrix.
    pub fn view_proj_matrix(&self) -> [[f32; 4]; 4] {
        let eye = self.eye_position();
        let forward = self.forward_look().normalize();
        let target = eye + forward;

        let view = Mat4::look_at_rh(eye, target, Vec3::Y);
        let proj = Mat4::perspective_rh(self.fov, self.aspect, self.near, self.far);

        (proj * view).to_cols_array_2d()
    }
}
