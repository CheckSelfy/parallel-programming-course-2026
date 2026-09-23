#pragma once

#include "interface.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <numeric>
#include <thread>
#include <cstdint>
#include <random>
#include <vector>
#include <latch>

namespace bench {
    inline std::vector<uint64_t> generate() {
        std::array<double, 1023> weights;
        for (size_t k = 1; k < 1024; k++) {
            weights[k - 1] = 1.0 / std::pow(k, 1.15);
        }

        std::mt19937_64 gen{42};
        std::discrete_distribution<uint64_t> dist(weights.begin(), weights.end());

        std::vector<uint64_t> result(1 << 20);
        for (size_t i = 0; i < (1 << 20); i++) {
            result[i] = dist(gen) + 1;
        }

        return result;
    }

    inline double run(MetricsCollectorInterface& collector, const std::vector<uint64_t>& values, size_t T, size_t seconds) {
        std::latch start(1);
        std::atomic<bool> stop = false;
        std::vector<uint64_t> ops(T);
        std::vector<std::thread> threads(T);

        auto thread_job = [&](size_t k) {
            size_t local_count = 0;
            size_t i = k * 1000;
            start.wait();
            while (!stop) {
                collector.record(values[i]);
                local_count++;
                i++;
                if (i == values.size()) {
                    i = 0;
                }
            }
            ops[k] = local_count;
        };

        for (size_t k = 0 ; k < T; k++) {
            threads[k] = std::move(std::thread(thread_job, k));
        }

        auto t0 = std::chrono::steady_clock::now();
        start.count_down();
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        stop = true;
        auto t1 = std::chrono::steady_clock::now();
        for (auto&& t: threads) {
            t.join();
        }

        auto duration = std::chrono::duration<double>(t1 - t0);
        return static_cast<double>(std::accumulate(ops.begin(), ops.end(), static_cast<uint64_t>(0))) / duration.count();
    }

    inline double measurePoint(MetricsCollectorInterface& collector, const std::vector<uint64_t>& values, size_t T) {
        constexpr size_t RUNS = 5;
        std::array<double, RUNS> results;

        run(collector, values, T, 5);
        for (size_t i = 0; i < RUNS; i++) {
            results[i] = run(collector, values, T, 5);
        }
        std::cout << collector.snapshot().count << std::endl;

        std::sort(results.begin(), results.end());
        return results[RUNS / 2];
    }
};