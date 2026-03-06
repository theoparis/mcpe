mod input;
mod platform;
mod render;
mod timer;
mod world;

use std::sync::Arc;
use std::time::Instant;

use render::camera::Camera;
use render::context::{RenderContext, RenderSurface};
use render::selection::{SelectionRenderer, SelectionUniforms};
use render::terrain::{TerrainRenderer, TerrainUniforms};
use render::ui::{GuiRenderer, GuiVertex};
use timer::Timer;
use world::level::Level;
use world::levelgen::OverworldGenerator;
use world::picking;

use winit::{
    application::ApplicationHandler,
    event::{DeviceEvent, ElementState, WindowEvent},
    event_loop::{ActiveEventLoop, EventLoop},
    window::{CursorGrabMode, Window, WindowId},
};

use crate::world::block::Block;

const WINDOW_WIDTH: u32 = 848;
const WINDOW_HEIGHT: u32 = 480;
const TICKS_PER_SECOND: f32 = 20.0;
const MESH_JOB_BUDGET: usize = 4;
const MESH_UPLOAD_BUDGET: usize = 6;

/// Main application state, corresponding to the C++ MinecraftApp.
struct App<'s> {
    // Rendering
    context: Option<RenderContext>,
    surface: Option<RenderSurface<'s>>,
    window: Option<Arc<Window>>,
    terrain_renderer: Option<TerrainRenderer>,
    gui_renderer: Option<GuiRenderer>,
    selection_renderer: Option<SelectionRenderer>,

    // Text rendering with glyphon
    text_renderer: Option<glyphon::TextRenderer>,
    font_system: Option<glyphon::FontSystem>,
    text_atlas: Option<glyphon::TextAtlas>,
    swash_cache: Option<glyphon::SwashCache>,
    viewport: Option<glyphon::Viewport>,
    glyph_cache: Option<glyphon::Cache>,

    // Game state
    level: Level,
    camera: Camera,
    input: input::InputState,
    timer: Timer,
    selected_slot: u32,
    storage: world::storage::LevelStorage,
    frame_count: u32,
    fps_update_time: Instant,
    current_fps: f64,

    // Breaking state
    break_pos: Option<glam::IVec3>,
    break_progress: f32,

    // Asset data
    terrain_data: Option<(Vec<u8>, u32, u32)>,
    gui_data: Option<(Vec<u8>, u32, u32)>,
    font_data: Option<Vec<u8>>,

    // Mesh update state
    lod_distance: f32,

    // UI state
    paused: bool,
    show_debug: bool,
}

