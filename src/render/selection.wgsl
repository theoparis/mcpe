struct Uniforms {
    view_proj: mat4x4<f32>,
    block_pos: vec3<f32>,
    progress: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
};

@vertex
fn vs_main(@location(0) pos: vec3<f32>) -> VertexOutput {
    var out: VertexOutput;
    // Scale slightly to avoid z-fighting or being hidden inside the block
    let offset = vec3<f32>(0.001, 0.001, 0.001);
    let world_pos = pos * 1.002 - offset + uniforms.block_pos;
    out.position = uniforms.view_proj * vec4<f32>(world_pos, 1.0);
    return out;
}

@fragment
fn fs_main() -> @location(0) vec4<f32> {
    // Increase opacity as progress grows
    let alpha = 0.4 + uniforms.progress * 0.4;
    return vec4<f32>(0.0, 0.0, 0.0, alpha);
}
