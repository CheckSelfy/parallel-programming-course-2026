#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

#include "interface.hpp"

namespace phase4_no_dekker {
    struct Buf {
        std::array<uint64_t, 256> buckets{};
        uint64_t count = 0;
        uint64_t sum = 0;
        uint64_t min = std::numeric_limits<uint64_t>::max();
        uint64_t max = 0;

        void clear() {
            buckets.fill(0);
            count = 0;
            sum = 0;
            min = std::numeric_limits<uint64_t>::max();
            max = 0;
        }
    };

    struct alignas(64) ThreadBuffers {
        std::atomic<int> inside{-1};
        std::array<Buf, 2> buf;
    };

    class MetricCollector final : public MetricsCollectorInterface {
    public:
        MetricCollector() = default;

        ThreadBuffers* get_my_buffers() {
            struct TLSSlot {
                uint64_t id = 0;
                ThreadBuffers* buffers = nullptr;
            };
            static thread_local TLSSlot slot;
            if (slot.id != id_) {
                auto s = std::make_unique<ThreadBuffers>();
                ThreadBuffers* raw = s.get();
                {
                    std::lock_guard<std::mutex> g(snap_lock_);
                    all_states_.push_back(std::move(s));
                }
                slot.id = id_;
                slot.buffers = raw;
            }
            return slot.buffers;
        }

        void record(uint64_t value) final {
            ThreadBuffers* my = get_my_buffers();
            int b = active_.load();

            for (volatile int i = 0; i < 2000; ++i) {
                __builtin_ia32_pause(); 
            }

            my->inside.store(b);

            Buf& cur = my->buf[b];
            uint64_t bucket = std::min(value / 4, static_cast<uint64_t>(255));
            cur.buckets[bucket]++;
            cur.count++;
            cur.sum += value;
            cur.min = std::min(cur.min, value);
            cur.max = std::max(cur.max, value);

            my->inside.store(-1);
        }

        Snapshot snapshot() final {
            std::lock_guard<std::mutex> g(snap_lock_);

            int old = active_.load();
            active_.store(1 - old);

            for (auto& s : all_states_) {
                while (s->inside.load() == old) {
                    std::this_thread::yield();
                }
                Buf& frozen = s->buf[old];
                for (size_t i = 0; i < 256; i++) {
                    global_buckets_[i] += frozen.buckets[i];
                }
                global_count_ += frozen.count;
                global_sum_ += frozen.sum;
                global_min_ = std::min(global_min_, frozen.min);
                global_max_ = std::max(global_max_, frozen.max);
                frozen.clear();
            }

            Snapshot result{
                .buckets = global_buckets_,
                .count = global_count_,
                .sum = global_sum_,
                .min = global_min_,
                .max = global_max_,
                .p50 = 0,
                .p99 = 0,
            };

            metrics::fillPercentiles(result);

            return result;
        }

    private:
        static uint64_t next_collector_id() {
            static std::atomic<uint64_t> counter{1};
            return counter.fetch_add(1);
        }
        const uint64_t id_ = next_collector_id();
        std::mutex snap_lock_;
        std::vector<std::unique_ptr<ThreadBuffers>> all_states_;
        std::atomic<int> active_{0};
        std::array<uint64_t, 256> global_buckets_{};
        uint64_t global_count_{0};
        uint64_t global_sum_{0};
        uint64_t global_min_{std::numeric_limits<uint64_t>::max()};
        uint64_t global_max_{0};
    };
}