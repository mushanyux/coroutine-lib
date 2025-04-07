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

    Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_schedule) :
    cb_(cb), runInScheduler_(run_in_schedule) {
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
        if (debug) std::cout << "Fiber(): child id = " << id_ << std::endl;
    }

    Fiber::~Fiber() {
        s_fiber_count --;
        if (stack_) {
            free(stack_);
        }
        if (debug) std::cout << "~!Fiber(): main id = " << id_ << std::endl;
    }

    void Fiber::reset(std::function<void()> cb) {
        assert(stack_ != nullptr && state_ == TERM);
        state_ = READY;
        cb_ = cb;

        if (getcontext(&ctx_)) {
            std::cerr << "reset() failed" << std::endl;
            pthread_exit(NULL);
        }

        ctx_.uc_link = nullptr;
        ctx_.uc_stack.ss_sp = stack_;
        ctx_.uc_stack.ss_size = stacksize_;
        makecontext(&ctx_, &Fiber::MainFunc, 0);
    }

    void Fiber::resume() {
        assert(state_ == READY);
        state_ = RUNNING;
        if (runInScheduler_) {
            SetThis(this);
            if (swapcontext(&(t_scheduler_fiber->ctx_), &ctx_)) {
                std::cerr << "resume() to t_scheduler_fiber failed" << std::endl;
                pthread_exit(NULL);
            }
        } else {
            SetThis(this);
            if (swapcontext(&(t_thread_fiber->ctx_), &ctx_)) {
                std::cerr << "resume() to t_thread_fiber failed" << std::endl;
                pthread_exit(NULL);
            }
        }
    }

    void Fiber::yield() {
        assert(state_ == RUNNING || state_ == TERM);
        if (state_ != TERM) {
            state_ = READY;
        } 
        if (runInScheduler_) {
            SetThis(t_scheduler_fiber);
            if (swapcontext(&ctx_, &(t_thread_fiber->ctx_))) {
                std::cerr << "yield() to t_thread_fiber failed" << std::endl;
                pthread_exit(NULL);
            }
        } else {
            SetThis(t_thread_fiber.get());
            if (swapcontext(&ctx_, &(t_thread_fiber->ctx_))) {
                std::cerr << "resume() to t_thread_fiber failed" << std::endl;
                pthread_exit(NULL);
            }
        }
    }

    void Fiber::MainFunc() {
        std::shared_ptr<Fiber> curr = GetThis();
        assert(curr != nullptr);

        curr->cb_();
        curr->cb_ = nullptr;
        curr->state_ = TERM;
    
        auto raw_ptr = curr.get();
        curr.reset();
        raw_ptr->yield();
    }
}