#pragma once
#include <array>
#include <cstdint>

struct Snapshot {
    std::array<uint64_t, 256> buckets;
    uint64_t count;
    uint64_t sum;
    uint64_t min;
    uint64_t max;
    uint64_t p50;
    uint64_t p99;
};

class MetricsCollectorInterface {
public:
    virtual ~MetricsCollectorInterface() = default;
    virtual void record(uint64_t value) = 0;
    virtual Snapshot snapshot() = 0;
};

namespace metrics {
    inline uint64_t get_percentile(const std::array<uint64_t, 256>& buckets,
                                      uint64_t count,
                                      uint64_t numerator,
                                      uint64_t denominator) {
    const uint64_t threshold = (count * numerator + denominator - 1) / denominator;
    uint64_t cumulative = 0;
    for (std::size_t i = 0; i < buckets.size(); ++i) {
        cumulative += buckets[i];
        if (cumulative >= threshold) {
            return static_cast<uint64_t>(i) * 4;
        }
    }
    return static_cast<uint64_t>(buckets.size() - 1) * 4;
}

inline std::pair<uint64_t, uint64_t> computePercentiles(
        const std::array<uint64_t, 256>& buckets, uint64_t count) {
    return {
        get_percentile(buckets, count, 50, 100),
        get_percentile(buckets, count, 99, 100),
    };
}

inline void fillPercentiles(Snapshot& s) {
    auto [p50, p99] = computePercentiles(s.buckets, s.count);
    s.p50 = p50;
    s.p99 = p99;
}
}
