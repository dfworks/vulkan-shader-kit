#include <chrono>
#include <thread>

#include "vksk/vksk.hpp"

namespace vksk {

void watch_tree(const fs::path& src_dir, const fs::path& out_dir, const CompileOptions& options,
                const std::function<void(const BuildEntry&)>& on_reload,
                const std::function<bool()>& should_stop, int poll_ms) {
    // The build cache already knows what changed (stamps hash every dependency), so a watch is
    // just a cheap rebuild on a timer that reports the entries it actually recompiled.
    while (!should_stop()) {
        for (const auto& entry : build_tree(src_dir, out_dir, options)) {
            if (entry.rebuilt) on_reload(entry);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
    }
}

}  // namespace vksk
