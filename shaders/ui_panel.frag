#version 450
layout(set = 0, binding = 0) uniform sampler2D panelImage;
layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;
void main() { outColor = texture(panelImage, texCoord); }
