#pragma once

#include <iostream>
#include <memory>
#include <atomic>
#include <functional>
#include <cassert>
#include <ucontext.h>
#include <unistd.h>
#include <mutex>

static bool debug = false;

namespace mushanyu {
class Fiber : public std::enable_shared_from_this<Fiber> {
public:
    // 协程状态
    enum State{
        READY,
        RUNNING,
        TERM
    };
    
    Fiber(std::function<void()> cb, size_t stacksize = 0, bool run_in_schedule = true);
    ~Fiber();
    
    void reset(std::function<void()> cb);
    void resume();
    void yield();

    uint64_t getId() const {return id_;}
    State getState() const {return state_;};

    static void SetThis(Fiber* f);
    static std::shared_ptr<Fiber> GetThis();
    static void SetSchedulerFiber(Fiber *f);
    static uint64_t GetFiberId();
    static void MainFunc();

    std::mutex mtx_;
private:
    Fiber();

    uint64_t id_ = 0;
    uint32_t stacksize_ = 0;
    State state_ = READY;
    ucontext_t ctx_;
    void* stack_ = nullptr;
    std::function<void()> cb_;
    bool runInScheduler_;
};


    
}