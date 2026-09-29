#include <chrono>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include "generator.hpp"
#include "interface.hpp"
#include "phase2.hpp"
#include "phase3.hpp"
#include "phase4.hpp"
#include "phase4_no_dekker.hpp"

static uint64_t buckets_sum(const Snapshot& s) {
    return std::accumulate(s.buckets.begin(), s.buckets.end(), uint64_t{0});
}

int run_stress(MetricsCollectorInterface& collector, const std::vector<uint64_t>& values,
               size_t writers, size_t iterations, size_t attempts, size_t M) {
    std::cout << "  writers=" << writers << "  snapshots=" << iterations
              << "  attempts=" << attempts << "  per-writer calls=" << M << "\n";

    uint64_t total_expected = 0;
    for (size_t run = 0; run < attempts; run++) {
        std::vector<uint64_t> per_writer(writers, 0);
        std::vector<std::thread> threads;
        threads.reserve(writers);
        for (size_t k = 0; k < writers; k++) {
            threads.emplace_back([&, k]() {
                size_t local = 0;
                size_t i = k * 1000;
                for (size_t n = 0; n < M; n++) {
                    collector.record(values[i % values.size()]);
                    local++;
                    i++;
                }
                per_writer[k] = local;
            });
        }

        uint64_t broken = 0, buckets_lt = 0, buckets_gt = 0;
        for (size_t it = 0; it < iterations; it++) {
            auto s = collector.snapshot();
            uint64_t bs = buckets_sum(s);
            if (bs != s.count) {
                broken++;
                if (bs < s.count) buckets_lt++;
                else buckets_gt++;
            }
        }

        for (auto& t : threads) t.join();

        uint64_t expected = std::accumulate(per_writer.begin(), per_writer.end(), uint64_t{0});
        total_expected += expected;
        auto final_ = collector.snapshot();

        double pct = 100.0 * static_cast<double>(broken) / static_cast<double>(iterations);
        std::cout << "  run " << run
                  << "  broken=" << broken << "/" << iterations << " (" << pct << " %)"
                  << "  lt=" << buckets_lt << " gt=" << buckets_gt
                  << "  expected(cum)=" << total_expected
                  << "  finalCount=" << final_.count
                  << "  diff=" << static_cast<int64_t>(final_.count) - static_cast<int64_t>(total_expected) << "\n";
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: stress <2|3|4|4-no-dekker> [writers=4] [snapshots=10000] [attempts=3]\n";
        return 1;
    }
    std::string phase = argv[1];
    size_t writers = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 4;
    size_t iterations = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 10000;
    size_t attempts = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 3;

    auto values = bench::generate();
    std::cout << "Consistency stress test, phase " << phase << "\n";

    if (phase == "2") {
        phase2::MetricCollector c;
        return run_stress(c, values, writers, iterations, 1, 1'000'000);
    }
    if (phase == "3") {
        phase3::MetricCollector c;
        return run_stress(c, values, writers, iterations, 1, 1'000'000);
    }
    if (phase == "4") {
        phase4::MetricCollector c;
        return run_stress(c, values, writers, iterations, 1, 1'000'000);
    }
    if (phase == "4-no-dekker") {
        phase4_no_dekker::MetricCollector c;
        return run_stress(c, values, writers, iterations, attempts, 20'000);
    }

    std::cerr << "unknown phase\n";
    return 1;
}