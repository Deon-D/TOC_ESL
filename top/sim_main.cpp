#include <fstream>
#include <iostream>
#include <string>

#include "system.hpp"
int main(int argc, char** argv) {
    esl::chi_vip::RunResult result;
    esl::SimConfig config;
    try {
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--help") {
                std::cout
                    << "esl_sim --config FILE --requests N --period-ps N --json FILE --trace FILE\n"
                    << "Options: --rn-count --cache-lines --rn-slots --snoop-slots "
                       "--pending-operations\n"
                    << "--hn-slots --sn-slots --sn-latency --timeout-cycles --issue-interval\n"
                    << "--memory-size --start-address --seed --address-mode sequential|random\n"
                    << "--write-percent 0..100 --max-cycles\n"
                    << "--log-level trace|debug|info|warn|error|fatal|off --log-dir DIRECTORY\n"
                    << "--reverse-registration (reverse Work/Xfer traversal for verification)\n";
                return 0;
            }
        }
        config = esl::LoadConfig(argc, argv);
        std::ofstream trace;
        if (!config.trace_path.empty()) {
            trace.open(config.trace_path);
            esl::Require(bool(trace), "cannot open trace output");
            trace.exceptions(std::ios::badbit | std::ios::failbit);
        }
        esl::chi_vip::System system(config, config.trace_path.empty() ? nullptr : &trace);
        result = system.Run();
        if (trace.is_open())
            trace.close();
    } catch (const std::exception& error) {
        result.success = false;
        result.error = error.what();
        result.config = config;
        for (int i = 1; i + 1 < argc; ++i)
            if (std::string(argv[i]) == "--json")
                config.json_path = argv[i + 1];
    }
    std::ofstream output(config.json_path);
    if (!output || !(output << result.JSON()) || !output.flush()) {
        std::cerr << "cannot write JSON result: " << config.json_path << '\n';
        return 2;
    }
    if (!result.success) {
        std::cerr << result.error << '\n';
        return 1;
    }
    std::cout << "Completed " << result.completed << " operations (" << result.loads << " loads, "
              << result.stores << " stores) in " << result.cycles
              << " cycles; throughput=" << result.Throughput() << " operations/cycle\n";
    return 0;
}
