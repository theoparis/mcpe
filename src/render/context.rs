use std::sync::Arc;
use wgpu::{Device, Instance, Queue, Surface, SurfaceConfiguration, TextureFormat};
use winit::window::Window;

/// Manages wgpu state: instance, device, queue, surface.
/// Modeled after the Vello Hybrid example's RenderContext.
pub struct RenderContext {
    pub device: Device,
    pub queue: Queue,
}

pub struct RenderSurface<'w> {
    pub surface: Surface<'w>,
    pub config: SurfaceConfiguration,
}

impl RenderContext {
    /// Create a new render context, finding an adapter compatible with the given window.
    pub async fn new_for_window(window: Arc<Window>) -> (Self, RenderSurface<'static>) {
        let backends = wgpu::Backends::from_env().unwrap_or_default();
        let flags = wgpu::InstanceFlags::from_build_config().with_env();
        let instance = Instance::new(&wgpu::InstanceDescriptor {
            backends,
            flags,
            ..Default::default()
        });

        let surface = instance
            .create_surface(window.clone())
            .expect("Failed to create surface");

        let adapter = wgpu::util::initialize_adapter_from_env_or_default(&instance, Some(&surface))
            .await
            .expect("Failed to find a suitable GPU adapter");

        let (device, queue) = adapter
            .request_device(&wgpu::DeviceDescriptor {
                label: Some("mcpe device"),
                required_features: wgpu::Features::empty(),
                required_limits: adapter.limits(),
                ..Default::default()
            })
            .await
            .expect("Failed to create device");

        let size = window.inner_size();
        let format = TextureFormat::Bgra8Unorm;
        let config = SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format,
            width: size.width.max(1),
            height: size.height.max(1),
            present_mode: wgpu::PresentMode::AutoNoVsync,
            desired_maximum_frame_latency: 2,
            alpha_mode: wgpu::CompositeAlphaMode::Auto,
            view_formats: vec![],
        };
        surface.configure(&device, &config);

        let ctx = Self { device, queue };
        let render_surface = RenderSurface { surface, config };

        (ctx, render_surface)
    }

    pub fn resize_surface(&self, surface: &mut RenderSurface<'_>, width: u32, height: u32) {
        surface.config.width = width.max(1);
        surface.config.height = height.max(1);
        surface.surface.configure(&self.device, &surface.config);
    }
}
