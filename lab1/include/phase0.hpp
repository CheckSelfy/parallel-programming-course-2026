#pragma once
#include <algorithm>
#include <limits>

#include "interface.hpp"

namespace phase0 {
    class MetricCollector final : public MetricsCollectorInterface {
    public:
        MetricCollector() = default;

        void record(uint64_t value) final {
            uint64_t bucket = std::min(value / 4, static_cast<uint64_t>(255));
            buckets_[bucket]++;
            count_++;
            sum_ += value;
            max_ = std::max(max_, value);
            min_ = std::min(min_, value);
        }

        Snapshot snapshot() final {
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

            return result;
        }

    private:
        std::array<uint64_t, 256> buckets_{};
        uint64_t count_{0};
        uint64_t sum_{0};
        uint64_t min_{std::numeric_limits<uint64_t>::max()};
        uint64_t max_{0};
    };
}