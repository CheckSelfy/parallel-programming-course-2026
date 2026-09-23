#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <latch>
#include <numeric>
#include <thread>
#include <vector>

#include "generator.hpp"
#include "interface.hpp"
#include "emptylock.hpp"
#include "phase0.hpp"
#include "phase1.hpp"
#include "phase2.hpp"
#include "phase3.hpp"
#include "phase4.hpp"

template <typename Collector>
static double run(Collector& c, const std::vector<uint64_t>& values, size_t T, size_t seconds) {
    std::latch start(1);
    std::atomic<bool> stop = false;
    std::vector<uint64_t> ops(T);
    std::vector<std::thread> threads;
    threads.reserve(T);
    for (size_t k = 0; k < T; k++) {
        threads.emplace_back([&, k]() {
            size_t local = 0;
            size_t i = k * 1000;
            start.wait();
            while (!stop) {
                c.record(values[i]);
                local++;
                i++;
                if (i == values.size()) i = 0;
            }
            ops[k] = local;
        });
    }
    auto t0 = std::chrono::steady_clock::now();
    start.count_down();
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    stop = true;
    auto t1 = std::chrono::steady_clock::now();
    for (auto& t : threads) t.join();
    double dur = std::chrono::duration<double>(t1 - t0).count();
    return std::accumulate(ops.begin(), ops.end(), uint64_t{0}) / dur;
}

template <typename Collector>
static double measure(Collector& c, const std::vector<uint64_t>& values, size_t T, size_t seconds) {
    constexpr size_t RUNS = 5;
    std::array<double, RUNS> res;
    run(c, values, T, seconds);
    for (auto& r : res) r = run(c, values, T, seconds);
    c.snapshot();  // keep collector from being optimized out
    std::sort(res.begin(), res.end());
    return res[RUNS / 2];
}

template <typename Collector>
static void bench_all(MetricsCollectorInterface* iface, const std::vector<uint64_t>& values, size_t seconds) {
    auto* c = static_cast<Collector*>(iface);
    for (size_t T : {1, 2, 4, 8, 16}) {
        double ops = measure(*c, values, T, seconds);
        std::cout << T << " " << static_cast<uint64_t>(ops) << "\n";
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: bench <0|1|2|3|4|empty>" << std::endl;
        return 1;
    }
    int phase = std::atoi(argv[1]);
    size_t seconds = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 5;
    auto values = bench::generate();

    std::string label = argv[1];
    if (label == "empty") {
        std::cout << "empty lock (T ops/sec) seconds=" << seconds << "\n";
        emptylock::EmptyLockCollector c;
        bench_all<emptylock::EmptyLockCollector>(&c, values, seconds);
        return 0;
    }

    std::cout << "phase " << phase << " (T ops/sec) seconds=" << seconds << "\n";
    switch (phase) {
        case 0: { phase0::MetricCollector c; bench_all<phase0::MetricCollector>(&c, values, seconds); return 0; }
        case 1: { phase1::MetricCollector c; bench_all<phase1::MetricCollector>(&c, values, seconds); return 0; }
        case 2: { phase2::MetricCollector c; bench_all<phase2::MetricCollector>(&c, values, seconds); return 0; }
        case 3: { phase3::MetricCollector c; bench_all<phase3::MetricCollector>(&c, values, seconds); return 0; }
        case 4: { phase4::MetricCollector c; bench_all<phase4::MetricCollector>(&c, values, seconds); return 0; }
        default: std::cerr << "unknown phase\n"; return 1;
    }
}