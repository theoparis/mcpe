use std::collections::{HashMap, HashSet};
use std::sync::mpsc;

use bytemuck::{Pod, Zeroable};
use rayon::prelude::*;
use wgpu::util::DeviceExt;

use crate::world::block::Block;
use crate::world::chunk::{CHUNK_HEIGHT, CHUNK_WIDTH, Chunk};
use crate::world::level::Level;

type BlockArray = [u8; CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_WIDTH];
type LodArray = [u8; 4 * 4 * CHUNK_HEIGHT];

/// Wrapper to send raw block pointers across threads.
/// SAFETY: The pointers reference heap-allocated data behind RwLock read guards
/// that are held for the entire duration of the parallel section.
struct BlockPtrSlice(Vec<Option<*const BlockArray>>);
unsafe impl Send for BlockPtrSlice {}
unsafe impl Sync for BlockPtrSlice {}

impl BlockPtrSlice {
    #[inline(always)]
    fn get(&self, idx: usize) -> Option<&BlockArray> {
        self.0[idx].map(|p| unsafe { &*p })
    }
}

struct ChunkMesh {
    vertex_buffer: wgpu::Buffer,
    num_vertices: u32,
    lod: bool,
}

enum MeshJob {
    Full {
        key: (i32, i32),
        generation: u64,
        blocks: Box<BlockArray>,
        n_blocks: Option<Box<BlockArray>>,
        s_blocks: Option<Box<BlockArray>>,
        w_blocks: Option<Box<BlockArray>>,
        e_blocks: Option<Box<BlockArray>>,
    },
    Lod {
        key: (i32, i32),
        generation: u64,
        lod: Box<LodArray>,
    },
}

struct MeshResult {
    key: (i32, i32),
    lod: bool,
    generation: u64,
    vertices: Vec<TerrainVertex>,
}

/// Vertex format for terrain rendering: position + texcoord + tile_origin + tile_size + color.
#[repr(C)]
#[derive(Copy, Clone, Debug, Pod, Zeroable)]
pub struct TerrainVertex {
    pub position: [f32; 3],
    pub texcoord: [f32; 2],
    pub tile_origin: [f32; 2],
    pub tile_size: [f32; 2],
    pub color: [f32; 3],
}

impl TerrainVertex {
    const ATTRS: [wgpu::VertexAttribute; 5] = wgpu::vertex_attr_array![
        0 => Float32x3,
        1 => Float32x2,
        2 => Float32x2,
        3 => Float32x2,
        4 => Float32x3,
    ];

    pub fn layout() -> wgpu::VertexBufferLayout<'static> {
        wgpu::VertexBufferLayout {
            array_stride: std::mem::size_of::<TerrainVertex>() as wgpu::BufferAddress,
            step_mode: wgpu::VertexStepMode::Vertex,
            attributes: &Self::ATTRS,
        }
    }
}

/// Uniform buffer holding the view-projection matrix.
#[repr(C)]
#[derive(Copy, Clone, Debug, Pod, Zeroable)]
pub struct TerrainUniforms {
    pub view_proj: [[f32; 4]; 4],
    pub fog_color: [f32; 4],
    pub fog_start: f32,
    pub fog_end: f32,
    pub _padding: [f32; 2],
}

/// The terrain atlas size (terrain.png is a 16×16 grid of tiles).
const ATLAS_TILES: u32 = 16;

/// Get UV coordinates for a tile index in the terrain atlas.
fn tile_uv(tile_index: u32) -> (f32, f32, f32, f32) {
    let tx = (tile_index % ATLAS_TILES) as f32;
    let ty = (tile_index / ATLAS_TILES) as f32;
    let inv = 1.0 / ATLAS_TILES as f32;
    (tx * inv, ty * inv, (tx + 1.0) * inv, (ty + 1.0) * inv)
}

/// Terrain renderer: builds meshes from chunks and renders with wgpu.
pub struct TerrainRenderer {
    pipeline: wgpu::RenderPipeline,
    uniform_buffer: wgpu::Buffer,
    uniform_bind_group: wgpu::BindGroup,
    texture_bind_group: wgpu::BindGroup,
    depth_texture: wgpu::TextureView,
    depth_format: wgpu::TextureFormat,
    chunk_meshes: HashMap<(i32, i32), ChunkMesh>,
    mesh_result_tx: mpsc::Sender<MeshResult>,
    mesh_result_rx: mpsc::Receiver<MeshResult>,
    in_flight: HashSet<(i32, i32)>,
    chunk_generations: HashMap<(i32, i32), u64>,
    generation_counter: u64,
}

