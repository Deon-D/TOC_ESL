#include <atomic>
#include <filesystem>
#include <iostream>
#include <optional>
#include <thread>

namespace toc::toolchainSmoke {

int runSmokeCheck() {
    static_assert(__GNUC__ == 12 && __GNUC_MINOR__ == 2);
    std::atomic<int> result{0};
    std::thread worker([&result] { result.store(42); });
    worker.join();
    const std::optional<int> answer{result.load()};
    const auto fileName = std::filesystem::path("model/SoC/include").filename();
    if (!answer || *answer != 42 || fileName != "include") {
        return 1;
    }
    std::cout << "GCC " << __VERSION__ << ": C++17, libstdc++ and threads OK\n";
    return 0;
}

}  // namespace toc::toolchainSmoke

int main() {
    return toc::toolchainSmoke::runSmokeCheck();
}
