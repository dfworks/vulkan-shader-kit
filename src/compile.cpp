#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

#include <mutex>
#include <sstream>
#include <stdexcept>

#include "vksk/vksk.hpp"

namespace vksk {
namespace {

glslang_stage_t to_glslang(Stage stage) {
    switch (stage) {
        case Stage::Vertex: return GLSLANG_STAGE_VERTEX;
        case Stage::TessControl: return GLSLANG_STAGE_TESSCONTROL;
        case Stage::TessEval: return GLSLANG_STAGE_TESSEVALUATION;
        case Stage::Geometry: return GLSLANG_STAGE_GEOMETRY;
        case Stage::Fragment: return GLSLANG_STAGE_FRAGMENT;
        case Stage::Compute: return GLSLANG_STAGE_COMPUTE;
    }
    throw std::logic_error("unknown stage");
}

void ensure_initialized() {
    static std::once_flag once;
    std::call_once(once, [] { glslang_initialize_process(); });
}

// glslang reports "ERROR: <string-number>:<line>: message". Map the string number back to a file.
void parse_log(const char* log, const ExpandedSource& source, std::vector<Diagnostic>& out) {
    if (!log) return;
    std::istringstream in(log);
    std::string line;
    while (std::getline(in, line)) {
        const std::string tag = "ERROR: ";
        if (line.rfind(tag, 0) != 0) continue;
        Diagnostic d;
        std::size_t a = tag.size();
        std::size_t b = line.find(':', a);
        std::size_t c = b == std::string::npos ? b : line.find(':', b + 1);
        int index = -1;
        if (b != std::string::npos && c != std::string::npos) {
            try {
                index = std::stoi(line.substr(a, b - a));
                d.line = std::stoi(line.substr(b + 1, c - b - 1));
                d.message = line.substr(c + 1);
                if (!d.message.empty() && d.message.front() == ' ') d.message.erase(0, 1);
            } catch (const std::exception&) {
                index = -1;
            }
        }
        if (index >= 0 && index < static_cast<int>(source.files.size())) {
            d.file = source.files[index];
        } else {
            d.file = source.files.empty() ? fs::path{} : source.files.front();
            d.line = 0;
            d.message = line.substr(tag.size());
        }
        if (d.message.find("compilation terminated") == std::string::npos &&
            d.message.find("compilation errors") == std::string::npos)
            out.push_back(std::move(d));
    }
}

}  // namespace

std::optional<Stage> stage_from_extension(const fs::path& path) {
    const std::string ext = path.extension().string();
    if (ext == ".vert") return Stage::Vertex;
    if (ext == ".tesc") return Stage::TessControl;
    if (ext == ".tese") return Stage::TessEval;
    if (ext == ".geom") return Stage::Geometry;
    if (ext == ".frag") return Stage::Fragment;
    if (ext == ".comp") return Stage::Compute;
    return std::nullopt;
}

const char* stage_name(Stage stage) {
    switch (stage) {
        case Stage::Vertex: return "vertex";
        case Stage::TessControl: return "tess_control";
        case Stage::TessEval: return "tess_eval";
        case Stage::Geometry: return "geometry";
        case Stage::Fragment: return "fragment";
        case Stage::Compute: return "compute";
    }
    return "unknown";
}

std::string Diagnostic::format() const {
    std::ostringstream ss;
    ss << file.string() << ':' << line << ": " << message;
    return ss.str();
}

CompileResult compile_file(const fs::path& path, const CompileOptions& options) {
    CompileResult result;
    const auto stage = stage_from_extension(path);
    if (!stage) {
        result.errors.push_back({path, 0, "unknown shader stage for extension " + path.extension().string()});
        return result;
    }
    ExpandedSource source;
    try {
        source = expand_includes(path, options.include_dirs);
    } catch (const std::exception& e) {
        result.errors.push_back({path, 0, e.what()});
        return result;
    }
    result.dependencies = source.files;

    std::string preamble;
    for (const auto& [name, value] : options.defines) preamble += "#define " + name + " " + value + "\n";

    ensure_initialized();
    glslang_input_t input{};
    input.language = GLSLANG_SOURCE_GLSL;
    input.stage = to_glslang(*stage);
    input.client = GLSLANG_CLIENT_VULKAN;
    input.client_version = GLSLANG_TARGET_VULKAN_1_3;
    input.target_language = GLSLANG_TARGET_SPV;
    input.target_language_version = GLSLANG_TARGET_SPV_1_6;
    input.code = source.text.c_str();
    input.default_version = 460;
    input.default_profile = GLSLANG_NO_PROFILE;
    input.messages = GLSLANG_MSG_DEFAULT_BIT;
    input.resource = glslang_default_resource();

    glslang_shader_t* shader = glslang_shader_create(&input);
    if (!preamble.empty()) glslang_shader_set_preamble(shader, preamble.c_str());
    if (!glslang_shader_preprocess(shader, &input) || !glslang_shader_parse(shader, &input)) {
        parse_log(glslang_shader_get_info_log(shader), source, result.errors);
        glslang_shader_delete(shader);
        return result;
    }
    glslang_program_t* program = glslang_program_create();
    glslang_program_add_shader(program, shader);
    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
        parse_log(glslang_program_get_info_log(program), source, result.errors);
    } else {
        glslang_spv_options_t spv{};
        spv.generate_debug_info = options.debug_info;
        spv.validate = true;
        glslang_program_SPIRV_generate_with_options(program, input.stage, &spv);
        const std::size_t words = glslang_program_SPIRV_get_size(program);
        const unsigned int* data = glslang_program_SPIRV_get_ptr(program);
        result.spirv.assign(data, data + words);
    }
    glslang_program_delete(program);
    glslang_shader_delete(shader);
    return result;
}

}  // namespace vksk
