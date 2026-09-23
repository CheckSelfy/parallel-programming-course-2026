#pragma once
#include <algorithm>
#include <atomic>
#include <mutex>

#include "interface.hpp"

namespace phase2 {
    class MetricCollector final : public MetricsCollectorInterface {
    public:
        MetricCollector() = default;

        void record(uint64_t value) final {
            uint64_t bucket = std::min(value / 4, static_cast<uint64_t>(255));
            auto& mutex = mutexes_[bucket % 16];
            mutex.lock();
            buckets_[bucket]++;
            mutex.unlock();

            auto count = count_.load();
            while (!count_.compare_exchange_weak(count, count + 1)) {
            }

            auto sum = sum_.load();
            while (!sum_.compare_exchange_weak(sum, sum + value)) {
            }

            auto max = max_.load();
            while (!max_.compare_exchange_weak(max, std::max(max, value))) {
            }

            auto min = min_.load();
            while (!min_.compare_exchange_weak(min, std::min(min, value))) {
            }
        }

        Snapshot snapshot() final {
            std::array<uint64_t, 256> buckets;
            for (size_t mtx_idx = 0; mtx_idx < 16; mtx_idx++) {
                mutexes_[mtx_idx].lock();
                for (size_t b = mtx_idx; b < buckets.size(); b += 16) {
                    buckets[b] = buckets_[b];
                }
                mutexes_[mtx_idx].unlock();
            }
            Snapshot result{
                .buckets = buckets,
                .count = count_,
                .sum = sum_,
                .min = min_,
                .max = max_,
                .p50 = 0, 
                .p99 = 0,
            };

            metrics::fillPercentiles(result);

            return result;
        }

    private:
        std::array<std::mutex, 16> mutexes_{};
        std::array<uint64_t, 256> buckets_{};
        std::atomic<uint64_t> count_{0};
        std::atomic<uint64_t> sum_{0};
        std::atomic<uint64_t> min_{std::numeric_limits<uint64_t>::max()};
        std::atomic<uint64_t> max_{0};
    };
}