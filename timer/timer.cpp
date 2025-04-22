#include "timer.h"
#include <assert.h>
#include <shared_mutex>

namespace mushanyu {
    bool Timer::cancel() {
        std::unique_lock<std::mutex> lock(manager_->mutex_);
        if (cb_ == nullptr) {
            return false;
        }
        cb_ = nullptr;
        auto it = manager_->timers_.find(shared_from_this());
        if (it != manager_->timers_.end()) {
            manager_->timers_.erase(it);
        }
        return true;
    }

    bool Timer::refresh() {
        std::unique_lock<std::shared_mutex> lock(manager_->mutex_);
        if (cb_ == nullptr) {
            return false;
        }
        auto it = manager_->timers_.find(shared_from_this());
        if (it == manager_->timers_.end()) {
            return false;
        }
        manager_->timers_.erase(it);
        next_ = std::chrono::system_clock::now() + std::chrono::milliseconds(ms_);
        manager_->timers_.insert(shared_from_this());
        return true;
    }

    bool Timer::reset(uint64_t ms, bool from_now) {
        if (ms == ms_ && !from_now) {
            return true;
        }
        {
            std::unique_lock<std::mutex> write_lock(manager_->mutex_);
            if (cb_ == nullptr) {
                return false;
            }
            auto it = manager_->timers_.find(shared_from_this());
            if (it == manager_->timers_.end()) {
                return false;
            }
            manager_->timers_.erase(it);
        }
        auto start = from_now ? std::chrono::system_clock::now() : next_ - std::chrono::milliseconds(ms_);
        ms_ = ms;
        next_ = start + std::chrono::milliseconds(ms_);
        manager_->addTimer(shared_from_this());
        return true;
    }

    Timer::Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager) : recurring_(recurring), ms_(ms), cb_(cb), manager_(manager) {
        auto now = std::chrono::system_clock::now();
        next_ = now + std::chrono::milliseconds(ms_);
    }

    bool Timer::Comparator::operator()(const std::shared_ptr<Timer>& lhs, const std::shared_ptr<Timer>& rhs) const {
        assert(lhs != nullptr && rhs != nullptr);
        return lhs->next_ > rhs->next_;
    }

    TimerManager::TimerManager() {
        previouseTime_ = std::chrono::system_clock::now();
    }

    TimerManager::~TimerManager() {
    }

    std::shared_ptr<Timer> TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring) {
        std::shared_ptr<Timer> timer(new Timer(ms, cb, recurring, this));
        addTimer(timer);
        return timer;
    }

    static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb) {
        std::shared_ptr<void> tmp = weak_cond.lock();
        if (tmp) {
            cb();
        }
    }

    std::shared_ptr<Timer> TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring) {
        return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
    }

    uint64_t TimerManager::getNextTimer() {
        std::shared_lock<std::shared_mutex> read_lock(mutex_);

        tickled_ = false;

        if (timers_.empty()) {
            return ~0ull;
        }
        auto now = std::chrono::system_clock::now();
        auto time = (*timers_.begin())->next_;
        if (now >= time) {
            return 0;
        }

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(time - now);
        return static_cast<uint64_t>(duration.count());
    }

    void TimerManager::listExpiredCb(std::vector<std::function<void()>>& cbs) {
        auto now = std::chrono::system_clock::now();
        std::unique_lock<std::shared_mutex> write_lock(mutex_);
        bool rollover = detectClockRollover();
        while (!timers_.empty() && rollover || !timers_.empty() && now >= (*timers_.begin())->next_) {
            std::shared_ptr<Timer> tmp = *timers_.begin();
            timers_.erase(timers_.begin());
            cbs.push_back(tmp->cb_);
            if (tmp->recurring_) {
                tmp->next_ = now + std::chrono::milliseconds(tmp->ms_);
                timers_.insert(tmp);
            } else {
                tmp->cb_ = nullptr;
            }
        }
    }

    bool TimerManager::hasTimer() {
        std::shared_lock<std::shared_mutex> read_lock(mutex_);
        return !timers_.empty();
    }

    void TimerManager::addTimer(std::shared_ptr<Timer> timer) {
        bool at_front = false;
        {
            std::unique_lock<std::shared_mutex> write_lock(mutex_);
            auto it = timers_.insert(timer).first;
            at_front = (it == timers_.begin() && !tickled_);
            if (at_front) {
                tickled_ = true;
            }
        }
        if (at_front) {
            onTimerInsertedAtFront();
        }
    }

    bool TimerManager::detectClockRollover() {
        bool rollover = false;
        auto now = std::chrono::system_clock::now();
        if (now < (previouseTime_ - std::chrono::seconds(60 * 60 * 1000))) {
            rollover = true;
        }
        previouseTime_ = now;
        return rollover;
    }
} 
