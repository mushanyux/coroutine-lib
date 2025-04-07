#pragma once

#include "../fiber/fiber.h"
#include "../thread/thread.h"

#include <mutex>
#include <vector>

namespace mushanyu {
class Scheduler {
public:
    Scheduler(size_t threads = 1, bool use_caller = true, const std::string& name = "Scheduler");
    virtual ~Scheduler();
    const std::string& getName() const {return name_;}
    
    static Scheduler* GetThis();

    template <class FiberOrCb>
    void scheduleLock(FiberOrCb fc, int thread = -1) {
        bool need_tickle;
        {
            std::lock_guard(std::mutex) lock(mutex_);
            need_tickle = tasks_.empty();
            ScheduleTask task(fc, thread);
            if (task.fiber || task.cb) {
                tasks_.push_back(task);
            }
        }
        if (need_tickle) {
            tickle();
        }
    }

    virtual void start();
    virtual void stop();

protected:
    void SetThis();

    virtual void tickle();

    virtual void run();

    virtual void idle();

    virtual bool stopping();

    bool hasIdleThreads() {return idleThreadCount_ > 0;}

private:
    struct ScheduleTask {
        std::shared_ptr<Fiber> fiber;
        std::function<void()> cb;
        int thread;

        ScheduleTask() {
            fiber = nullptr;
            cb = nullptr;
            thread = -1;
        }

        ScheduleTask(std::shared_ptr<Fiber> f, int thr) {
            fiber = f;
            thread = thr;
        }

        ScheduleTask(std::shared_ptr<Fiber>* f, int thr) {
            fiber.swap(*f);
            thread = thr;
        }

        ScheduleTask(std::function<void()> f, int thr) {
            cb = f;
            thread = thr;
        }
        
        ScheduleTask(std::function<void()>* f, int thr) {
            cb.swap(*f);
            thread = thr;
        }

        void reset() {
            fiber = nullptr;
            cb = nullptr;
            thread = -1;
        }
    };
    

    std::string name_;
    std::mutex mutex_;
    std::vector<std::shared_ptr<Thread>> threads_;
    std::vector<ScheduleTask> tasks_;
    std::vector<int> threadIds_;
    size_t threadCount_ = 0;
    std::atomic<size_t> activeThreadCount_ = {0};
    std::atomic<size_t> idleThreadCount_ = {0};
    
    bool useCaller_;
    std::shared_ptr<Fiber> schedulerFiber_;
    int rootThread_ = -1;
    bool stopping_ = false;
};

}