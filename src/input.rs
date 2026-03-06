use winit::event::ElementState;
use winit::keyboard::{KeyCode, PhysicalKey};

/// Tracks keyboard and mouse input state, similar to the C++ Mouse/Keyboard/Multitouch classes.
pub struct InputState {
    // Movement keys
    pub forward: bool,
    pub backward: bool,
    pub left: bool,
    pub right: bool,
    pub jump: bool,
    pub sneak: bool,

    // Mouse state
    pub mouse_dx: f64,
    pub mouse_dy: f64,
    pub mouse_left: bool,
    pub mouse_right: bool,
    pub mouse_left_just_pressed: bool,
    pub mouse_right_just_pressed: bool,
    pub mouse_grabbed: bool,
}

impl InputState {
    pub fn new() -> Self {
        Self {
            forward: false,
            backward: false,
            left: false,
            right: false,
            jump: false,
            sneak: false,
            mouse_dx: 0.0,
            mouse_dy: 0.0,
            mouse_left: false,
            mouse_right: false,
            mouse_left_just_pressed: false,
            mouse_right_just_pressed: false,
            mouse_grabbed: true,
        }
    }

    pub fn handle_key(&mut self, key: PhysicalKey, state: ElementState) {
        let pressed = state == ElementState::Pressed;
        match key {
            PhysicalKey::Code(KeyCode::KeyW) => self.forward = pressed,
            PhysicalKey::Code(KeyCode::KeyS) => self.backward = pressed,
            PhysicalKey::Code(KeyCode::KeyA) => self.left = pressed,
            PhysicalKey::Code(KeyCode::KeyD) => self.right = pressed,
            PhysicalKey::Code(KeyCode::Space) => self.jump = pressed,
            PhysicalKey::Code(KeyCode::ShiftLeft) => self.sneak = pressed,
            _ => {}
        }
    }

    pub fn handle_mouse_motion(&mut self, dx: f64, dy: f64) {
        self.mouse_dx += dx;
        self.mouse_dy += dy;
    }

    pub fn handle_mouse_click(&mut self, button: winit::event::MouseButton, state: ElementState) {
        let pressed = state == ElementState::Pressed;
        match button {
            winit::event::MouseButton::Left => {
                if pressed && !self.mouse_left {
                    self.mouse_left_just_pressed = true;
                }
                self.mouse_left = pressed;
            }
            winit::event::MouseButton::Right => {
                if pressed && !self.mouse_right {
                    self.mouse_right_just_pressed = true;
                }
                self.mouse_right = pressed;
            }
            _ => {}
        }
    }

    /// Reset per-frame deltas and one-shot clicks.
    pub fn reset_deltas(&mut self) {
        self.mouse_dx = 0.0;
        self.mouse_dy = 0.0;
        self.mouse_left_just_pressed = false;
        self.mouse_right_just_pressed = false;
    }
}