fn main() {
    env_logger::init();

    // Load assets
    let data_dir = platform::find_data_dir();

    let path = data_dir.join("images").join("terrain.png");
    let terrain_data = platform::load_png(&path);

    let path = data_dir.join("images").join("gui").join("gui.png");
    let gui_data = platform::load_png(&path);

    let path = data_dir.join("fonts").join("monocraft.ttc");
    let font_data = std::fs::read(&path).ok();

    if terrain_data.is_none() || gui_data.is_none() || font_data.is_none() {
        log::error!("Failed to load assets from {:?}", data_dir);
    }

    // Initialize storage
    let storage_path = std::env::current_dir()
        .unwrap()
        .join("worlds")
        .join("default");
    let storage = world::storage::LevelStorage::new(&storage_path);

    // Try to load world, otherwise generate
    let level = match storage.load_level() {
        Ok(l) => {
            log::info!("Loaded world from {:?}", storage_path);
            l
        }
        Err(_) => {
            log::info!("Generating new world...");
            let mut l = Level::new();
            l.seed = 12345;
            let generator = OverworldGenerator::new(l.seed);
            l.generate(12, &generator); // 12 radius = 25x25 chunks
            l
        }
    };

    let mut camera = Camera::new();
    let mut spawn_x = level.spawn_x;
    let mut spawn_z = level.spawn_z;
    let mut spawn_y = level.get_top_block(spawn_x, spawn_z);

    if spawn_y == 0 {
        spawn_x = 8;
        spawn_z = 8;
        spawn_y = level.get_top_block(spawn_x, spawn_z);
    }

    // Fallback if still 0
    if spawn_y == 0 {
        'search: for r in 1..16 {
            for dx in -r..=r {
                for dz in -r..=r {
                    let y = level.get_top_block(spawn_x + dx, spawn_z + dz);
                    if y > 0 {
                        spawn_x += dx;
                        spawn_z += dz;
                        spawn_y = y;
                        break 'search;
                    }
                }
            }
        }
    }

    camera.foot_x = spawn_x as f32 + 0.5;
    camera.foot_y = spawn_y as f32 + 1.1;
    camera.foot_z = spawn_z as f32 + 0.5;
    camera.on_ground = false;

    log::info!(
        "Spawned player at ({:.1}, {:.1}, {:.1})",
        camera.foot_x,
        camera.foot_y,
        camera.foot_z
    );

    let now = Instant::now();
    let app = App {
        context: None,
        surface: None,
        window: None,
        terrain_renderer: None,
        gui_renderer: None,
        selection_renderer: None,

        text_renderer: None,
        font_system: None,
        text_atlas: None,
        swash_cache: None,
        viewport: None,
        glyph_cache: None,

        level,
        camera,
        input: input::InputState::new(),
        timer: Timer::new(TICKS_PER_SECOND),
        selected_slot: 0,
        storage,

        frame_count: 0,
        fps_update_time: now,
        current_fps: 60.0,

        break_pos: None,
        break_progress: 0.0,

        terrain_data,
        gui_data,
        font_data,

        lod_distance: 128.0,

        paused: false,
        show_debug: false,
    };

    let event_loop = EventLoop::new().unwrap();
    event_loop.set_control_flow(winit::event_loop::ControlFlow::Poll);

    let mut app = app;
    event_loop.run_app(&mut app).expect("Event loop failed");
}

