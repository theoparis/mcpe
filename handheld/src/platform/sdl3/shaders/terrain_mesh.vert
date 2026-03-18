#version 450

layout(push_constant) uniform WorldPushConstants {
  mat4 modelView;
  mat4 projection;
  uint textureIndex;
  vec4 colorMultiplier;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inLocalTexCoord;
layout(location = 2) in vec2 inTileOrigin;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec2 fragTileOrigin;
layout(location = 2) out vec4 fragColor;

void main() {
  fragTexCoord = inLocalTexCoord;
  fragTileOrigin = inTileOrigin;
  fragColor = inColor;
  gl_Position = pc.projection * pc.modelView * vec4(inPosition, 1.0);
}
