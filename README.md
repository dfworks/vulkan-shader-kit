# Vulkan Shader Kit

A small C++20 tool and library for the shader side of a Vulkan engine:

- **build**: compiles GLSL (`.vert .frag .comp .geom .tesc .tese`) to SPIR-V 1.6 with glslang, resolving
  `#include "..."` itself so compile errors point at the original file and line (`file:line: message`).
- **cache**: rebuilds only shaders whose source *or any included file* changed (content hashes, not timestamps).
- **reflect**: reads the SPIR-V directly (its own parser, no SPIRV-Reflect) and writes the pipeline-layout facts as JSON
  next to each `.spv`: descriptor set/binding/`VkDescriptorType`/array count, push-constant size, vertex inputs and
  fragment outputs by location, compute local size.
- **watch**: hot-reload loop that recompiles on change and calls back with each rebuilt shader.

```
cmake -S . -B build && cmake --build build -j
build/vksk build shaders/ build/spv -I shaders/include -D QUALITY=2
build/vksk watch shaders/ build/spv
build/vksk reflect build/spv/mesh.frag.spv
build/vksk_tests
```

Example reflection (`mesh.frag.json`):

```json
{
  "stage": "fragment",
  "bindings": [
    {"name": "albedo_map", "set": 1, "binding": 0, "type": "VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER", "count": 1},
    {"name": "extra_maps", "set": 1, "binding": 1, "type": "VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE", "count": 4},
    {"name": "extra_sampler", "set": 1, "binding": 2, "type": "VK_DESCRIPTOR_TYPE_SAMPLER", "count": 1}
  ]
}
```

glslang 16.6.0 is fetched by CMake. No Vulkan SDK is needed to build or test.