impl ApplicationHandler for App<'_> {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.window.is_some() {
            return;
        }

        let attr = Window::default_attributes()
            .with_inner_size(winit::dpi::PhysicalSize::new(WINDOW_WIDTH, WINDOW_HEIGHT))
            .with_resizable(true)
            .with_title("Minecraft – Pocket Edition");

        let window = Arc::new(event_loop.create_window(attr).unwrap());
        let (context, surface) = pollster::block_on(RenderContext::new_for_window(window.clone()));
        let size = window.inner_size();
        if size.height > 0 {
            self.camera.aspect = size.width as f32 / size.height as f32;
        }

        // Initialize terrain renderer
        if let Some((ref rgba, tw, th)) = self.terrain_data {
            let terrain_renderer = TerrainRenderer::new(
                &context.device,
                &context.queue,
                surface.config.format,
                size.width,
                size.height,
                rgba,
                tw,
                th,
            );
            self.terrain_renderer = Some(terrain_renderer);
        }

        self.selection_renderer = Some(SelectionRenderer::new(
            &context.device,
            surface.config.format,
        ));

        // Initialize GUI renderer
        if let Some((ref rgba, gw, gh)) = self.gui_data {
            let gui_renderer = GuiRenderer::new(
                &context.device,
                &context.queue,
                surface.config.format,
                rgba,
                gw,
                gh,
            );
            self.gui_renderer = Some(gui_renderer);
        }

        // Initialize Glyphon text rendering
        let mut font_system = glyphon::FontSystem::new();
        if let Some(ref data) = self.font_data {
            font_system.db_mut().load_font_data(data.clone());
        }

        let swash_cache = glyphon::SwashCache::new();
        let glyph_cache = glyphon::Cache::new(&context.device);
        let mut text_atlas = glyphon::TextAtlas::new(
            &context.device,
            &context.queue,
            &glyph_cache,
            surface.config.format,
        );
        let text_renderer = glyphon::TextRenderer::new(
            &mut text_atlas,
            &context.device,
            wgpu::MultisampleState::default(),
            None,
        );
        let viewport = glyphon::Viewport::new(&context.device, &glyph_cache);

        self.font_system = Some(font_system);
        self.swash_cache = Some(swash_cache);
        self.text_atlas = Some(text_atlas);
        self.text_renderer = Some(text_renderer);
        self.viewport = Some(viewport);
        self.glyph_cache = Some(glyph_cache);

        self.context = Some(context);
        self.surface = Some(surface);
        self.window = Some(window.clone());

        self.set_cursor_grab(true);
    }

    fn suspended(&mut self, _event_loop: &ActiveEventLoop) {
        self.terrain_renderer = None;
        self.gui_renderer = None;
        self.selection_renderer = None;
        self.text_renderer = None;
        self.font_system = None;
        self.text_atlas = None;
        self.swash_cache = None;
        self.viewport = None;
        self.glyph_cache = None;
        self.surface = None;
        self.context = None;
    }

    fn device_event(
        &mut self,
        _event_loop: &ActiveEventLoop,
        _device_id: winit::event::DeviceId,
        event: DeviceEvent,
    ) {
        if let DeviceEvent::MouseMotion { delta: (dx, dy) } = event
            && self.input.mouse_grabbed
        {
            self.input.handle_mouse_motion(dx, dy);
        }
    }

    fn window_event(
        &mut self,
        event_loop: &ActiveEventLoop,
        _window_id: WindowId,
        event: WindowEvent,
    ) {
        match event {
            WindowEvent::CloseRequested => event_loop.exit(),
            WindowEvent::Resized(size) => {
                if let (Some(ctx), Some(surface)) = (&self.context, &mut self.surface) {
                    ctx.resize_surface(surface, size.width, size.height);
                    if size.height > 0 {
                        self.camera.aspect = size.width as f32 / size.height as f32;
                    }
                    if let Some(ref mut tr) = self.terrain_renderer {
                        tr.resize(&ctx.device, size.width, size.height);
                    }
                }
            }
            WindowEvent::Focused(true) if self.input.mouse_grabbed => {
                self.set_cursor_grab(true);
            }
            WindowEvent::KeyboardInput { event, .. } if event.state == ElementState::Pressed => {
                if let winit::keyboard::PhysicalKey::Code(key_code) = event.physical_key {
                    match key_code {
                        winit::keyboard::KeyCode::Escape => {
                            self.paused = !self.paused;
                            self.set_cursor_grab(!self.paused);
                        }
                        winit::keyboard::KeyCode::F3 => {
                            self.show_debug = !self.show_debug;
                        }
                        winit::keyboard::KeyCode::Digit1 => self.selected_slot = 0,
                        winit::keyboard::KeyCode::Digit2 => self.selected_slot = 1,
                        winit::keyboard::KeyCode::Digit3 => self.selected_slot = 2,
                        winit::keyboard::KeyCode::Digit4 => self.selected_slot = 3,
                        winit::keyboard::KeyCode::Digit5 => self.selected_slot = 4,
                        winit::keyboard::KeyCode::Digit6 => self.selected_slot = 5,
                        winit::keyboard::KeyCode::Digit7 => self.selected_slot = 6,
                        winit::keyboard::KeyCode::Digit8 => self.selected_slot = 7,
                        winit::keyboard::KeyCode::Digit9 => self.selected_slot = 8,
                        winit::keyboard::KeyCode::KeyS if !self.paused => {
                            // Update spawn point before saving
                            self.level.spawn_x = self.camera.foot_x.floor() as i32;
                            self.level.spawn_y = self.camera.foot_y.floor() as i32;
                            self.level.spawn_z = self.camera.foot_z.floor() as i32;
                            if let Err(e) = self.storage.save_level(&self.level) {
                                log::error!("Failed to save level: {}", e);
                            } else {
                                log::info!("Level saved!");
                            }
                        }
                        _ => {}
                    }
                }
                if !self.paused {
                    self.input.handle_key(event.physical_key, event.state);
                }
            }
            WindowEvent::KeyboardInput { event, .. } => {
                if !self.paused {
                    self.input.handle_key(event.physical_key, event.state);
                }
            }
            WindowEvent::MouseInput {
                state: ElementState::Pressed,
                button,
                ..
            } if self.paused => {
                // Clicking while paused resumes the game
                self.paused = false;
                self.set_cursor_grab(true);
                self.input.handle_mouse_click(button, ElementState::Pressed);
            }
            WindowEvent::MouseInput {
                state: ElementState::Pressed,
                button,
                ..
            } if !self.input.mouse_grabbed => {
                self.set_cursor_grab(true);
                self.input.handle_mouse_click(button, ElementState::Pressed);
            }
            WindowEvent::MouseInput { state, button, .. } => {
                self.input.handle_mouse_click(button, state);
            }
            WindowEvent::RedrawRequested => {
                self.update_and_render();
            }
            _ => {}
        }
    }

    fn about_to_wait(&mut self, _event_loop: &ActiveEventLoop) {
        if let Some(ref w) = self.window {
            w.request_redraw();
        }
    }
}

