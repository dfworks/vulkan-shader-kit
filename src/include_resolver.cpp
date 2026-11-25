#include <fstream>
#include <sstream>
#include <stdexcept>

#include "vksk/vksk.hpp"

namespace vksk {
namespace {

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Returns the quoted name if the line is `#include "name"` (whitespace tolerated), else empty.
std::string include_target(const std::string& line) {
    std::size_t i = line.find_first_not_of(" \t");
    if (i == std::string::npos || line[i] != '#') return {};
    i = line.find_first_not_of(" \t", i + 1);
    if (i == std::string::npos || line.compare(i, 7, "include") != 0) return {};
    const std::size_t open = line.find('"', i + 7);
    if (open == std::string::npos) return {};
    const std::size_t close = line.find('"', open + 1);
    if (close == std::string::npos) return {};
    return line.substr(open + 1, close - open - 1);
}

fs::path resolve(const std::string& name, const fs::path& from, const std::vector<fs::path>& dirs) {
    fs::path candidate = from.parent_path() / name;
    if (fs::exists(candidate)) return fs::weakly_canonical(candidate);
    for (const auto& dir : dirs) {
        candidate = dir / name;
        if (fs::exists(candidate)) return fs::weakly_canonical(candidate);
    }
    throw std::runtime_error(from.string() + ": cannot find include \"" + name + "\"");
}

class Expander {
public:
    explicit Expander(const std::vector<fs::path>& dirs) : dirs_(dirs) {}

    void expand(const fs::path& path, std::ostringstream& out) {
        for (const auto& open : stack_) {
            if (open == path) throw std::runtime_error("include cycle through " + path.string());
        }
        stack_.push_back(path);
        const int index = static_cast<int>(result.files.size());
        result.files.push_back(path);

        std::istringstream in(read_text(path));
        std::string line;
        int number = 0;
        bool version_seen = false;
        while (std::getline(in, line)) {
            ++number;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::string target = include_target(line);
            if (!target.empty()) {
                expand(resolve(target, path, dirs_), out);
                out << "#line " << number + 1 << ' ' << index << '\n';
                continue;
            }
            out << line << '\n';
            // #version must be the first statement, so the first #line goes right after it.
            if (!version_seen && line.find("#version") != std::string::npos && stack_.size() == 1) {
                version_seen = true;
                out << "#line " << number + 1 << ' ' << index << '\n';
            }
        }
        stack_.pop_back();
    }

    ExpandedSource result;

private:
    const std::vector<fs::path>& dirs_;
    std::vector<fs::path> stack_;
};

}  // namespace

ExpandedSource expand_includes(const fs::path& path, const std::vector<fs::path>& include_dirs) {
    Expander expander(include_dirs);
    std::ostringstream out;
    expander.expand(fs::weakly_canonical(path), out);
    expander.result.text = out.str();
    return expander.result;
}

}  // namespace vksk
