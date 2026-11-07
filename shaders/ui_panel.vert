#version 450
layout(push_constant) uniform PushConstants { mat4 projection; mat4 modelView; } pc;
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 0) out vec2 texCoord;
void main() { gl_Position = pc.projection * pc.modelView * vec4(inPosition, 1.0); texCoord = inTexCoord; }
