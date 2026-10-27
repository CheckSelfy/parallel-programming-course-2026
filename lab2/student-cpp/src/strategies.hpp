#pragma once
// Стратегии выбора следующего потока.
// FirstAlive — этап 1, Replay — этап 2, RandomPick — этап 3, Dfs — этап 4
// (ограничение bound — этап 5). enabled — номера потоков, которые могут сделать
// шаг, по возрастанию; last — прошлый выбранный (-1 в начале).
#include <algorithm>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "strategy.hpp"

namespace sched {

struct FirstAlive : Strategy {
    int pick(const std::vector<int> &enabled, int) override {
        return enabled.front();
    }
};

struct Replay : Strategy {
    explicit Replay(std::string trace) : trace_(std::move(trace)) {}
    void begin_run() override { pos_ = 0; }
    int pick(const std::vector<int> &enabled, int) override {
        if (pos_ < trace_.size()) {
            int t = trace_[pos_++] - 'A';
            if (std::find(enabled.begin(), enabled.end(), t) != enabled.end())
                return t;
        }
        return enabled.front();
    }

  private:
    std::string trace_;
    std::size_t pos_ = 0;
};

// Случайный поток: enabled[rnd() % enabled.size()], rnd — std::mt19937(seed).
struct RandomPick : Strategy {
    explicit RandomPick(uint32_t seed) : rnd_(seed) {}
    int pick(const std::vector<int> &enabled, int) override {
        return enabled[rnd_() % enabled.size()];
    }

  private:
    std::mt19937 rnd_;
};

// Stateless DFS по дереву выборов: каждый прогон с нуля, повторяет префикс
// прошлого и берёт следующий вариант. bound — максимум вытеснений (этап 5); для
// dfs runner передаёт INT_MAX.
struct Dfs : Strategy {
    explicit Dfs(int bound) : bound_(bound) {}
    void begin_run() override {
        pickCount_ = 0;
        preemptions_ = 0;
    }
    bool end_run() override {
        for (int f = static_cast<int>(desired_.size()) - 1; f >= 0; --f) {
            auto &cur_opts = options_[f];
            if (cur_opts.empty())
                continue;
            std::size_t cur = 0;
            for (; cur < cur_opts.size(); ++cur)
                if (cur_opts[cur] == desired_[f])
                    break;
            if (cur + 1 < cur_opts.size()) {
                desired_[f] = cur_opts[cur + 1];
                options_.resize(f + 1);
                desired_.resize(f + 1);
                return true;
            }
        }
        return false;
    }

    int pick(const std::vector<int> &enabled, int last) override {
        int f = pickCount_++;
        if (f < static_cast<int>(desired_.size())) {
            if (last != -1 && desired_[f] != last)
                ++preemptions_;
            return desired_[f];
        }
        std::vector<int> opts;
        opts.reserve(enabled.size());
        for (int c : enabled) {
            int add = (last != -1 && c != last) ? 1 : 0;
            if (preemptions_ + add <= bound_)
                opts.push_back(c);
        }
        options_.push_back(opts);
        int choice = opts.empty() ? lastIfEnabled(enabled, last) : opts.front();
        desired_.push_back(choice);
        if (last != -1 && choice != last)
            ++preemptions_;
        return choice;
    }

  private:
    int bound_;
    int pickCount_ = 0;
    int preemptions_ = 0;
    std::vector<std::vector<int>> options_;
    std::vector<int> desired_;

    int lastIfEnabled(const std::vector<int> &enabled, int last) {
        if (last != -1 &&
            std::find(enabled.begin(), enabled.end(), last) != enabled.end())
            return last;
        return enabled.front();
    }
};

} // namespace sched