// Self-contained tests (no framework): each CHECK prints the failing expression and line.
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

#include "vksk/vksk.hpp"

namespace fs = std::filesystem;
static int g_failures = 0;
#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK(" #expr ")\n"; \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

static const fs::path kShaders = VKSK_TEST_SHADERS;

static fs::path temp_dir(const char* name) {
    fs::path dir = fs::temp_directory_path() / ("vksk_test_" + std::string(name));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

static const vksk::Binding* find(const vksk::Reflection& r, std::uint32_t set, std::uint32_t binding) {
    for (const auto& b : r.bindings) {
        if (b.set == set && b.binding == binding) return &b;
    }
    return nullptr;
}

static void test_vertex_reflection() {
    auto result = vksk::compile_file(kShaders / "mesh.vert");
    CHECK(result.ok());
    if (!result.ok()) return;
    auto r = vksk::reflect(result.spirv);
    CHECK(r.stage == vksk::Stage::Vertex);
    CHECK(r.entry_point == "main");
    CHECK(r.bindings.size() == 1);
    auto* camera = find(r, 0, 0);
    CHECK(camera && camera->type == vksk::DescriptorType::UniformBuffer && camera->name == "camera");
    CHECK(r.push_constant_size == 68);  // mat4 (64) + uint (4)
    CHECK(r.inputs.size() == 3 && r.inputs[2].name == "in_uv" && r.inputs[2].location == 2);
    CHECK(r.outputs.size() == 2);  // gl_Position is a built-in and is left out
}

static void test_fragment_includes_and_descriptor_kinds() {
    auto result = vksk::compile_file(kShaders / "mesh.frag");
    CHECK(result.ok());
    CHECK(result.dependencies.size() == 2);  // the shader and common/lighting.glsl
    if (!result.ok()) return;
    auto r = vksk::reflect(result.spirv);
    auto* albedo = find(r, 1, 0);
    auto* extras = find(r, 1, 1);
    auto* sampler = find(r, 1, 2);
    CHECK(albedo && albedo->type == vksk::DescriptorType::CombinedImageSampler);
    CHECK(extras && extras->type == vksk::DescriptorType::SampledImage && extras->count == 4);
    CHECK(sampler && sampler->type == vksk::DescriptorType::Sampler);
    CHECK(r.push_constant_size == 0);
}

static void test_compute_reflection() {
    auto result = vksk::compile_file(kShaders / "particles.comp");
    CHECK(result.ok());
    if (!result.ok()) return;
    auto r = vksk::reflect(result.spirv);
    CHECK(r.stage == vksk::Stage::Compute);
    CHECK(r.local_size[0] == 64 && r.local_size[1] == 1);
    auto* particles = find(r, 0, 0);
    auto* heat = find(r, 0, 1);
    CHECK(particles && particles->type == vksk::DescriptorType::StorageBuffer);
    CHECK(heat && heat->type == vksk::DescriptorType::StorageImage);
    CHECK(r.push_constant_size == 8);
    CHECK(r.to_json().find("\"VK_DESCRIPTOR_TYPE_STORAGE_IMAGE\"") != std::string::npos);
}

static void test_error_maps_to_original_file_and_line() {
    auto result = vksk::compile_file(kShaders.parent_path() / "broken" / "bad.frag");
    CHECK(!result.ok());
    CHECK(!result.errors.empty());
    if (result.errors.empty()) return;
    const auto& e = result.errors.front();
    CHECK(e.file.filename() == "bad.frag");
    CHECK(e.line == 5);
    std::cerr << "  (expected diagnostic) " << e.format() << '\n';
}

static void test_missing_include_is_an_error_not_a_crash() {
    fs::path dir = temp_dir("missing");
    std::ofstream(dir / "x.frag") << "#version 460\n#include \"nope.glsl\"\nvoid main() {}\n";
    auto result = vksk::compile_file(dir / "x.frag");
    CHECK(!result.ok());
    CHECK(!result.errors.empty() && result.errors[0].message.find("nope.glsl") != std::string::npos);
}

static void test_build_cache_and_include_invalidation() {
    fs::path src = temp_dir("src");
    fs::path out = temp_dir("out");
    fs::copy(kShaders, src, fs::copy_options::recursive);

    auto first = vksk::build_tree(src, out);
    CHECK(first.size() == 3);
    for (const auto& e : first) CHECK(e.rebuilt && e.errors.empty() && fs::exists(e.output));
    CHECK(fs::exists(out / "mesh.frag.json"));

    auto second = vksk::build_tree(src, out);
    for (const auto& e : second) CHECK(!e.rebuilt);

    // Touching the shared include rebuilds only the shader that includes it.
    std::ofstream(src / "common" / "lighting.glsl", std::ios::app) << "// edited\n";
    auto third = vksk::build_tree(src, out);
    int rebuilt = 0;
    for (const auto& e : third) {
        rebuilt += e.rebuilt;
        if (e.rebuilt) CHECK(e.source.filename() == "mesh.frag");
    }
    CHECK(rebuilt == 1);
}

static void test_watch_reports_reload() {
    fs::path src = temp_dir("wsrc");
    fs::path out = temp_dir("wout");
    fs::copy_file(kShaders / "particles.comp", src / "particles.comp");
    vksk::build_tree(src, out);

    std::vector<fs::path> reloaded;
    int polls = 0;
    vksk::watch_tree(src, out, {}, [&](const vksk::BuildEntry& e) { reloaded.push_back(e.source); },
                     [&] {
                         if (++polls == 2) std::ofstream(src / "particles.comp", std::ios::app) << "// tweak\n";
                         return polls > 4;
                     }, 10);
    CHECK(reloaded.size() == 1);
}

static void test_rejects_garbage() {
    bool threw = false;
    try {
        vksk::reflect({1, 2, 3, 4, 5, 6});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

int main() {
    test_vertex_reflection();
    test_fragment_includes_and_descriptor_kinds();
    test_compute_reflection();
    test_error_maps_to_original_file_and_line();
    test_missing_include_is_an_error_not_a_crash();
    test_build_cache_and_include_invalidation();
    test_watch_reports_reload();
    test_rejects_garbage();
    std::cout << (g_failures ? "FAILED: " : "all tests passed") << (g_failures ? std::to_string(g_failures) : "")
              << '\n';
    return g_failures ? 1 : 0;
}
