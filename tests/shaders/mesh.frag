#version 460
#include "common/lighting.glsl"
layout(set = 1, binding = 0) uniform sampler2D albedo_map;
layout(set = 1, binding = 1) uniform texture2D extra_maps[4];
layout(set = 1, binding = 2) uniform sampler extra_sampler;
layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec2 in_uv;
layout(location = 0) out vec4 out_color;
void main() {
    vec3 albedo = texture(albedo_map, in_uv).rgb
                + texture(sampler2D(extra_maps[1], extra_sampler), in_uv).rgb * 0.1;
    out_color = vec4(lambert(in_normal, vec3(0.3, 1.0, 0.2), albedo), 1.0);
}
