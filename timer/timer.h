#pragma once

#include <memory>
#include <mutex>
#include <vector>
#include <set>
#include <functional>
#include <shared_mutex>

namespace mushanyu {

    class TimerManager;

    class Timer : public std::enable_shared_from_this<Timer>{
        friend class TimerManager;
    public:
        bool cancel();
        bool refresh();
        bool reset(uint64_t ms, bool from_now);

    private:
        Timer(u_int64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager);
        bool recurring_ = false;
        uint64_t ms_ = 0;
        TimerManager* manager_ = nullptr;
        std::function<void()> cb_;
        std::chrono::time_point<std::chrono::system_clock> next_;

        struct Comparator {
            bool operator()(const std::shared_ptr<Timer>& lhs, const std::shared_ptr<Timer>& rhs) const;
        };
    };

    class TimerManager {
        friend class Timer;
    public:
        TimerManager();
        virtual ~TimerManager();

        std::shared_ptr<Timer> addTimer(uint64_t ms, std::function<void()> cb, bool recurring = false);
        
        std::shared_ptr<Timer> addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring = false);

        uint64_t getNextTimer();
        
        void listExpiredCb(std::vector<std::function<void()>>& cbs);

        bool hasTimer();

    protected:
        void onTimerInsertedAtFront();

        void addTimer(std::shared_ptr<Timer> timer);

    private:
        bool detectClockRollover();

        std::shared_mutex mutex_;

        std::set<std::shared_ptr<Timer>, Timer::Comparator> timers_;

        bool tickled_ = false;

        std::chrono::time_point<std::chrono::system_clock> previouseTime_;
    };
}