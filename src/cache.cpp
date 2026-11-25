#include <algorithm>
#include <fstream>
#include <sstream>

#include "vksk/vksk.hpp"

namespace vksk {
namespace {

constexpr std::uint64_t kFnvOffset = 0xcbf29ce484222325ull;
constexpr std::uint64_t kFnvPrime = 0x100000001b3ull;

void mix(std::uint64_t& h, const char* data, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        h ^= static_cast<unsigned char>(data[i]);
        h *= kFnvPrime;
    }
}

fs::path stamp_path(const fs::path& output) { return fs::path(output).concat(".stamp"); }

// A stamp records the input hash and the dependency list of the last successful build.
struct Stamp {
    std::uint64_t hash = 0;
    std::vector<fs::path> dependencies;
};

std::optional<Stamp> read_stamp(const fs::path& output) {
    std::ifstream in(stamp_path(output));
    if (!in) return std::nullopt;
    Stamp s;
    if (!(in >> std::hex >> s.hash)) return std::nullopt;
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line)) {
        if (!line.empty()) s.dependencies.emplace_back(line);
    }
    return s;
}

void write_stamp(const fs::path& output, std::uint64_t hash, const std::vector<fs::path>& deps) {
    std::ofstream out(stamp_path(output), std::ios::trunc);
    out << std::hex << hash << '\n';
    for (const auto& d : deps) out << d.string() << '\n';
}

void write_spirv(const fs::path& path, const std::vector<std::uint32_t>& words) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(words.data()), static_cast<std::streamsize>(words.size() * 4));
}

bool is_shader(const fs::path& p) { return stage_from_extension(p).has_value(); }

}  // namespace

std::uint64_t hash_files(const std::vector<fs::path>& files) {
    std::uint64_t h = kFnvOffset;
    for (const auto& f : files) {
        const std::string name = f.string();
        mix(h, name.data(), name.size());
        std::ifstream in(f, std::ios::binary);
        if (!in) {
            mix(h, "<missing>", 9);
            continue;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string data = ss.str();
        mix(h, data.data(), data.size());
    }
    return h;
}

std::vector<BuildEntry> build_tree(const fs::path& src_dir, const fs::path& out_dir, const CompileOptions& options) {
    std::vector<fs::path> sources;
    for (const auto& e : fs::recursive_directory_iterator(src_dir)) {
        if (e.is_regular_file() && is_shader(e.path())) sources.push_back(e.path());
    }
    std::sort(sources.begin(), sources.end());

    CompileOptions with_root = options;
    with_root.include_dirs.push_back(src_dir);

    std::vector<BuildEntry> entries;
    for (const auto& src : sources) {
        BuildEntry entry;
        entry.source = src;
        const fs::path rel = fs::relative(src, src_dir);
        entry.output = out_dir / fs::path(rel).concat(".spv");
        fs::create_directories(entry.output.parent_path());

        if (const auto stamp = read_stamp(entry.output); stamp && fs::exists(entry.output) &&
                                                         hash_files(stamp->dependencies) == stamp->hash) {
            entries.push_back(std::move(entry));
            continue;
        }
        CompileResult result = compile_file(src, with_root);
        entry.rebuilt = true;
        if (!result.ok()) {
            entry.errors = std::move(result.errors);
            entries.push_back(std::move(entry));
            continue;
        }
        write_spirv(entry.output, result.spirv);
        std::ofstream(fs::path(entry.output).replace_extension(".json"), std::ios::trunc)
            << reflect(result.spirv).to_json();
        write_stamp(entry.output, hash_files(result.dependencies), result.dependencies);
        entries.push_back(std::move(entry));
    }
    return entries;
}

}  // namespace vksk
