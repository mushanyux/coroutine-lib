#include "fiber.h"

static bool debug = false;

namespace mushanyu {
    // 正在运行的协程
    static thread_local Fiber* t_fiber = nullptr;
    // 主协程
    static thread_local std::shared_ptr<Fiber> t_thread_fiber = nullptr;
    // 调度协程
    static thread_local Fiber* t_scheduler_fiber = nullptr;
    // 协程计数器
    static std::atomic<uint64_t> s_fiber_count{0};
    // 协程id
    static std::atomic<uint64_t> s_fiber_id{0};

    void Fiber::SetThis(Fiber* f) {
        t_fiber = f;
    }

    std::shared_ptr<Fiber> Fiber::GetThis() {
        if (t_fiber) {
            return t_fiber->shared_from_this();
        }
        std::shared_ptr<Fiber> main_fiber(new Fiber());
        t_thread_fiber = main_fiber;
        t_scheduler_fiber = main_fiber.get();

        assert(t_fiber == main_fiber.get());
        return t_fiber->shared_from_this();
    }

    void Fiber::SetSchedulerFiber(Fiber* f) {
        t_scheduler_fiber = f;
    }

    uint64_t Fiber::GetFiberId() {
        if (t_fiber) {
            return t_fiber->getId();
        }
        return (uint64_t)-1;
    }

    Fiber::Fiber() {
        SetThis(this);
        state_ = RUNNING;

        if (getcontext(&ctx_)) {
            std::cerr << "Fiber() failed" << std::endl;
            pthread_exit(NULL);
        }
        id_ = s_fiber_id ++;
        s_fiber_count ++;
        if (debug) std::cout << "Fiber(): main id = " << id_ << std::endl;
    }

    Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_schedule) 
    : cb_(cb), runInScheduler_(run_in_schedule) {
        state_ = READY;
        
        stacksize_ = stacksize ? stacksize : 128000;
        stack_ = malloc(stacksize_);

        if (getcontext(&ctx_)) {
            std::cerr << "Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler) failed" << std::endl;
		    pthread_exit(NULL);
        }

        ctx_.uc_link = nullptr;
        ctx_.uc_stack.ss_sp = stack_;
        ctx_.uc_stack.ss_size = stacksize_;
        makecontext(&ctx_, &Fiber::MainFunc, 0);

        id_ = s_fiber_id ++;
        s_fiber_count ++;
        if (debug) std::cout << "Fiber(): main id = " << id_ << std::endl;
    }
}