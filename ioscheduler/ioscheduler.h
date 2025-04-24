#pragma once

#include "../scheduler/scheduler.h"
#include "../timer/timer.h"

namespace mushanyu {
    class IOManager : public Scheduler, public TimerManager {
    public:
        enum  Event{
            NONE = 0x00,
            READ = 0x01,
            WRITE = 0x04
        };

        IOManager(size_t threads = 1, bool use_caller = false, const std::string &name = "IOManager");
        ~IOManager();

        int addEvent(int fd, Event event, std::function<void()> cb = nullptr);
        bool delEvent(int fd, Event event);
        bool cancelEvent(int fd, Event event);
        bool cancelAll(int fd);

        static IOManager* GetThis();

    private:
        struct FdContext {
            struct EventContext {
                Scheduler *scheduler = nullptr;
                std::shared_ptr<Fiber> fiber;
                std::function<void()> cb;
            };
            
            EventContext read;
            
            EventContext write;
    
            int fd = 0;
    
            Event events = NONE;
            std::mutex mutex;
    
            EventContext& getEventContext(Event event);
            void resetEventContext(EventContext &ctx);
            void triggerEvent(Event event);
        };

        int epfd_ = 0;
        int tickleFds_[2];
        std::atomic<size_t> pendingEventCount_ = 0;
        std::shared_mutex mutex_;
        std::vector<FdContext*> fdContexts_;

    protected:
        void tickle() override;
        bool stopping() override;
        void idle() override;
        void onTimerInsertedAtFront() override;
        void contextResize(size_t size);

    };

}