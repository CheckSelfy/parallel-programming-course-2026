#pragma once
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "interface.hpp"

namespace phase3 {
    struct alignas(64) ThreadState {
        std::array<std::atomic<uint64_t>, 256> buckets{};
        std::atomic<uint64_t> count{0};
        std::atomic<uint64_t> sum{0};
        std::atomic<uint64_t> min{UINT64_MAX};
        std::atomic<uint64_t> max{0};
    };

    inline uint64_t next_collector_id() {
        static std::atomic<uint64_t> counter{1};
        return counter.fetch_add(1);
    }

    class MetricCollector final : public MetricsCollectorInterface {
    public:
        MetricCollector() = default;

        ThreadState* get_my_state() {
            struct TLSSlot {
                uint64_t id = 0;
                ThreadState* state = nullptr;
            };
            static thread_local TLSSlot slot;
            if (slot.id != id_) {
                auto s = std::make_unique<ThreadState>();
                ThreadState* raw = s.get();
                {
                    std::lock_guard<std::mutex> g(list_lock_);
                    all_states_.push_back(std::move(s));
                }
                slot.id = id_;
                slot.state = raw;
            }
            return slot.state;
        }

        void record(uint64_t value) final {
            ThreadState* s = get_my_state();
            uint64_t b = std::min(value / 4, static_cast<uint64_t>(255));

            relaxed_add(s->buckets[b], 1);
            relaxed_add(s->count, 1);
            relaxed_add(s->sum, value);

            if (value < s->min.load(std::memory_order_relaxed)) {
                s->min.store(value, std::memory_order_relaxed);
            }
            if (value > s->max.load(std::memory_order_relaxed)) {
                s->max.store(value, std::memory_order_relaxed);
            }
        }

        Snapshot snapshot() final {
            std::vector<ThreadState*> states;
            {
                std::lock_guard<std::mutex> g(list_lock_);
                states.reserve(all_states_.size());
                for (auto& s : all_states_) {
                    states.push_back(s.get());
                }
            }

            std::array<uint64_t, 256> buckets{};
            uint64_t count = 0;
            uint64_t sum = 0;
            uint64_t min = std::numeric_limits<uint64_t>::max();
            uint64_t max = 0;

            for (auto* s : states) {
                for (size_t i = 0; i < buckets.size(); i++) {
                    buckets[i] += s->buckets[i].load(std::memory_order_relaxed);
                }
                count += s->count.load(std::memory_order_relaxed);
                sum += s->sum.load(std::memory_order_relaxed);
                min = std::min(min, s->min.load(std::memory_order_relaxed));
                max = std::max(max, s->max.load(std::memory_order_relaxed));
            }

            Snapshot result{
                .buckets = buckets,
                .count = count,
                .sum = sum,
                .min = min,
                .max = max,
                .p50 = 0,
                .p99 = 0,
            };

            metrics::fillPercentiles(result);

            return result;
        }

    private:
        static inline void relaxed_add(std::atomic<uint64_t>& c, uint64_t delta) {
            c.store(c.load(std::memory_order_relaxed) + delta, std::memory_order_relaxed);
        }

        const uint64_t id_ = next_collector_id();
        std::mutex list_lock_;
        std::vector<std::unique_ptr<ThreadState>> all_states_;
    };
}