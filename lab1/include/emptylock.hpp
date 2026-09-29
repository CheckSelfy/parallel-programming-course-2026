#pragma once
#include <cstdint>
#include <mutex>

#include "interface.hpp"

namespace emptylock {
    class EmptyLockCollector final : public MetricsCollectorInterface {
    public:
        EmptyLockCollector() = default;

        void record(uint64_t value) final {
            std::lock_guard<std::mutex> lock(mtx_);
            (void)value;
        }

        Snapshot snapshot() final {
            return Snapshot{};
        }

    private:
        std::mutex mtx_;
    };
}