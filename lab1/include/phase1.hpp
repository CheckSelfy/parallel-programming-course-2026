#pragma once
#include <algorithm>
#include <mutex>

#include "interface.hpp"

namespace phase1 {
    class MetricCollector final : public MetricsCollectorInterface {
    public:
        MetricCollector() = default;

        void record(uint64_t value) final {
            mtx_.lock();
            uint64_t bucket = std::min(value / 4, static_cast<uint64_t>(255));
            buckets_[bucket]++;
            count_++;
            sum_ += value;
            max_ = std::max(max_, value);
            min_ = std::min(min_, value);
            mtx_.unlock();
        }

        Snapshot snapshot() final {
            mtx_.lock();
            Snapshot result{
                .buckets = buckets_,
                .count = count_,
                .sum = sum_,
                .min = min_,
                .max = max_,
                .p50 = 0, 
                .p99 = 0,
            };

            metrics::fillPercentiles(result);

            mtx_.unlock();
            return result;
        }

    private:
        std::mutex mtx_;
        std::array<uint64_t, 256> buckets_{};
        uint64_t count_{0};
        uint64_t sum_{0};
        uint64_t min_{std::numeric_limits<uint64_t>::max()};
        uint64_t max_{0};
    };
}