impl App<'_> {
    fn set_cursor_grab(&mut self, grab: bool) {
        if let Some(ref window) = self.window {
            if grab {
                // Centering the cursor is important on macOS/Linux to avoid it escaping before the lock takes hold
                let size = window.inner_size();
                let _ = window.set_cursor_position(winit::dpi::PhysicalPosition::new(
                    size.width / 2,
                    size.height / 2,
                ));

                let _ = window.set_cursor_grab(CursorGrabMode::Locked).or_else(|e| {
                    log::warn!("Failed to lock cursor: {:?}, trying confined", e);
                    window.set_cursor_grab(CursorGrabMode::Confined)
                });
                window.set_cursor_visible(false);
                self.input.mouse_grabbed = true;
            } else {
                let _ = window.set_cursor_grab(CursorGrabMode::None);
                window.set_cursor_visible(true);
                self.input.mouse_grabbed = false;
            }
        }
    }

    fn update_and_render(&mut self) {
        if self.context.is_none() || self.surface.is_none() {
            return;
        }

        self.timer.advance_time();
        if !self.paused {
            for _ in 0..self.timer.ticks {
                self.tick();
            }
        }

        self.frame_count += 1;
        let now = Instant::now();
        let elapsed = now.duration_since(self.fps_update_time).as_secs_f64();
        if elapsed >= 1.0 {
            self.current_fps = self.frame_count as f64 / elapsed;
            self.frame_count = 0;
            self.fps_update_time = now;
        }

        if self.input.mouse_grabbed
            && let Some(ref w) = self.window
        {
            w.set_cursor_visible(false);
        }

        if !self.paused {
            self.camera
                .turn(self.input.mouse_dx as f32, self.input.mouse_dy as f32);
        }

        let pick_res = picking::pick(
            &self.level,
            self.camera.eye_position(),
            self.camera.forward_look(),
            5.0,
        );
        let tick_count = self.timer.ticks;
        self.handle_block_interactions(pick_res, tick_count);

        let ctx = self.context.as_ref().unwrap();
        let surface = self.surface.as_ref().unwrap();

        if let Some(ref mut tr) = self.terrain_renderer {
            tr.update_meshes(
                &ctx.device,
                &mut self.level,
                self.camera.eye_position().to_array(),
                self.lod_distance,
                MESH_JOB_BUDGET,
                MESH_UPLOAD_BUDGET,
            );
        }

        let surface_texture = match surface.surface.get_current_texture() {
            Ok(t) => t,
            Err(e) => {
                log::error!("Surface error: {e}");
                return;
            }
        };

        let color_view = surface_texture
            .texture
            .create_view(&wgpu::TextureViewDescriptor::default());
        let mut encoder = ctx
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("frame encoder"),
            });

        // 1) Render 3D terrain
        if let Some(ref tr) = self.terrain_renderer {
            let eye = self.camera.eye_position();
            let eye_block = self.level.get_block(
                eye.x.floor() as i32,
                eye.y.floor() as i32,
                eye.z.floor() as i32,
            );
            let is_underwater = eye_block == Block::Water;

            let fog_color = if is_underwater {
                [0.05, 0.1, 0.4, 1.0]
            } else {
                [0.53, 0.81, 0.98, 1.0]
            };
            let fog_start = if is_underwater {
                0.0
            } else {
                self.camera.far * 0.6
            };
            let fog_end = if is_underwater { 10.0 } else { self.camera.far };

            let uniforms = TerrainUniforms {
                view_proj: self.camera.view_proj_matrix(),
                fog_color,
                fog_start,
                fog_end,
                _padding: [0.0; 2],
            };
            tr.render(&mut encoder, &color_view, &ctx.queue, &uniforms);
        }

        // 1.5) Render Selection Highlight
        if let Some(res) = pick_res
            && let Some(ref sr) = self.selection_renderer
        {
            let suniforms = SelectionUniforms {
                view_proj: self.camera.view_proj_matrix(),
                block_pos: [
                    res.block_pos.x as f32,
                    res.block_pos.y as f32,
                    res.block_pos.z as f32,
                ],
                progress: if Some(res.block_pos) == self.break_pos {
                    self.break_progress
                } else {
                    0.0
                },
            };
            sr.render(&mut encoder, &color_view, &ctx.queue, &suniforms);
        }

        let debug_text = if self.show_debug {
            let eye = self.camera.eye_position();
            let fwd = self.camera.forward_look();
            let cx = self.camera.foot_x.div_euclid(16.0) as i32;
            let cz = self.camera.foot_z.div_euclid(16.0) as i32;

            let facing = if self.camera.yaw.rem_euclid(360.0) < 45.0
                || self.camera.yaw.rem_euclid(360.0) >= 315.0
            {
                "South (+Z)"
            } else if self.camera.yaw.rem_euclid(360.0) < 135.0 {
                "West (-X)"
            } else if self.camera.yaw.rem_euclid(360.0) < 225.0 {
                "North (-Z)"
            } else {
                "East (+X)"
            };

            Some(format!(
                "Minecraft PE (Rust)\n\
                 FPS: {:.0}\n\
                 \n\
                 XYZ: {:.3} / {:.3} / {:.3}\n\
                 Block: {} {} {}\n\
                 Chunk: {} {}\n\
                 Facing: {} (yaw {:.1}, pitch {:.1})\n\
                 Looking: ({:.2}, {:.2}, {:.2})\n\
                 \n\
                 Seed: {}",
                self.current_fps,
                eye.x,
                eye.y,
                eye.z,
                eye.x.floor() as i32,
                eye.y.floor() as i32,
                eye.z.floor() as i32,
                cx,
                cz,
                facing,
                self.camera.yaw,
                self.camera.pitch,
                fwd.x,
                fwd.y,
                fwd.z,
                self.level.seed,
            ))
        } else {
            None
        };

        let mut debug_buffer: Option<glyphon::Buffer> = None;
        let mut debug_line_widths: Option<Vec<f32>> = None;
        if let (Some(debug_text), Some(fs)) = (debug_text.as_ref(), self.font_system.as_mut()) {
            let sw = surface.config.width as f32;
            let sh = surface.config.height as f32;
            let ui_scale = (sh / 480.0).max(1.0);
            let font_size = (14.0 * ui_scale).round();
            let line_height = (24.0 * ui_scale).round();
            let white_attr = glyphon::Attrs::new()
                .family(glyphon::Family::Name("Monocraft"))
                .color(glyphon::Color::rgb(255, 255, 255));
            let mut buf = glyphon::Buffer::new(fs, glyphon::Metrics::new(font_size, line_height));
            buf.set_size(fs, Some(sw), Some(sh));
            buf.set_text(
                fs,
                debug_text,
                &white_attr,
                glyphon::Shaping::Advanced,
                None,
            );
            buf.shape_until_scroll(fs, true);
            debug_line_widths = Some(buf.layout_runs().map(|run| run.line_w).collect());
            debug_buffer = Some(buf);
        }

        // 2) GUI Elements (Crosshair, Hotbar, Pause overlay)
        if let Some(ref gui) = self.gui_renderer {
            let mut gui_vertices = Vec::new();
            let ww = surface.config.width as f32;
            let wh = surface.config.height as f32;

            let gui_scale = (wh / 480.0).max(1.0);
            if let Some(ref line_widths) = debug_line_widths {
                let ui_scale = gui_scale;
                let line_height = (24.0 * ui_scale).round();
                let line_gap = (10.0 * ui_scale).round();
                let line_inner = (line_height - line_gap).max(0.0);
                let text_pad = (10.0 * ui_scale).round();
                let pad_x = (10.0 * ui_scale).round();
                let pad_y = (5.0 * ui_scale).round();

                for (line_idx, line_w) in line_widths.iter().enumerate() {
                    if *line_w <= 0.0 {
                        continue;
                    }
                    let line_w = *line_w + pad_x * 2.0;
                    let line_h = line_inner + pad_y * 2.0;
                    let x0 = -1.0 + ((text_pad - pad_x) / ww) * 2.0;
                    let x1 = -1.0 + ((text_pad - pad_x + line_w) / ww) * 2.0;
                    let y_top_px = text_pad + line_idx as f32 * line_height - pad_y;
                    let y1 = 1.0 - (y_top_px / wh) * 2.0;
                    let y0 = 1.0 - ((y_top_px + line_h) / wh) * 2.0;
                    push_gui_quad(
                        &mut gui_vertices,
                        x0,
                        y0,
                        x1,
                        y1,
                        90.0 / 256.0,
                        10.0 / 256.0,
                        91.0 / 256.0,
                        11.0 / 256.0,
                        [0.0, 0.0, 0.0, 0.55],
                    );
                }
            }
            if !self.paused {
                // Crosshair
                let cs = 16.0 * gui_scale / ww;
                let csy = 16.0 * gui_scale / wh;
                push_gui_quad(
                    &mut gui_vertices,
                    -cs,
                    -csy,
                    cs,
                    csy,
                    0.0,
                    0.0,
                    16.0 / 256.0,
                    16.0 / 256.0,
                    [1.0, 1.0, 1.0, 1.0],
                );
            }

            // Hotbar
            let bar_scale = gui_scale * 2.0;
            let hbw = (182.0 * bar_scale) / ww;
            let hbh = (22.0 * bar_scale) / wh;
            let hby = -1.0 + (60.0 * bar_scale) / wh;
            push_gui_quad(
                &mut gui_vertices,
                -hbw,
                hby - hbh,
                hbw,
                hby + hbh,
                0.0,
                0.0,
                182.0 / 256.0,
                22.0 / 256.0,
                [1.0, 1.0, 1.0, 1.0],
            );

            // Selector
            let sw = (24.0 * bar_scale) / ww;
            let sy = (24.0 * bar_scale) / wh;
            let slot_offset = (self.selected_slot as f32 - 4.0) * (40.0 * bar_scale / ww);
            push_gui_quad(
                &mut gui_vertices,
                slot_offset - sw,
                hby - sy,
                slot_offset + sw,
                hby + sy,
                0.0,
                22.0 / 256.0,
                24.0 / 256.0,
                46.0 / 256.0,
                [1.0, 1.0, 1.0, 1.0],
            );

            // Pause overlay
            if self.paused {
                // Semi-transparent dark overlay covering the whole screen
                // Sample a single opaque pixel from the hotbar interior and tint it
                push_gui_quad(
                    &mut gui_vertices,
                    -1.0,
                    -1.0,
                    1.0,
                    1.0,
                    90.0 / 256.0,
                    10.0 / 256.0,
                    91.0 / 256.0,
                    11.0 / 256.0,
                    [0.0, 0.0, 0.0, 0.65],
                );
            }

            gui.draw_quads(&ctx.device, &mut encoder, &color_view, &gui_vertices);
        }

        // 3) Text
        if let (Some(tr), Some(fs), Some(atlas), Some(cache), Some(vp)) = (
            &mut self.text_renderer,
            &mut self.font_system,
            &mut self.text_atlas,
            &mut self.swash_cache,
            &mut self.viewport,
        ) {
            let sw = surface.config.width as f32;
            let sh = surface.config.height as f32;
            let ui_scale = (sh / 480.0).max(1.0);
            let font_size = (14.0 * ui_scale).round();
            let line_height = (21.0 * ui_scale).round();
            let white_attr = glyphon::Attrs::new()
                .family(glyphon::Family::Name("Monocraft"))
                .color(glyphon::Color::rgb(255, 255, 255));

            let mut text_areas: Vec<glyphon::TextArea> = Vec::new();
            let mut buffers: Vec<glyphon::Buffer> = Vec::new();
            let mut has_debug_buffer = false;

            // F3 debug screen
            if let Some(buf) = debug_buffer.take() {
                buffers.push(buf);
                has_debug_buffer = true;
            }

            // Pause screen title
            if self.paused {
                let pause_size = (20.0 * ui_scale).round();
                let pause_line = (26.0 * ui_scale).round();
                let mut buf =
                    glyphon::Buffer::new(fs, glyphon::Metrics::new(pause_size, pause_line));
                buf.set_size(fs, Some(sw), Some(sh));
                buf.set_text(
                    fs,
                    "Game Menu\n\nClick anywhere to resume",
                    &glyphon::Attrs::new()
                        .family(glyphon::Family::Name("Monocraft"))
                        .color(glyphon::Color::rgb(255, 255, 255)),
                    glyphon::Shaping::Advanced,
                    None,
                );
                buf.shape_until_scroll(fs, true);
                buffers.push(buf);
            }

            // Simple FPS counter when debug is off and not paused
            if !self.show_debug && !self.paused {
                let mut buf =
                    glyphon::Buffer::new(fs, glyphon::Metrics::new(font_size, line_height));
                buf.set_size(fs, Some(sw), Some(sh));
                buf.set_text(
                    fs,
                    &format!("FPS: {:.0}", self.current_fps),
                    &white_attr,
                    glyphon::Shaping::Advanced,
                    None,
                );
                buf.shape_until_scroll(fs, true);
                buffers.push(buf);
            }

            // Build text areas from buffers
            let mut buf_index = 0;
            let text_pad = (4.0 * ui_scale).round();
            if has_debug_buffer {
                text_areas.push(glyphon::TextArea {
                    buffer: &buffers[buf_index],
                    left: text_pad,
                    top: text_pad,
                    scale: 1.0,
                    bounds: glyphon::TextBounds {
                        left: 0,
                        top: 0,
                        right: sw as i32,
                        bottom: sh as i32,
                    },
                    default_color: glyphon::Color::rgb(255, 255, 255),
                    custom_glyphs: &[],
                });
                buf_index += 1;
            }

            if self.paused {
                text_areas.push(glyphon::TextArea {
                    buffer: &buffers[buf_index],
                    left: sw / 2.0 - 120.0 * ui_scale,
                    top: sh / 2.0 - 40.0 * ui_scale,
                    scale: 1.0,
                    bounds: glyphon::TextBounds {
                        left: 0,
                        top: 0,
                        right: sw as i32,
                        bottom: sh as i32,
                    },
                    default_color: glyphon::Color::rgb(255, 255, 255),
                    custom_glyphs: &[],
                });
                buf_index += 1;
            }

            if !self.show_debug && !self.paused {
                text_areas.push(glyphon::TextArea {
                    buffer: &buffers[buf_index],
                    left: text_pad,
                    top: text_pad,
                    scale: 1.0,
                    bounds: glyphon::TextBounds {
                        left: 0,
                        top: 0,
                        right: sw as i32,
                        bottom: sh as i32,
                    },
                    default_color: glyphon::Color::rgb(255, 255, 255),
                    custom_glyphs: &[],
                });
            }

            vp.update(
                &ctx.queue,
                glyphon::Resolution {
                    width: surface.config.width,
                    height: surface.config.height,
                },
            );

            tr.prepare(&ctx.device, &ctx.queue, fs, atlas, vp, text_areas, cache)
                .unwrap();

            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("text pass"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &color_view,
                    resolve_target: None,
                    depth_slice: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Load,
                        store: wgpu::StoreOp::Store,
                    },
                })],
                ..Default::default()
            });
            tr.render(atlas, vp, &mut pass).unwrap();
        }

        self.input.reset_deltas();
        ctx.queue.submit([encoder.finish()]);
        surface_texture.present();
        ctx.device.poll(wgpu::PollType::Poll).ok();
    }

    fn handle_block_interactions(
        &mut self,
        pick_res: Option<picking::PickingResult>,
        tick_count: i32,
    ) {
        if self.paused {
            return;
        }

        if self.input.mouse_left {
            if let Some(res) = pick_res {
                let block = self
                    .level
                    .get_block(res.block_pos.x, res.block_pos.y, res.block_pos.z);
                let hardness = block.hardness();

                if hardness >= 0.0 {
                    if Some(res.block_pos) == self.break_pos {
                        // Increase progress
                        // 20.0 ticks per second. break_progress increases per tick.
                        // Instant break if hardness is 0.
                        if hardness == 0.0 {
                            self.break_progress = 1.0;
                        } else if tick_count > 0 {
                            self.break_progress +=
                                (tick_count as f32) / (hardness * TICKS_PER_SECOND);
                        }

                        if self.break_progress >= 1.0 {
                            self.level.set_block(
                                res.block_pos.x,
                                res.block_pos.y,
                                res.block_pos.z,
                                world::block::Block::Air,
                            );
                            self.break_pos = None;
                            self.break_progress = 0.0;
                        }
                    } else {
                        self.break_pos = Some(res.block_pos);
                        self.break_progress = 0.0;
                    }
                } else {
                    self.break_pos = None;
                    self.break_progress = 0.0;
                }
            } else {
                self.break_pos = None;
                self.break_progress = 0.0;
            }
        } else {
            self.break_pos = None;
            self.break_progress = 0.0;

            if self.input.mouse_right_just_pressed
                && let Some(res) = pick_res
            {
                let p = res.block_pos + res.face_normal;

                // Collision check: don't place block if it intersects player
                let pr = 0.3;
                let ph = 1.8;
                let px = self.camera.foot_x;
                let py = self.camera.foot_y;
                let pz = self.camera.foot_z;

                let intersects = px + pr > p.x as f32
                    && px - pr < p.x as f32 + 1.0
                    && py + ph > p.y as f32
                    && py < p.y as f32 + 1.0
                    && pz + pr > p.z as f32
                    && pz - pr < p.z as f32 + 1.0;

                if !intersects {
                    self.level
                        .set_block(p.x, p.y, p.z, world::block::Block::Cobblestone);
                }
            }
        }
    }

    fn tick(&mut self) {
        let forward = (if self.input.forward { 1.0 } else { 0.0 })
            - (if self.input.backward { 1.0 } else { 0.0 });
        let strafe =
            (if self.input.right { 1.0 } else { 0.0 }) - (if self.input.left { 1.0 } else { 0.0 });

        self.camera
            .tick(forward, strafe, self.input.jump, &self.level);
    }
}

#[allow(clippy::too_many_arguments)]
fn push_gui_quad(
    v: &mut Vec<GuiVertex>,
    x0: f32,
    y0: f32,
    x1: f32,
    y1: f32,
    u0: f32,
    v0: f32,
    u1: f32,
    v1: f32,
    color: [f32; 4],
) {
    v.push(GuiVertex {
        position: [x0, y0],
        texcoord: [u0, v0],
        color,
    });
    v.push(GuiVertex {
        position: [x1, y0],
        texcoord: [u1, v0],
        color,
    });
    v.push(GuiVertex {
        position: [x1, y1],
        texcoord: [u1, v1],
        color,
    });
    v.push(GuiVertex {
        position: [x0, y0],
        texcoord: [u0, v0],
        color,
    });
    v.push(GuiVertex {
        position: [x1, y1],
        texcoord: [u1, v1],
        color,
    });
    v.push(GuiVertex {
        position: [x0, y1],
        texcoord: [u0, v1],
        color,
    });
}
