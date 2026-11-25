// A small SPIR-V reader: enough of the module to recover the descriptor, push-constant and
// stage interface a Vulkan pipeline layout needs. Spec: https://registry.khronos.org/SPIR-V/
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <optional>
#include <tuple>
#include <unordered_map>

#include "vksk/vksk.hpp"

namespace vksk {
namespace {

constexpr std::uint32_t kMagic = 0x07230203;

// Opcodes
enum : std::uint32_t {
    OpName = 5, OpMemberName = 6, OpEntryPoint = 15, OpExecutionMode = 16,
    OpTypeBool = 20, OpTypeInt = 21, OpTypeFloat = 22, OpTypeVector = 23, OpTypeMatrix = 24,
    OpTypeImage = 25, OpTypeSampler = 26, OpTypeSampledImage = 27, OpTypeArray = 28,
    OpTypeRuntimeArray = 29, OpTypeStruct = 30, OpTypePointer = 32, OpConstant = 43,
    OpVariable = 59, OpDecorate = 71, OpMemberDecorate = 72,
};
// Decorations
enum : std::uint32_t {
    DecBlock = 2, DecBufferBlock = 3, DecArrayStride = 6, DecMatrixStride = 7, DecBuiltIn = 11,
    DecLocation = 30, DecBinding = 33, DecDescriptorSet = 34, DecOffset = 35,
};
// Storage classes
enum : std::uint32_t {
    ScUniformConstant = 0, ScInput = 1, ScUniform = 2, ScOutput = 3, ScPushConstant = 9,
    ScStorageBuffer = 12,
};
enum : std::uint32_t { ExecLocalSize = 17 };
enum : std::uint32_t { DimBuffer = 5, DimSubpassData = 6 };

struct Type {
    std::uint32_t op = 0;
    std::vector<std::uint32_t> args;  // operands after the result id
};

struct Decorations {
    std::optional<std::uint32_t> set, binding, location, array_stride, matrix_stride;
    bool block = false, buffer_block = false, builtin = false;
};

std::string read_string(const std::uint32_t* words, std::size_t count) {
    std::string s;
    for (std::size_t i = 0; i < count; ++i) {
        for (int b = 0; b < 4; ++b) {
            const char c = static_cast<char>((words[i] >> (8 * b)) & 0xff);
            if (c == '\0') return s;
            s.push_back(c);
        }
    }
    return s;
}

class Module {
public:
    explicit Module(const std::vector<std::uint32_t>& spirv) {
        if (spirv.size() < 5 || spirv[0] != kMagic) throw std::runtime_error("not a SPIR-V module");
        std::size_t i = 5;
        while (i < spirv.size()) {
            const std::uint32_t word_count = spirv[i] >> 16;
            const std::uint32_t op = spirv[i] & 0xffff;
            if (word_count == 0 || i + word_count > spirv.size()) throw std::runtime_error("truncated SPIR-V");
            read(op, &spirv[i + 1], word_count - 1);
            i += word_count;
        }
    }

