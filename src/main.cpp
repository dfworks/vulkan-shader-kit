// vksk: build, reflect and watch Vulkan GLSL shaders.
//
//   vksk build  <src-dir> <out-dir> [-I dir]... [-D NAME=VALUE]...   exit 1 if any shader fails
//   vksk watch  <src-dir> <out-dir> [-I dir]... [-D NAME=VALUE]...   rebuild on change until Ctrl+C
//   vksk reflect <file.spv>                                          print the interface as JSON
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>

#include "vksk/vksk.hpp"

namespace {

std::atomic<bool> g_stop{false};

int usage() {
    std::cerr << "usage: vksk build|watch <src-dir> <out-dir> [-I dir] [-D NAME=VALUE]\n"
                 "       vksk reflect <file.spv>\n";
    return 2;
}

bool parse_options(int argc, char** argv, int first, vksk::CompileOptions& options) {
    for (int i = first; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "-I" || arg == "-D") && i + 1 < argc) {
            const std::string value = argv[++i];
            if (arg == "-I") {
                options.include_dirs.emplace_back(value);
            } else {
                const auto eq = value.find('=');
                options.defines.emplace_back(value.substr(0, eq), eq == std::string::npos ? "1" : value.substr(eq + 1));
            }
        } else if (arg == "-g") {
            options.debug_info = true;
        } else {
            std::cerr << "unknown argument: " << arg << '\n';
            return false;
        }
    }
    return true;
}

int report(const std::vector<vksk::BuildEntry>& entries) {
    int failed = 0, built = 0;
    for (const auto& e : entries) {
        for (const auto& d : e.errors) std::cerr << d.format() << '\n';
        failed += !e.errors.empty();
        built += e.rebuilt && e.errors.empty();
    }
    std::cout << entries.size() << " shaders: " << built << " built, " << failed << " failed, "
              << entries.size() - built - failed << " up to date\n";
    return failed ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string command = argv[1];
    try {
        if (command == "reflect") {
            std::ifstream in(argv[2], std::ios::binary);
            if (!in) {
                std::cerr << "cannot open " << argv[2] << '\n';
                return 2;
            }
            std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::vector<std::uint32_t> words(bytes.size() / 4);
            std::memcpy(words.data(), bytes.data(), words.size() * 4);
            std::cout << vksk::reflect(words).to_json();
            return 0;
        }
        if (argc < 4) return usage();
        vksk::CompileOptions options;
        if (!parse_options(argc, argv, 4, options)) return 2;
        if (command == "build") return report(vksk::build_tree(argv[2], argv[3], options));
        if (command == "watch") {
            std::signal(SIGINT, [](int) { g_stop = true; });
            report(vksk::build_tree(argv[2], argv[3], options));
            std::cout << "watching " << argv[2] << " (Ctrl+C to stop)\n";
            vksk::watch_tree(argv[2], argv[3], options, [](const vksk::BuildEntry& e) {
                if (e.errors.empty()) {
                    std::cout << "reloaded " << e.output.string() << '\n';
                } else {
                    for (const auto& d : e.errors) std::cerr << d.format() << '\n';
                }
                std::cout.flush();
            }, [] { return g_stop.load(); });
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return usage();
}
