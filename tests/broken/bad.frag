#version 460
#include "../shaders/common/lighting.glsl"
layout(location = 0) out vec4 out_color;
void main() {
    out_color = vec4(lambert(vec3(1.0), vec3(1.0)), 1.0);
}