impl TerrainRenderer {
    pub fn new(
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        surface_format: wgpu::TextureFormat,
        width: u32,
        height: u32,
        terrain_rgba: &[u8],
        terrain_width: u32,
        terrain_height: u32,
    ) -> Self {
        let depth_format = wgpu::TextureFormat::Depth32Float;

        // -- Terrain texture --
        let terrain_texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("terrain atlas"),
            size: wgpu::Extent3d {
                width: terrain_width,
                height: terrain_height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8UnormSrgb,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });
        queue.write_texture(
            wgpu::TexelCopyTextureInfo {
                texture: &terrain_texture,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            terrain_rgba,
            wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(4 * terrain_width),
                rows_per_image: Some(terrain_height),
            },
            wgpu::Extent3d {
                width: terrain_width,
                height: terrain_height,
                depth_or_array_layers: 1,
            },
        );
        let terrain_view = terrain_texture.create_view(&wgpu::TextureViewDescriptor::default());
        let terrain_sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("terrain sampler"),
            mag_filter: wgpu::FilterMode::Nearest,
            min_filter: wgpu::FilterMode::Nearest,
            mipmap_filter: wgpu::MipmapFilterMode::Nearest,
            ..Default::default()
        });

        // -- Bind group layouts --
        let uniform_bgl = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("uniform bgl"),
            entries: &[wgpu::BindGroupLayoutEntry {
                binding: 0,
                visibility: wgpu::ShaderStages::VERTEX | wgpu::ShaderStages::FRAGMENT,
                ty: wgpu::BindingType::Buffer {
                    ty: wgpu::BufferBindingType::Uniform,
                    has_dynamic_offset: false,
                    min_binding_size: None,
                },
                count: None,
            }],
        });

        let texture_bgl = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("texture bgl"),
            entries: &[
                wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture {
                        sample_type: wgpu::TextureSampleType::Float { filterable: true },
                        view_dimension: wgpu::TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 1,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                    count: None,
                },
            ],
        });

        // -- Uniform buffer --
        let uniform_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("terrain uniforms"),
            size: std::mem::size_of::<TerrainUniforms>() as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });

        let uniform_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("uniform bg"),
            layout: &uniform_bgl,
            entries: &[wgpu::BindGroupEntry {
                binding: 0,
                resource: uniform_buffer.as_entire_binding(),
            }],
        });

        let texture_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("texture bg"),
            layout: &texture_bgl,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&terrain_view),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::Sampler(&terrain_sampler),
                },
            ],
        });

        // -- Shader --
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("terrain shader"),
            source: wgpu::ShaderSource::Wgsl(include_str!("terrain.wgsl").into()),
        });

        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("terrain pipeline layout"),
            bind_group_layouts: &[&uniform_bgl, &texture_bgl],
            immediate_size: 0,
        });

        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("terrain pipeline"),
            layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: Some("vs_main"),
                buffers: &[TerrainVertex::layout()],
                compilation_options: Default::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: Some("fs_main"),
                targets: &[Some(wgpu::ColorTargetState {
                    format: surface_format,
                    blend: Some(wgpu::BlendState::ALPHA_BLENDING),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
                compilation_options: Default::default(),
            }),
            primitive: wgpu::PrimitiveState {
                topology: wgpu::PrimitiveTopology::TriangleList,
                front_face: wgpu::FrontFace::Cw,
                cull_mode: None,
                ..Default::default()
            },
            depth_stencil: Some(wgpu::DepthStencilState {
                format: depth_format,
                depth_write_enabled: true,
                depth_compare: wgpu::CompareFunction::LessEqual,
                stencil: Default::default(),
                bias: Default::default(),
            }),
            multisample: Default::default(),
            multiview_mask: None,
            cache: None,
        });

        let depth_texture = Self::create_depth_texture(device, depth_format, width, height);
        let (mesh_result_tx, mesh_result_rx) = mpsc::channel();

        Self {
            pipeline,
            uniform_buffer,
            uniform_bind_group,
            texture_bind_group,
            depth_texture,
            depth_format,
            chunk_meshes: HashMap::new(),
            mesh_result_tx,
            mesh_result_rx,
            in_flight: HashSet::new(),
            chunk_generations: HashMap::new(),
            generation_counter: 0,
        }
    }

    fn create_depth_texture(
        device: &wgpu::Device,
        format: wgpu::TextureFormat,
        width: u32,
        height: u32,
    ) -> wgpu::TextureView {
        let texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("depth texture"),
            size: wgpu::Extent3d {
                width: width.max(1),
                height: height.max(1),
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING,
            view_formats: &[],
        });
        texture.create_view(&wgpu::TextureViewDescriptor::default())
    }

    pub fn resize(&mut self, device: &wgpu::Device, width: u32, height: u32) {
        self.depth_texture = Self::create_depth_texture(device, self.depth_format, width, height);
    }

    /// Rebuild all chunk meshes at once (synchronous).
    /// Uses LoD for chunks further than lod_dist.
    pub fn rebuild_mesh(
        &mut self,
        device: &wgpu::Device,
        level: &Level,
        cam_pos: [f32; 3],
        lod_dist: f32,
    ) {
        let lod_dist_sq = lod_dist * lod_dist;

        let chunks: Vec<&Chunk> = level.chunks().collect();

        // Classify chunks: full-detail vs LOD-only. Only decompress full-detail ones.
        for &chunk in &chunks {
            let dx = (chunk.cx as f32 + 0.5) * CHUNK_WIDTH as f32 - cam_pos[0];
            let dz = (chunk.cz as f32 + 0.5) * CHUNK_WIDTH as f32 - cam_pos[2];
            if dx * dx + dz * dz <= lod_dist_sq || chunk.lod_blocks.is_none() {
                chunk.decompress();
            }
        }

        // Lock all decompressed chunks once, extract raw pointers before parallel section.
        // This eliminates all RwLock traffic from the hot meshing loop.
        struct LockedChunk<'a> {
            chunk: &'a Chunk,
            blocks_guard: std::sync::RwLockReadGuard<'a, Option<Box<BlockArray>>>,
        }
        let locked: Vec<LockedChunk> = chunks
            .iter()
            .map(|&chunk| LockedChunk {
                blocks_guard: chunk.blocks.read().unwrap(),
                chunk,
            })
            .collect();

        // Build a map from (cx,cz) -> index for O(1) neighbor lookup.
        let index_map: HashMap<(i32, i32), usize> = locked
            .iter()
            .enumerate()
            .map(|(i, lc)| ((lc.chunk.cx, lc.chunk.cz), i))
            .collect();

        // Pre-extract raw block slice pointers indexed same as `locked`.
        let block_ptrs: Vec<Option<*const BlockArray>> = locked
            .iter()
            .map(|lc| lc.blocks_guard.as_ref().map(|b| &**b as *const BlockArray))
            .collect();

        let ptrs = BlockPtrSlice(block_ptrs);

        let mesh_builds: Vec<((i32, i32), bool, Vec<TerrainVertex>)> = locked
            .par_iter()
            .enumerate()
            .map(|(ci, lc)| {
                let chunk = lc.chunk;
                let mut chunk_vertices = Vec::new();

                let dx = (chunk.cx as f32 + 0.5) * CHUNK_WIDTH as f32 - cam_pos[0];
                let dz = (chunk.cz as f32 + 0.5) * CHUNK_WIDTH as f32 - cam_pos[2];
                let dist_sq = dx * dx + dz * dz;

                let use_lod = dist_sq > lod_dist_sq && chunk.lod_blocks.is_some();
                if use_lod {
                    if let Some(ref lod) = chunk.lod_blocks {
                        Self::push_lod_vertices_into(&mut chunk_vertices, chunk.cx, chunk.cz, lod);
                    }
                    return ((chunk.cx, chunk.cz), true, chunk_vertices);
                }

                let Some(blocks) = ptrs.get(ci) else {
                    return ((chunk.cx, chunk.cz), false, chunk_vertices);
                };

                // Resolve neighbor block slices (zero-cost after this).
                let get_neighbor = |dcx: i32, dcz: i32| -> Option<&BlockArray> {
                    let idx = *index_map.get(&(chunk.cx + dcx, chunk.cz + dcz))?;
                    ptrs.get(idx)
                };
                let n_blocks = get_neighbor(0, -1);
                let s_blocks = get_neighbor(0, 1);
                let w_blocks = get_neighbor(-1, 0);
                let e_blocks = get_neighbor(1, 0);

                let mesh = Self::build_full_mesh_from_blocks(
                    chunk.cx, chunk.cz, blocks, n_blocks, s_blocks, w_blocks, e_blocks,
                );
                ((chunk.cx, chunk.cz), false, mesh)
            })
            .collect();

        self.chunk_meshes.clear();
        for (key, lod, vertices) in mesh_builds {
            if vertices.is_empty() {
                continue;
            }
            let vertex_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
                label: Some("terrain vb"),
                contents: bytemuck::cast_slice(&vertices),
                usage: wgpu::BufferUsages::VERTEX,
            });
            self.chunk_meshes.insert(
                key,
                ChunkMesh {
                    vertex_buffer,
                    num_vertices: vertices.len() as u32,
                    lod,
                },
            );
        }

        self.in_flight.clear();
        self.chunk_generations.clear();
    }

    /// Incrementally update chunk meshes with background jobs.
    pub fn update_meshes(
        &mut self,
        device: &wgpu::Device,
        level: &mut Level,
        cam_pos: [f32; 3],
        lod_dist: f32,
        job_budget: usize,
        upload_budget: usize,
    ) {
        self.drain_mesh_results(device, upload_budget);

        if job_budget == 0 {
            return;
        }

        let mut scheduled = Vec::new();
        let mut remaining = job_budget;

        for (&(cx, cz), chunk) in level.chunks_iter() {
            if !chunk.dirty {
                continue;
            }

            let dx = (cx as f32 + 0.5) * CHUNK_WIDTH as f32 - cam_pos[0];
            let dz = (cz as f32 + 0.5) * CHUNK_WIDTH as f32 - cam_pos[2];
            let dist_sq = dx * dx + dz * dz;

            let current_lod = self.chunk_meshes.get(&(cx, cz)).map(|m| m.lod);
            let desired_lod = Self::desired_lod(current_lod, dist_sq, lod_dist);
            let needs_mesh =
                chunk.dirty || current_lod.is_none() || current_lod != Some(desired_lod);

            if needs_mesh && !self.in_flight.contains(&(cx, cz)) {
                scheduled.push((cx, cz, desired_lod));
                remaining -= 1;
                if remaining == 0 {
                    break;
                }
            }
        }

        if remaining > 0 {
            for (&(cx, cz), chunk) in level.chunks_iter() {
                if chunk.dirty {
                    continue;
                }

                let dx = (cx as f32 + 0.5) * CHUNK_WIDTH as f32 - cam_pos[0];
                let dz = (cz as f32 + 0.5) * CHUNK_WIDTH as f32 - cam_pos[2];
                let dist_sq = dx * dx + dz * dz;

                let current_lod = self.chunk_meshes.get(&(cx, cz)).map(|m| m.lod);
                let desired_lod = Self::desired_lod(current_lod, dist_sq, lod_dist);
                let needs_mesh =
                    chunk.dirty || current_lod.is_none() || current_lod != Some(desired_lod);

                if needs_mesh && !self.in_flight.contains(&(cx, cz)) {
                    scheduled.push((cx, cz, desired_lod));
                    remaining -= 1;
                    if remaining == 0 {
                        break;
                    }
                }
            }
        }

        for (cx, cz, desired_lod) in scheduled {
            let key = (cx, cz);
            let Some(chunk) = level.get_chunk(cx, cz) else {
                continue;
            };

            let job = if desired_lod {
                if let Some(lod) = Self::copy_lod(chunk) {
                    let generation = self.next_generation(key);
                    Some(MeshJob::Lod {
                        key,
                        generation,
                        lod,
                    })
                } else if let Some(blocks) = Self::copy_blocks(chunk) {
                    let n_blocks = level
                        .get_chunk(cx, cz - 1)
                        .and_then(|c| Self::copy_blocks(c));
                    let s_blocks = level
                        .get_chunk(cx, cz + 1)
                        .and_then(|c| Self::copy_blocks(c));
                    let w_blocks = level
                        .get_chunk(cx - 1, cz)
                        .and_then(|c| Self::copy_blocks(c));
                    let e_blocks = level
                        .get_chunk(cx + 1, cz)
                        .and_then(|c| Self::copy_blocks(c));
                    let generation = self.next_generation(key);
                    Some(MeshJob::Full {
                        key,
                        generation,
                        blocks,
                        n_blocks,
                        s_blocks,
                        w_blocks,
                        e_blocks,
                    })
                } else {
                    None
                }
            } else {
                let Some(blocks) = Self::copy_blocks(chunk) else {
                    continue;
                };
                let n_blocks = level
                    .get_chunk(cx, cz - 1)
                    .and_then(|c| Self::copy_blocks(c));
                let s_blocks = level
                    .get_chunk(cx, cz + 1)
                    .and_then(|c| Self::copy_blocks(c));
                let w_blocks = level
                    .get_chunk(cx - 1, cz)
                    .and_then(|c| Self::copy_blocks(c));
                let e_blocks = level
                    .get_chunk(cx + 1, cz)
                    .and_then(|c| Self::copy_blocks(c));
                let generation = self.next_generation(key);
                Some(MeshJob::Full {
                    key,
                    generation,
                    blocks,
                    n_blocks,
                    s_blocks,
                    w_blocks,
                    e_blocks,
                })
            };

            let Some(job) = job else {
                continue;
            };

            self.in_flight.insert(key);
            if let Some(chunk) = level.get_chunk_mut(cx, cz) {
                chunk.dirty = false;
            }

            let tx = self.mesh_result_tx.clone();
            Self::spawn_mesh_job(job, tx);
        }
    }

    fn spawn_mesh_job(job: MeshJob, tx: mpsc::Sender<MeshResult>) {
        rayon::spawn(move || {
            let (key, lod, generation, vertices) = match job {
                MeshJob::Full {
                    key,
                    generation,
                    blocks,
                    n_blocks,
                    s_blocks,
                    w_blocks,
                    e_blocks,
                } => {
                    let vertices = TerrainRenderer::build_full_mesh_from_blocks(
                        key.0,
                        key.1,
                        &blocks,
                        n_blocks.as_deref(),
                        s_blocks.as_deref(),
                        w_blocks.as_deref(),
                        e_blocks.as_deref(),
                    );
                    (key, false, generation, vertices)
                }
                MeshJob::Lod {
                    key,
                    generation,
                    lod,
                } => {
                    let vertices = TerrainRenderer::build_lod_mesh(key.0, key.1, &lod);
                    (key, true, generation, vertices)
                }
            };
            let _ = tx.send(MeshResult {
                key,
                lod,
                generation,
                vertices,
            });
        });
    }

    fn drain_mesh_results(&mut self, device: &wgpu::Device, upload_budget: usize) {
        for _ in 0..upload_budget {
            match self.mesh_result_rx.try_recv() {
                Ok(result) => {
                    self.in_flight.remove(&result.key);

                    if self.chunk_generations.get(&result.key) != Some(&result.generation) {
                        continue;
                    }

                    if result.vertices.is_empty() {
                        self.chunk_meshes.remove(&result.key);
                        continue;
                    }

                    let vertex_buffer =
                        device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
                            label: Some("terrain vb"),
                            contents: bytemuck::cast_slice(&result.vertices),
                            usage: wgpu::BufferUsages::VERTEX,
                        });

                    self.chunk_meshes.insert(
                        result.key,
                        ChunkMesh {
                            vertex_buffer,
                            num_vertices: result.vertices.len() as u32,
                            lod: result.lod,
                        },
                    );
                }
                Err(mpsc::TryRecvError::Empty) => break,
                Err(mpsc::TryRecvError::Disconnected) => break,
            }
        }
    }

    fn next_generation(&mut self, key: (i32, i32)) -> u64 {
        self.generation_counter = self.generation_counter.wrapping_add(1);
        self.chunk_generations.insert(key, self.generation_counter);
        self.generation_counter
    }

    fn copy_blocks(chunk: &Chunk) -> Option<Box<BlockArray>> {
        chunk.decompress();
        let blocks = chunk.blocks.read().ok()?;
        let Some(src) = blocks.as_ref() else {
            return None;
        };
        let mut copy = Box::new([0u8; CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_WIDTH]);
        copy[..].copy_from_slice(&src[..]);
        Some(copy)
    }

    fn copy_lod(chunk: &Chunk) -> Option<Box<LodArray>> {
        let Some(src) = chunk.lod_blocks.as_ref() else {
            return None;
        };
        let mut copy = Box::new([0u8; 4 * 4 * CHUNK_HEIGHT]);
        copy[..].copy_from_slice(&src[..]);
        Some(copy)
    }

    fn desired_lod(current: Option<bool>, dist_sq: f32, lod_dist: f32) -> bool {
        let lod_in = lod_dist * 0.9;
        let lod_out = lod_dist * 1.1;
        let lod_in_sq = lod_in * lod_in;
        let lod_out_sq = lod_out * lod_out;
        let lod_dist_sq = lod_dist * lod_dist;

        match current {
            Some(true) => dist_sq > lod_in_sq,
            Some(false) => dist_sq > lod_out_sq,
            None => dist_sq > lod_dist_sq,
        }
    }

    fn build_full_mesh_from_blocks(
        cx: i32,
        cz: i32,
        blocks: &BlockArray,
        n_blocks: Option<&BlockArray>,
        s_blocks: Option<&BlockArray>,
        w_blocks: Option<&BlockArray>,
        e_blocks: Option<&BlockArray>,
    ) -> Vec<TerrainVertex> {
        let mut chunk_vertices = Vec::new();
        let ox = (cx * CHUNK_WIDTH as i32) as f32;
        let oz = (cz * CHUNK_WIDTH as i32) as f32;

        #[inline(always)]
        fn neighbor_block(
            blocks: &BlockArray,
            x: i32,
            y: i32,
            z: i32,
            n: Option<&BlockArray>,
            s: Option<&BlockArray>,
            w: Option<&BlockArray>,
            e: Option<&BlockArray>,
        ) -> Block {
            if y < 0 || y >= CHUNK_HEIGHT as i32 {
                return Block::Air;
            }
            let cw = CHUNK_WIDTH as i32;
            if x >= 0 && x < cw && z >= 0 && z < cw {
                return Block::from_id(blocks[Chunk::index(x as usize, y as usize, z as usize)]);
            }
            let yu = y as usize;
            if z < 0 {
                return match n {
                    Some(nb) => Block::from_id(nb[Chunk::index(x as usize, yu, (z + cw) as usize)]),
                    None => Block::Air,
                };
            }
            if z >= cw {
                return match s {
                    Some(nb) => Block::from_id(nb[Chunk::index(x as usize, yu, (z - cw) as usize)]),
                    None => Block::Air,
                };
            }
            if x < 0 {
                return match w {
                    Some(nb) => Block::from_id(nb[Chunk::index((x + cw) as usize, yu, z as usize)]),
                    None => Block::Air,
                };
            }
            if x >= cw {
                return match e {
                    Some(nb) => Block::from_id(nb[Chunk::index((x - cw) as usize, yu, z as usize)]),
                    None => Block::Air,
                };
            }
            Block::Air
        }

        for y in 0..CHUNK_HEIGHT {
            for z in 0..CHUNK_WIDTH {
                for x in 0..CHUNK_WIDTH {
                    let block_id = blocks[Chunk::index(x, y, z)];
                    if block_id == 0 {
                        continue;
                    }
                    let block = Block::from_id(block_id);

                    let wx = ox + x as f32;
                    let wy = y as f32;
                    let wz = oz + z as f32;

                    let (top_tex, side_tex, bottom_tex) = block.texture_indices();
                    let grass_tint = [0.44, 0.74, 0.31];
                    let white = [1.0_f32, 1.0, 1.0];
                    let top_tint = match block {
                        Block::Grass | Block::OakLeaves => grass_tint,
                        _ => white,
                    };
                    let side_tint = match block {
                        Block::OakLeaves => grass_tint,
                        _ => white,
                    };

                    let ix = x as i32;
                    let iy = y as i32;
                    let iz = z as i32;

                    let top_neighbor = neighbor_block(
                        blocks,
                        ix,
                        iy + 1,
                        iz,
                        n_blocks,
                        s_blocks,
                        w_blocks,
                        e_blocks,
                    );
                    if (top_neighbor.is_transparent()
                        && (top_neighbor != block || block == Block::Glass))
                        || iy + 1 >= CHUNK_HEIGHT as i32
                    {
                        let (u0, v0, u1, v1) = tile_uv(top_tex);
                        let c = apply_light(top_tint, 1.0);
                        push_quad(
                            &mut chunk_vertices,
                            [wx, wy + 1.0, wz],
                            [wx + 1.0, wy + 1.0, wz],
                            [wx + 1.0, wy + 1.0, wz + 1.0],
                            [wx, wy + 1.0, wz + 1.0],
                            u0,
                            v0,
                            u1,
                            v1,
                            c,
                        );
                    }

                    let bottom_neighbor = neighbor_block(
                        blocks,
                        ix,
                        iy - 1,
                        iz,
                        n_blocks,
                        s_blocks,
                        w_blocks,
                        e_blocks,
                    );
                    if iy > 0
                        && bottom_neighbor.is_transparent()
                        && (bottom_neighbor != block || block == Block::Glass)
                    {
                        let (u0, v0, u1, v1) = tile_uv(bottom_tex);
                        let c = apply_light(white, 0.5);
                        push_quad(
                            &mut chunk_vertices,
                            [wx, wy, wz + 1.0],
                            [wx + 1.0, wy, wz + 1.0],
                            [wx + 1.0, wy, wz],
                            [wx, wy, wz],
                            u0,
                            v0,
                            u1,
                            v1,
                            c,
                        );
                    }

                    let north_neighbor = neighbor_block(
                        blocks,
                        ix,
                        iy,
                        iz - 1,
                        n_blocks,
                        s_blocks,
                        w_blocks,
                        e_blocks,
                    );
                    if north_neighbor.is_transparent()
                        && (north_neighbor != block || block == Block::Glass)
                    {
                        let (u0, v0, u1, v1) = tile_uv(side_tex);
                        let c = apply_light(side_tint, 0.7);
                        push_quad(
                            &mut chunk_vertices,
                            [wx + 1.0, wy + 1.0, wz],
                            [wx, wy + 1.0, wz],
                            [wx, wy, wz],
                            [wx + 1.0, wy, wz],
                            u0,
                            v0,
                            u1,
                            v1,
                            c,
                        );
                    }

                    let south_neighbor = neighbor_block(
                        blocks,
                        ix,
                        iy,
                        iz + 1,
                        n_blocks,
                        s_blocks,
                        w_blocks,
                        e_blocks,
                    );
                    if south_neighbor.is_transparent()
                        && (south_neighbor != block || block == Block::Glass)
                    {
                        let (u0, v0, u1, v1) = tile_uv(side_tex);
                        let c = apply_light(side_tint, 0.7);
                        push_quad(
                            &mut chunk_vertices,
                            [wx, wy + 1.0, wz + 1.0],
                            [wx + 1.0, wy + 1.0, wz + 1.0],
                            [wx + 1.0, wy, wz + 1.0],
                            [wx, wy, wz + 1.0],
                            u0,
                            v0,
                            u1,
                            v1,
                            c,
                        );
                    }

                    let west_neighbor = neighbor_block(
                        blocks,
                        ix - 1,
                        iy,
                        iz,
                        n_blocks,
                        s_blocks,
                        w_blocks,
                        e_blocks,
                    );
                    if west_neighbor.is_transparent()
                        && (west_neighbor != block || block == Block::Glass)
                    {
                        let (u0, v0, u1, v1) = tile_uv(side_tex);
                        let c = apply_light(side_tint, 0.6);
                        push_quad(
                            &mut chunk_vertices,
                            [wx, wy + 1.0, wz],
                            [wx, wy + 1.0, wz + 1.0],
                            [wx, wy, wz + 1.0],
                            [wx, wy, wz],
                            u0,
                            v0,
                            u1,
                            v1,
                            c,
                        );
                    }

                    let east_neighbor = neighbor_block(
                        blocks,
                        ix + 1,
                        iy,
                        iz,
                        n_blocks,
                        s_blocks,
                        w_blocks,
                        e_blocks,
                    );
                    if east_neighbor.is_transparent()
                        && (east_neighbor != block || block == Block::Glass)
                    {
                        let (u0, v0, u1, v1) = tile_uv(side_tex);
                        let c = apply_light(side_tint, 0.8);
                        push_quad(
                            &mut chunk_vertices,
                            [wx + 1.0, wy + 1.0, wz + 1.0],
                            [wx + 1.0, wy + 1.0, wz],
                            [wx + 1.0, wy, wz],
                            [wx + 1.0, wy, wz + 1.0],
                            u0,
                            v0,
                            u1,
                            v1,
                            c,
                        );
                    }
                }
            }
        }

        chunk_vertices
    }

    fn build_lod_mesh(cx: i32, cz: i32, lod: &LodArray) -> Vec<TerrainVertex> {
        let mut vertices = Vec::new();
        Self::push_lod_vertices_into(&mut vertices, cx, cz, lod);
        vertices
    }

    fn push_lod_vertices_into(vertices: &mut Vec<TerrainVertex>, cx: i32, cz: i32, lod: &LodArray) {
        let ox = (cx * CHUNK_WIDTH as i32) as f32;
        let oz = (cz * CHUNK_WIDTH as i32) as f32;

        for y in 0..CHUNK_HEIGHT {
            for lz in 0..4 {
                for lx in 0..4 {
                    let idx = (y * 4 * 4) + (lz * 4) + lx;
                    let block_id = lod[idx];
                    if block_id == 0 {
                        continue;
                    }
                    let block = Block::from_id(block_id);

                    let wx = ox + (lx * 4) as f32;
                    let wy = y as f32;
                    let wz = oz + (lz * 4) as f32;

                    let (top_tex, side_tex, bottom_tex) = block.texture_indices();
                    let white = [1.0, 1.0, 1.0];
                    let grass_tint = [0.44, 0.74, 0.31];
                    let top_tint = match block {
                        Block::Grass | Block::OakLeaves => grass_tint,
                        _ => white,
                    };
                    let side_tint = match block {
                        Block::OakLeaves => grass_tint,
                        _ => white,
                    };

                    // 1. Top Face
                    let mut render_top = true;
                    if y + 1 < CHUNK_HEIGHT {
                        let above_id = lod[((y + 1) * 4 * 4) + (lz * 4) + lx];
                        if above_id != 0 && !Block::from_id(above_id).is_transparent() {
                            render_top = false;
                        }
                    }
                    if render_top {
                        let (u0, v0, u1, v1) = tile_uv(top_tex);
                        let tile_w = u1 - u0;
                        let tile_h = v1 - v0;
                        push_quad(
                            vertices,
                            [wx, wy + 1.0, wz],
                            [wx + 4.0, wy + 1.0, wz],
                            [wx + 4.0, wy + 1.0, wz + 4.0],
                            [wx, wy + 1.0, wz + 4.0],
                            u0,
                            v0,
                            u0 + tile_w * 4.0,
                            v0 + tile_h * 4.0,
                            apply_light(top_tint, 1.0),
                        );
                    }

                    // 2. Bottom Face
                    let mut render_bottom = true;
                    if y > 0 {
                        let below_id = lod[((y - 1) * 4 * 4) + (lz * 4) + lx];
                        if below_id != 0 && !Block::from_id(below_id).is_transparent() {
                            render_bottom = false;
                        }
                    }
                    if render_bottom {
                        let (u0, v0, u1, v1) = tile_uv(bottom_tex);
                        let tile_w = u1 - u0;
                        let tile_h = v1 - v0;
                        push_quad(
                            vertices,
                            [wx, wy, wz + 4.0],
                            [wx + 4.0, wy, wz + 4.0],
                            [wx + 4.0, wy, wz],
                            [wx, wy, wz],
                            u0,
                            v0,
                            u0 + tile_w * 4.0,
                            v0 + tile_h * 4.0,
                            apply_light(white, 0.5),
                        );
                    }

                    // 3. North Face (-Z)
                    let mut render_north = true;
                    if lz > 0 {
                        let n_id = lod[(y * 4 * 4) + ((lz - 1) * 4) + lx];
                        if n_id != 0 && !Block::from_id(n_id).is_transparent() {
                            render_north = false;
                        }
                    }
                    if render_north {
                        let (u0, v0, u1, v1) = tile_uv(side_tex);
                        let tile_w = u1 - u0;
                        push_quad(
                            vertices,
                            [wx + 4.0, wy + 1.0, wz],
                            [wx, wy + 1.0, wz],
                            [wx, wy, wz],
                            [wx + 4.0, wy, wz],
                            u0,
                            v0,
                            u0 + tile_w * 4.0,
                            v1,
                            apply_light(side_tint, 0.7),
                        );
                    }

                    // 4. South Face (+Z)
                    let mut render_south = true;
                    if lz < 3 {
                        let s_id = lod[(y * 4 * 4) + ((lz + 1) * 4) + lx];
                        if s_id != 0 && !Block::from_id(s_id).is_transparent() {
                            render_south = false;
                        }
                    }
                    if render_south {
                        let (u0, v0, u1, v1) = tile_uv(side_tex);
                        let tile_w = u1 - u0;
                        push_quad(
                            vertices,
                            [wx, wy + 1.0, wz + 4.0],
                            [wx + 4.0, wy + 1.0, wz + 4.0],
                            [wx + 4.0, wy, wz + 4.0],
                            [wx, wy, wz + 4.0],
                            u0,
                            v0,
                            u0 + tile_w * 4.0,
                            v1,
                            apply_light(side_tint, 0.7),
                        );
                    }

                    // 5. West Face (-X)
                    let mut render_west = true;
                    if lx > 0 {
                        let w_id = lod[(y * 4 * 4) + (lz * 4) + (lx - 1)];
                        if w_id != 0 && !Block::from_id(w_id).is_transparent() {
                            render_west = false;
                        }
                    }
                    if render_west {
                        let (u0, v0, u1, v1) = tile_uv(side_tex);
                        let tile_w = u1 - u0;
                        push_quad(
                            vertices,
                            [wx, wy + 1.0, wz],
                            [wx, wy + 1.0, wz + 4.0],
                            [wx, wy, wz + 4.0],
                            [wx, wy, wz],
                            u0,
                            v0,
                            u0 + tile_w * 4.0,
                            v1,
                            apply_light(side_tint, 0.6),
                        );
                    }

                    // 6. East Face (+X)
                    let mut render_east = true;
                    if lx < 3 {
                        let e_id = lod[(y * 4 * 4) + (lz * 4) + (lx + 1)];
                        if e_id != 0 && !Block::from_id(e_id).is_transparent() {
                            render_east = false;
                        }
                    }
                    if render_east {
                        let (u0, v0, u1, v1) = tile_uv(side_tex);
                        let tile_w = u1 - u0;
                        push_quad(
                            vertices,
                            [wx + 4.0, wy + 1.0, wz + 4.0],
                            [wx + 4.0, wy + 1.0, wz],
                            [wx + 4.0, wy, wz],
                            [wx + 4.0, wy, wz + 4.0],
                            u0,
                            v0,
                            u0 + tile_w * 4.0,
                            v1,
                            apply_light(side_tint, 0.8),
                        );
                    }
                }
            }
        }
    }

    /// Render the terrain.
    pub fn render(
        &self,
        encoder: &mut wgpu::CommandEncoder,
        color_view: &wgpu::TextureView,
        queue: &wgpu::Queue,
        uniforms: &TerrainUniforms,
    ) {
        queue.write_buffer(&self.uniform_buffer, 0, bytemuck::bytes_of(uniforms));

        let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("terrain pass"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: color_view,
                resolve_target: None,
                depth_slice: None,
                ops: wgpu::Operations {
                    load: wgpu::LoadOp::Clear(wgpu::Color {
                        r: uniforms.fog_color[0] as f64,
                        g: uniforms.fog_color[1] as f64,
                        b: uniforms.fog_color[2] as f64,
                        a: uniforms.fog_color[3] as f64,
                    }),
                    store: wgpu::StoreOp::Store,
                },
            })],
            depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                view: &self.depth_texture,
                depth_ops: Some(wgpu::Operations {
                    load: wgpu::LoadOp::Clear(1.0),
                    store: wgpu::StoreOp::Store,
                }),
                stencil_ops: None,
            }),
            ..Default::default()
        });

        pass.set_pipeline(&self.pipeline);
        pass.set_bind_group(0, &self.uniform_bind_group, &[]);
        pass.set_bind_group(1, &self.texture_bind_group, &[]);

        for mesh in self.chunk_meshes.values() {
            pass.set_vertex_buffer(0, mesh.vertex_buffer.slice(..));
            pass.draw(0..mesh.num_vertices, 0..1);
        }
    }
}