    Reflection build() const {
        Reflection r;
        r.stage = stage_;
        r.entry_point = entry_point_;
        std::copy(std::begin(local_size_), std::end(local_size_), r.local_size);
        for (const auto& [id, var] : variables_) {
            const auto& [pointer_type, storage] = var;
            const std::uint32_t pointee = types_.at(pointer_type).args.at(1);
            const Decorations& d = decor(id);
            const std::string& name = name_of(id, pointee);
            if (storage == ScInput || storage == ScOutput) {
                if (d.builtin || is_builtin_block(pointee) || !d.location) continue;
                (storage == ScInput ? r.inputs : r.outputs).push_back({name, *d.location});
            } else if (storage == ScPushConstant) {
                r.push_constant_size = std::max(r.push_constant_size, size_of(pointee));
            } else if (storage == ScUniformConstant || storage == ScUniform || storage == ScStorageBuffer) {
                Binding b;
                b.name = name;
                b.set = d.set.value_or(0);
                b.binding = d.binding.value_or(0);
                std::uint32_t element = pointee;
                const Type& t = types_.at(pointee);
                if (t.op == OpTypeArray) {
                    element = t.args.at(0);
                    b.count = constant(t.args.at(1));
                } else if (t.op == OpTypeRuntimeArray) {
                    element = t.args.at(0);
                    b.count = 0;
                }
                b.type = descriptor_type(element, storage);
                r.bindings.push_back(std::move(b));
            }
        }
        std::sort(r.bindings.begin(), r.bindings.end(),
                  [](const Binding& a, const Binding& b) { return std::tie(a.set, a.binding) < std::tie(b.set, b.binding); });
        auto by_location = [](const Location& a, const Location& b) { return a.location < b.location; };
        std::sort(r.inputs.begin(), r.inputs.end(), by_location);
        std::sort(r.outputs.begin(), r.outputs.end(), by_location);
        return r;
    }

private:
    void read(std::uint32_t op, const std::uint32_t* w, std::size_t n) {
        switch (op) {
            case OpName:
                names_[w[0]] = read_string(w + 1, n - 1);
                break;
            case OpEntryPoint:
                if (entry_point_.empty()) {
                    stage_ = execution_model(w[0]);
                    entry_id_ = w[1];
                    entry_point_ = read_string(w + 2, n - 2);
                }
                break;
            case OpExecutionMode:
                if (w[0] == entry_id_ && w[1] == ExecLocalSize && n >= 5) {
                    local_size_[0] = w[2], local_size_[1] = w[3], local_size_[2] = w[4];
                }
                break;
            case OpDecorate: {
                Decorations& d = decorations_[w[0]];
                switch (w[1]) {
                    case DecBlock: d.block = true; break;
                    case DecBufferBlock: d.buffer_block = true; break;
                    case DecBuiltIn: d.builtin = true; break;
                    case DecLocation: d.location = w[2]; break;
                    case DecBinding: d.binding = w[2]; break;
                    case DecDescriptorSet: d.set = w[2]; break;
                    case DecArrayStride: d.array_stride = w[2]; break;
                    default: break;
                }
                break;
            }
            case OpMemberDecorate:
                if (w[2] == DecOffset) member_offsets_[w[0]][w[1]] = w[3];
                if (w[2] == DecBuiltIn) builtin_structs_.insert({w[0], true});
                if (w[2] == DecMatrixStride) decorations_[w[0]].matrix_stride = w[3];
                break;
            case OpTypeBool: case OpTypeInt: case OpTypeFloat: case OpTypeVector: case OpTypeMatrix:
            case OpTypeImage: case OpTypeSampler: case OpTypeSampledImage: case OpTypeArray:
            case OpTypeRuntimeArray: case OpTypeStruct:
                types_[w[0]] = Type{op, std::vector<std::uint32_t>(w + 1, w + n)};
                break;
            case OpTypePointer:
                types_[w[0]] = Type{op, {w[1], w[2]}};  // storage class, pointee
                break;
            case OpConstant:
                constants_[w[1]] = w[2];
                break;
            case OpVariable:
                if (w[2] != 7 /* Function */) variables_.push_back({w[1], {w[0], w[2]}});
                break;
            default:
                break;
        }
    }

    static Stage execution_model(std::uint32_t model) {
        switch (model) {
            case 0: return Stage::Vertex;
            case 1: return Stage::TessControl;
            case 2: return Stage::TessEval;
            case 3: return Stage::Geometry;
            case 4: return Stage::Fragment;
            case 5: return Stage::Compute;
            default: throw std::runtime_error("unsupported execution model " + std::to_string(model));
        }
    }

    const Decorations& decor(std::uint32_t id) const {
        static const Decorations none;
        const auto it = decorations_.find(id);
        return it == decorations_.end() ? none : it->second;
    }

    std::string name_of(std::uint32_t var, std::uint32_t type) const {
        if (auto it = names_.find(var); it != names_.end() && !it->second.empty()) return it->second;
        if (auto it = names_.find(type); it != names_.end()) return it->second;  // unnamed block instance
        return "_" + std::to_string(var);
    }

    bool is_builtin_block(std::uint32_t type) const {
        const Type& t = types_.at(type);
        const std::uint32_t inner = (t.op == OpTypeArray) ? t.args.at(0) : type;
        return builtin_structs_.count(inner) > 0;
    }

    std::uint32_t constant(std::uint32_t id) const {
        const auto it = constants_.find(id);
        if (it == constants_.end()) throw std::runtime_error("array length is not a plain constant");
        return it->second;
    }

    std::uint32_t size_of(std::uint32_t id) const {
        const Type& t = types_.at(id);
        switch (t.op) {
            case OpTypeBool: return 4;
            case OpTypeInt: case OpTypeFloat: return t.args.at(0) / 8;
            case OpTypeVector: return size_of(t.args.at(0)) * t.args.at(1);
            case OpTypeMatrix: {
                const std::uint32_t stride = decor(id).matrix_stride.value_or(size_of(t.args.at(0)));
                return stride * t.args.at(1);
            }
            case OpTypeArray: {
                const std::uint32_t stride = decor(id).array_stride.value_or(size_of(t.args.at(0)));
                return stride * constant(t.args.at(1));
            }
            case OpTypeStruct: {
                std::uint32_t end = 0;
                const auto offsets = member_offsets_.find(id);
                for (std::uint32_t m = 0; m < t.args.size(); ++m) {
                    std::uint32_t offset = end;
                    if (offsets != member_offsets_.end()) {
                        if (auto o = offsets->second.find(m); o != offsets->second.end()) offset = o->second;
                    }
                    end = std::max(end, offset + member_size(id, m, t.args[m]));
                }
                return end;
            }
            default: return 0;
        }
    }

