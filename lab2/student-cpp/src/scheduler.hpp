#pragma once
// Этап 1: планировщик, который ведёт настоящие потоки строго по одному
// (передача хода через семафоры). Этап 2: обнаружение deadlock в главном цикле
// и корректное завершение застрявших потоков. Внутреннее устройство (поля,
// семафоры, вспомогательные функции для shared и sched_mutex) придумываете
// сами.
#include <functional>
#include <memory>
#include <mutex>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

#include "strategy.hpp"

namespace sched {

class Scheduler {
  public:
    Scheduler();
    ~Scheduler();
    // Зарегистрировать поток. Потоки получают буквы A, B, C… в порядке вызова
    // spawn.
    void spawn(std::function<void()> body);
    // Запустить потоки и вести их по одному: на каждой паузе спросить
    // strategy.pick(enabled, last).
    void run(Strategy &strategy);
    // Трасса прогона: буквы потоков, выбранных во всех решениях подряд,
    // например "AAAABB".
    std::string trace_string() const;

    // Если true, каждая операция с общими данными добавляет строку в log.
    // Выставляет runner.
    bool logging = false;
    // Лог прогона: строки "<поток>  <операция>", например "A  lock mu", "A
    // balance -> 100".
    std::vector<std::string> log;
    // Пустая строка, если deadlock не было; иначе "held={a:A, b:B}" (этап 2).
    std::string deadlock;

    // Internals
    static Scheduler &current();
    static int currentId();
    void pauseBeforeOp();
    void pointOp();
    void shareTouched();
    void logOp(const std::string &op);
    int registerMutex(const std::string &name);
    void mutexLock(int id);
    void mutexUnlock(int id);
    void awaitCond(std::function<bool()> cond);

  private:
    enum State {
        RUNNING,
        FREE,
        BLOCKED_LOCK,
        BLOCKED_AWAIT,
        FINISHED,
    };
    struct MutexRec {
        std::string name;
        int owner = -1;
    };

    void yieldTurn();
    std::vector<int> enabledListLocked();
    bool anyAliveLocked() const;
    void setDeadlockHeldLocked();
    void abortAndWakeLocked();
    void appendLogLocked(const std::string &op);

    void threadMain(int i, std::function<void()> body);

    std::vector<std::function<void()>> bodies_;
    std::vector<std::thread> threads_;
    std::vector<std::unique_ptr<std::binary_semaphore>> go_; // to workers
    std::counting_semaphore<> yield_{0}; // to scheduler-thread
    std::mutex m_;
    std::vector<State> state_;
    std::vector<bool> touched_;
    std::vector<int> blockedLock_;
    std::vector<std::function<bool()>> awaitCond_;
    std::vector<MutexRec> mutexes_;
    std::string trace_;
    int last_ = -1;
    bool aborted_ = false;
};

// window() из тестовых программ: пауза перед следующей операцией с общими
// данными обязательна.
void point();
// Ждать, пока cond не станет true. До этого поток не попадает в enabled.
void await(std::function<bool()> cond);

} // namespace sched