#version 450

layout(push_constant) uniform QuadPushConstants {
  mat4 modelView;
  mat4 projection;
  uint textureIndex;
  vec4 colorMultiplier;
} pc;

layout(set = 0, binding = 0) uniform sampler2D atlasTextures[1024];

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
  vec4 sampled =
      texture(atlasTextures[pc.textureIndex], fragTexCoord) * fragColor;
  if (sampled.a <= 0.1) {
    discard;
  }
  outColor = sampled;
}