    // Matrix members carry their stride on the struct member, not the type.
    std::uint32_t member_size(std::uint32_t, std::uint32_t, std::uint32_t member_type) const {
        const Type& t = types_.at(member_type);
        if (t.op == OpTypeMatrix) {
            const std::uint32_t column = size_of(t.args.at(0));
            const std::uint32_t stride = column > 8 ? 16 : column;  // std140/std430 vec3/vec4 columns
            return stride * t.args.at(1);
        }
        return size_of(member_type);
    }

    DescriptorType descriptor_type(std::uint32_t type, std::uint32_t storage) const {
        const Type& t = types_.at(type);
        switch (t.op) {
            case OpTypeSampler: return DescriptorType::Sampler;
            case OpTypeSampledImage: {
                const Type& image = types_.at(t.args.at(0));
                return image.args.at(1) == DimBuffer ? DescriptorType::UniformTexelBuffer
                                                     : DescriptorType::CombinedImageSampler;
            }
            case OpTypeImage: {
                const std::uint32_t dim = t.args.at(1);
                const std::uint32_t sampled = t.args.at(5);
                if (dim == DimSubpassData) return DescriptorType::InputAttachment;
                if (dim == DimBuffer) return sampled == 2 ? DescriptorType::StorageTexelBuffer
                                                          : DescriptorType::UniformTexelBuffer;
                return sampled == 2 ? DescriptorType::StorageImage : DescriptorType::SampledImage;
            }
            case OpTypeStruct:
                if (storage == ScStorageBuffer || decor(type).buffer_block) return DescriptorType::StorageBuffer;
                return DescriptorType::UniformBuffer;
            default:
                throw std::runtime_error("unsupported resource type");
        }
    }

    Stage stage_ = Stage::Vertex;
    std::uint32_t entry_id_ = 0;
    std::string entry_point_;
    std::uint32_t local_size_[3] = {0, 0, 0};
    std::unordered_map<std::uint32_t, std::string> names_;
    std::unordered_map<std::uint32_t, Decorations> decorations_;
    std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, std::uint32_t>> member_offsets_;
    std::unordered_map<std::uint32_t, bool> builtin_structs_;
    std::unordered_map<std::uint32_t, Type> types_;
    std::unordered_map<std::uint32_t, std::uint32_t> constants_;
    std::vector<std::pair<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>>> variables_;
};

std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

}  // namespace

const char* descriptor_type_name(DescriptorType type) {
    switch (type) {
        case DescriptorType::Sampler: return "VK_DESCRIPTOR_TYPE_SAMPLER";
        case DescriptorType::CombinedImageSampler: return "VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER";
        case DescriptorType::SampledImage: return "VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE";
        case DescriptorType::StorageImage: return "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE";
        case DescriptorType::UniformTexelBuffer: return "VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER";
        case DescriptorType::StorageTexelBuffer: return "VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER";
        case DescriptorType::UniformBuffer: return "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER";
        case DescriptorType::StorageBuffer: return "VK_DESCRIPTOR_TYPE_STORAGE_BUFFER";
        case DescriptorType::InputAttachment: return "VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT";
    }
    return "?";
}

Reflection reflect(const std::vector<std::uint32_t>& spirv) { return Module(spirv).build(); }

std::string Reflection::to_json() const {
    std::ostringstream o;
    o << "{\n  \"stage\": \"" << stage_name(stage) << "\",\n  \"entry_point\": \"" << json_escape(entry_point)
      << "\",\n  \"push_constant_size\": " << push_constant_size << ",\n  \"local_size\": [" << local_size[0]
      << ", " << local_size[1] << ", " << local_size[2] << "],\n  \"bindings\": [";
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        const Binding& b = bindings[i];
        o << (i ? "," : "") << "\n    {\"name\": \"" << json_escape(b.name) << "\", \"set\": " << b.set
          << ", \"binding\": " << b.binding << ", \"type\": \"" << descriptor_type_name(b.type)
          << "\", \"count\": " << b.count << "}";
    }
    o << (bindings.empty() ? "" : "\n  ") << "],\n";
    auto locations = [&o](const char* key, const std::vector<Location>& list, bool last) {
        o << "  \"" << key << "\": [";
        for (std::size_t i = 0; i < list.size(); ++i) {
            o << (i ? ", " : "") << "{\"name\": \"" << json_escape(list[i].name) << "\", \"location\": "
              << list[i].location << "}";
        }
        o << "]" << (last ? "\n" : ",\n");
    };
    locations("inputs", inputs, false);
    locations("outputs", outputs, true);
    o << "}\n";
    return o.str();
}

}  // namespace vksk
