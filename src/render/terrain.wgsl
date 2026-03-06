// Terrain rendering shader for MCPE
// Textured blocks with per-vertex color tinting and distance fog.

struct Uniforms {
    view_proj: mat4x4<f32>,
    fog_color: vec4<f32>,
    fog_start: f32,
    fog_end: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(1) @binding(0) var terrain_texture: texture_2d<f32>;
@group(1) @binding(1) var terrain_sampler: sampler;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) texcoord: vec2<f32>,
    @location(2) tile_origin: vec2<f32>,
    @location(3) tile_size: vec2<f32>,
    @location(4) color: vec3<f32>,
};

struct VertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) texcoord: vec2<f32>,
    @location(1) tile_origin: vec2<f32>,
    @location(2) tile_size: vec2<f32>,
    @location(3) color: vec3<f32>,
    @location(4) fog_factor: f32,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let world_pos = vec4<f32>(in.position, 1.0);
    out.clip_position = uniforms.view_proj * world_pos;
    out.texcoord = in.texcoord;
    out.tile_origin = in.tile_origin;
    out.tile_size = in.tile_size;
    out.color = in.color;

    // Linear fog based on clip-space depth
    let dist = length(out.clip_position.xyz);
    out.fog_factor = clamp((uniforms.fog_end - dist) / (uniforms.fog_end - uniforms.fog_start), 0.0, 1.0);

    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    // Wrap UVs within the tile for repeating textures
    let rel = in.texcoord - in.tile_origin;
    let wrapped = fract(rel / in.tile_size) * in.tile_size + in.tile_origin;
    let tex_color = textureSample(terrain_texture, terrain_sampler, wrapped);

    // Discard nearly-transparent pixels (alpha test)
    if tex_color.a < 0.1 {
        discard;
    }

    // Apply per-vertex color (tint + lighting baked together)
    let lit_color = tex_color.rgb * in.color;

    let final_color = mix(uniforms.fog_color.rgb, lit_color, in.fog_factor);

    if tex_color.a < 0.5 {
        discard;
    }

    // Force opaque to prevent X-ray on water
    return vec4<f32>(final_color, 1.0);
}
