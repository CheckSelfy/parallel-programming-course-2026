#include <cstdio>
// Этап 1–2: реализация Scheduler, point() и await().
#include "scheduler.hpp"

#include <stdexcept>

namespace sched {

namespace {
thread_local Scheduler *tls_current = nullptr;
thread_local int tls_id = -1;
struct AbortException {};
} // namespace

Scheduler::Scheduler() {}

Scheduler::~Scheduler() {
    for (auto &t : threads_)
        if (t.joinable())
            t.join();
}

Scheduler &Scheduler::current() { return *tls_current; }
int Scheduler::currentId() { return tls_id; }

void Scheduler::spawn(std::function<void()> body) {
    bodies_.push_back(std::move(body));
}

void Scheduler::threadMain(int i, std::function<void()> body) {
    tls_current = this;
    tls_id = i;
    {
        std::lock_guard<std::mutex> lk(m_);
        touched_[i] = false;
        state_[i] = FREE;
    }
    yield_.release();
    go_[i]->acquire();
    if (!aborted_) {
        try {
            body();
        } catch (const AbortException &) {
            // nah, skip, we aborted it
        }
    }
    {
        std::lock_guard<std::mutex> lk(m_);
        state_[i] = FINISHED;
        touched_[i] = false;
    }
    yield_.release();
}

void Scheduler::run(Strategy &strategy) {
    const int n = static_cast<int>(bodies_.size());
    state_.assign(n, RUNNING);
    touched_.assign(n, false);
    blockedLock_.assign(n, -1);
    awaitCond_.assign(n, nullptr);
    go_.clear();
    for (int i = 0; i < n; ++i)
        go_.push_back(std::make_unique<std::binary_semaphore>(0));
    threads_.clear();
    for (int i = 0; i < n; ++i)
        threads_.emplace_back([this, i]() mutable {
            threadMain(i, std::move(bodies_[static_cast<int>(i)]));
        });

    for (int i = 0; i < n; ++i)
        yield_.acquire();

    trace_.clear();
    deadlock.clear();
    last_ = -1;

    for (;;) {
        std::vector<int> enabled;
        bool allDone = false;
        {
            std::lock_guard<std::mutex> lk(m_);
            enabled = enabledListLocked();
            if (enabled.empty())
                allDone = !anyAliveLocked();
        }
        if (enabled.empty()) {
            if (allDone)
                break;
            // nobody to pick, but not finished - deadlock
            {
                std::lock_guard<std::mutex> lk(m_);
                setDeadlockHeldLocked();
                abortAndWakeLocked();
            }
            break;
        }
        int t = strategy.pick(enabled, last_);
        if (t < 0 || t >= static_cast<int>(state_.size())) {
            // std::fprintf(stderr, "BAD pick t=%d n=%d\n", t, (int)state_.size());
            std::abort();
        }
        last_ = t;
        trace_ += static_cast<char>('A' + t);
        {
            std::lock_guard<std::mutex> lk(m_);
            state_[t] = RUNNING;
        }
        go_[t]->release();
        yield_.acquire();
        {
            std::lock_guard<std::mutex> lk(m_);
            // check if not preemption for correct dfs counting
            if (state_[t] == FINISHED || state_[t] == BLOCKED_AWAIT)
                last_ = -1;
        }
    }

    for (auto &t : threads_)
        if (t.joinable())
            t.join();
}

std::string Scheduler::trace_string() const { return trace_; }

void point() { Scheduler::current().pointOp(); }

void await(std::function<bool()> cond) {
    Scheduler::current().awaitCond(std::move(cond));
}

bool Scheduler::anyAliveLocked() const {
    for (auto st : state_)
        if (st != FINISHED)
            return true;
    return false;
}

void Scheduler::appendLogLocked(const std::string &op) {
    if (!logging)
        return;
    char c = static_cast<char>('A' + currentId());
    std::string line;
    line.reserve(3 + op.size());
    line += c;
    line += "  ";
    line += op;
    log.push_back(std::move(line));
}

void Scheduler::logOp(const std::string &op) {
    std::lock_guard<std::mutex> lk(m_);
    appendLogLocked(op);
}

void Scheduler::yieldTurn() {
    int me = currentId();
    {
        std::lock_guard<std::mutex> lk(m_);
        touched_[me] = false;
        state_[me] = FREE;
    }
    yield_.release();
    go_[me]->acquire();
    if (aborted_)
        throw AbortException{};
}

void Scheduler::pauseBeforeOp() {
    int me = currentId();
    {
        std::lock_guard<std::mutex> lk(m_);
        if (!touched_[me])
            return;
    }
    yieldTurn();
}

void Scheduler::shareTouched() {
    std::lock_guard<std::mutex> lk(m_);
    touched_[currentId()] = true;
}

void Scheduler::pointOp() {
    shareTouched();
    logOp("window");
}

int Scheduler::registerMutex(const std::string &name) {
    std::lock_guard<std::mutex> lk(m_);
    mutexes_.push_back(MutexRec{name, -1});
    return static_cast<int>(mutexes_.size() - 1);
}

void Scheduler::mutexLock(int id) {
    int me = currentId();
    for (;;) {
        {
            std::lock_guard<std::mutex> lk(m_);
            // Пауза перед локом: если поток трогал состояние, или лок занят —
            // уходим с хода как BLOCKED_LOCK. (в отличие от FREE: ожидающий лок
            // не должен считаться готовым, пока лок занят)
            if (!touched_[me] && mutexes_[id].owner == -1) {
                mutexes_[id].owner = me;
                touched_[me] = true;
                appendLogLocked("lock " + mutexes_[id].name);
                return;
            }
            touched_[me] = false;
            state_[me] = BLOCKED_LOCK;
            blockedLock_[me] = id;
        }
        yield_.release();
        go_[me]->acquire();
        if (aborted_)
            throw AbortException{};
    }
}

void Scheduler::mutexUnlock(int id) {
    if (aborted_)
        return;
    pauseBeforeOp();
    std::lock_guard<std::mutex> lk(m_);
    mutexes_[id].owner = -1;
    touched_[currentId()] = true;
    appendLogLocked("unlock " + mutexes_[id].name);
}

void Scheduler::awaitCond(std::function<bool()> cond) {
    int me = currentId();
    {
        std::lock_guard<std::mutex> lk(m_);
        awaitCond_[me] = cond;
    }
    bool entryPauseDone = false;
    for (;;) {
        bool satisfied = cond();
        bool mustPause;
        {
            std::lock_guard<std::mutex> lk(m_);
            mustPause = touched_[me];
        }
        if (satisfied) {
            int need = entryPauseDone ? 0 : (mustPause ? 1 : 0);
            if (need > 0) {
                entryPauseDone = true;
                yieldTurn();
                continue;
            }
            std::lock_guard<std::mutex> lk(m_);
            touched_[me] = true;
            awaitCond_[me] = nullptr;
            appendLogLocked("await done");
            return;
        }
        {
            std::lock_guard<std::mutex> lk(m_);
            touched_[me] = false;
            state_[me] = BLOCKED_AWAIT;
        }
        yield_.release();
        go_[me]->acquire();
        if (aborted_)
            throw AbortException{};
    }
}

std::vector<int> Scheduler::enabledListLocked() {
    std::vector<int> res;
    for (int i = 0; i < static_cast<int>(state_.size()); ++i) {
        switch (state_[i]) {
        case FREE:
            res.push_back(i);
            break;
        case BLOCKED_AWAIT:
            if (awaitCond_[i] && awaitCond_[i]()) {
                state_[i] = FREE;
                res.push_back(i);
            }
            break;
        case BLOCKED_LOCK:
            if (blockedLock_[i] >= 0 && mutexes_[blockedLock_[i]].owner == -1)
                res.push_back(i);
            break;
        case RUNNING:
        case FINISHED:
            break;
        }
    }
    return res;
}

void Scheduler::setDeadlockHeldLocked() {
    std::string s = "held={";
    bool first = true;
    for (auto &mu : mutexes_) {
        if (mu.owner == -1)
            continue;
        if (!first)
            s += ", ";
        s += mu.name;
        s += ":";
        s += static_cast<char>('A' + mu.owner);
        first = false;
    }
    s += "}";
    deadlock = std::move(s);
}

void Scheduler::abortAndWakeLocked() {
    aborted_ = true;
    for (int i = 0; i < static_cast<int>(state_.size()); ++i) {
        if (state_[i] == FINISHED)
            continue;
        state_[i] = RUNNING;
        go_[i]->release();
    }
}

} // namespace sched