/// Multiply tint color by a light factor.
fn apply_light(tint: [f32; 3], light: f32) -> [f32; 3] {
    [tint[0] * light, tint[1] * light, tint[2] * light]
}

/// Push a quad as two triangles.
#[allow(clippy::too_many_arguments)]
fn push_quad(
    vertices: &mut Vec<TerrainVertex>,
    p0: [f32; 3],
    p1: [f32; 3],
    p2: [f32; 3],
    p3: [f32; 3],
    u0: f32,
    v0: f32,
    u1: f32,
    v1: f32,
    color: [f32; 3],
) {
    let inv = 1.0 / ATLAS_TILES as f32;
    let tile_origin = [
        (u0 * ATLAS_TILES as f32).floor() * inv,
        (v0 * ATLAS_TILES as f32).floor() * inv,
    ];
    let tile_size = [inv, inv];

    vertices.push(TerrainVertex {
        position: p0,
        texcoord: [u0, v0],
        tile_origin,
        tile_size,
        color,
    });
    vertices.push(TerrainVertex {
        position: p1,
        texcoord: [u1, v0],
        tile_origin,
        tile_size,
        color,
    });
    vertices.push(TerrainVertex {
        position: p2,
        texcoord: [u1, v1],
        tile_origin,
        tile_size,
        color,
    });
    vertices.push(TerrainVertex {
        position: p0,
        texcoord: [u0, v0],
        tile_origin,
        tile_size,
        color,
    });
    vertices.push(TerrainVertex {
        position: p2,
        texcoord: [u1, v1],
        tile_origin,
        tile_size,
        color,
    });
    vertices.push(TerrainVertex {
        position: p3,
        texcoord: [u0, v1],
        tile_origin,
        tile_size,
        color,
    });
}
