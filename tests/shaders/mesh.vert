#version 460
layout(set = 0, binding = 0) uniform Camera {
    mat4 view_proj;
    vec4 eye;
} camera;
layout(push_constant) uniform Push {
    mat4 model;
    uint material;
} push;
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec2 out_uv;
void main() {
    out_normal = mat3(push.model) * in_normal;
    out_uv = in_uv;
    gl_Position = camera.view_proj * push.model * vec4(in_position, 1.0);
}
