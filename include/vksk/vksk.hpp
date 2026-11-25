// Vulkan Shader Kit: compile GLSL to SPIR-V, reflect its interface, cache builds, watch for changes.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vksk {

namespace fs = std::filesystem;

enum class Stage { Vertex, TessControl, TessEval, Geometry, Fragment, Compute };

std::optional<Stage> stage_from_extension(const fs::path& path);
const char* stage_name(Stage stage);

// ---- includes -------------------------------------------------------------

// A source with every `#include "file"` inlined and `#line` directives inserted, so compiler
// messages can be mapped back to the original file and line.
struct ExpandedSource {
    std::string text;
    std::vector<fs::path> files;  // index = GLSL source-string number used in #line
};

// Inlines includes relative to the including file, then the include directories.
// Throws std::runtime_error on a missing file or an include cycle.
ExpandedSource expand_includes(const fs::path& path, const std::vector<fs::path>& include_dirs);

// ---- compile --------------------------------------------------------------

struct Diagnostic {
    fs::path file;
    int line = 0;
    std::string message;
    std::string format() const;  // "file:line: message", the form editors make clickable
};

struct CompileResult {
    std::vector<std::uint32_t> spirv;
    std::vector<Diagnostic> errors;
    std::vector<fs::path> dependencies;  // the source and everything it included
    bool ok() const { return errors.empty() && !spirv.empty(); }
};

struct CompileOptions {
    std::vector<fs::path> include_dirs;
    std::vector<std::pair<std::string, std::string>> defines;
    bool debug_info = false;
};

CompileResult compile_file(const fs::path& path, const CompileOptions& options = {});

// ---- reflect --------------------------------------------------------------

enum class DescriptorType {
    Sampler, CombinedImageSampler, SampledImage, StorageImage, UniformTexelBuffer,
    StorageTexelBuffer, UniformBuffer, StorageBuffer, InputAttachment
};
const char* descriptor_type_name(DescriptorType type);  // VkDescriptorType enumerator name

struct Binding {
    std::string name;
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    DescriptorType type{};
    std::uint32_t count = 1;  // array size; 0 = runtime-sized (bindless)
};

struct Location {
    std::string name;
    std::uint32_t location = 0;
};

struct Reflection {
    Stage stage{};
    std::string entry_point;
    std::vector<Binding> bindings;     // sorted by (set, binding)
    std::vector<Location> inputs;      // sorted by location; built-ins excluded
    std::vector<Location> outputs;
    std::uint32_t push_constant_size = 0;
    std::uint32_t local_size[3] = {0, 0, 0};  // compute only
    std::string to_json() const;
};

// Parses SPIR-V directly (no SPIRV-Reflect dependency). Throws std::runtime_error on bad input.
Reflection reflect(const std::vector<std::uint32_t>& spirv);

// ---- build cache ----------------------------------------------------------

struct BuildEntry {
    fs::path source;
    fs::path output;
    bool rebuilt = false;
    std::vector<Diagnostic> errors;
};

// Compiles every shader under src_dir into out_dir as <name>.<stage-ext>.spv plus a .json reflection,
// skipping shaders whose source and includes hash the same as the last successful build.
std::vector<BuildEntry> build_tree(const fs::path& src_dir, const fs::path& out_dir,
                                   const CompileOptions& options = {});

std::uint64_t hash_files(const std::vector<fs::path>& files);

// ---- watch ----------------------------------------------------------------

// Polls the tree and rebuilds changed shaders, calling on_reload for each rebuilt one.
// Returns when should_stop() is true. Polling keeps it portable across Windows and Linux.
void watch_tree(const fs::path& src_dir, const fs::path& out_dir, const CompileOptions& options,
                const std::function<void(const BuildEntry&)>& on_reload,
                const std::function<bool()>& should_stop, int poll_ms = 250);

}  // namespace vksk
