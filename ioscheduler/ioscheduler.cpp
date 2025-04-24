#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <cstring>

#include "ioscheduler.h"

static bool debnug = true;

namespace mushanyu {
    IOManager* IOManager::GetThis() {
        return dynamic_cast<IOManager*>(Scheduler::GetThis());
    }

    IOManager::FdContext::EventContext& IOManager::FdContext::getEventContext(Event event) {
        assert(event == READ || event == WRITE);
        switch (event) {
        case READ:
            return read;
        case WRITE:
            return write;
        }
        throw std::invalid_argument("unsupported event type");
    }

    void IOManager::FdContext::resetEventContext(EventContext &ctx) {
        ctx.scheduler = nullptr;
        ctx.fiber.reset();
        ctx.cb = nullptr;
    }

    void IOManager::FdContext::triggerEvent(IOManager::Event event) {
        assert(events & event);
        events = (Event)(events & ~event);

        EventContext& ctx = getEventContext(event);
        if (ctx.cb) {
            ctx.scheduler->scheduleLock(&ctx.cb);
        } else {
            ctx.scheduler->scheduleLock(&ctx.fiber);
        }

        resetEventContext(ctx);
    }

    IOManager::IOManager(size_t threads, bool use_caller, const std::string& name)
        : Scheduler(threads, use_caller, name), TimerManager() {
            epfd_ = epoll_create(5000);
            assert(epfd_ > 0);
            int rt = pipe(tickleFds_);
            assert(!rt);

            epoll_event event{};
            event.events = EPOLLIN | EPOLLET;
            event.data.fd = tickleFds_[0];
            rt = fcntl(tickleFds_[0], F_SETFL, O_NONBLOCK);
            assert(!rt);

            rt = epoll_ctl(epfd_, EPOLL_CTL_ADD, tickleFds_[0], &event);
            assert(!rt);

            contextResize(32);

            start();
    }

    IOManager::~IOManager() {
        stop();
        close(epfd_);
        close(tickleFds_[0]);
        close(tickleFds_[1]);

        for (size_t i = 0; i < fdContexts_.size(); ++i) {
            if (fdContexts_[i]) {
                delete fdContexts_[i];
            }
        }
    }

    void IOManager::contextResize(size_t size) {
        fdContexts_.resize(size);
        for (size_t i = 0; i < fdContexts_.size(); ++i) {
            if (!fdContexts_[i]) {
                fdContexts_[i] = new FdContext;
                fdContexts_[i]->fd = i;
            }
        }
    }

    int IOManager::addEvent(int fd, Event event, std::function<void()> cb) {
        FdContext* fd_ctx = nullptr;

        std::shared_lock<std::shared_mutex> read_lock(mutex_);
        if ((int) fdContexts_.size() > fd) {
            fd_ctx = fdContexts_[fd];
            read_lock.unlock();
        } else {
            read_lock.unlock();
            std::unique_lock<std::shared_mutex> write_lock(mutex_);
            contextResize(fd * 1.5);
            fd_ctx = fdContexts_[fd];
        }

        std::lock_guard<std::mutex> lock(fd_ctx->mutex);
        if (fd_ctx->events & event) {
            return -1;
        }
        int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
        epoll_event epevent{};
        epevent.events = EPOLLET | fd_ctx->events | event;
        epevent.data.ptr = fd_ctx;
        
        int rt = epoll_ctl(epfd_, op, fd, &epevent);
        if (rt) {
            std::cerr << "addEvent::epoll_ctl failed: " << strerror(errno) << std::endl;
            return -1;
        }

        ++ pendingEventCount_;

        fd_ctx->events = (Event)(fd_ctx->events | event);

        FdContext::EventContext& event_ctx = fd_ctx->getEventContext(event);
        event_ctx.scheduler = Scheduler::GetThis();
        if (cb) {
            event_ctx.cb.swap(cb);
        } else {
            event_ctx.fiber = Fiber::GetThis();
            assert(event_ctx.fiber->getState() == Fiber::State::RUNNING);
        }
        return 0;
    }

    bool IOManager::delEvent(int fd, Event event) {
        FdContext* fd_ctx = nullptr;
        std::shared_lock<std::shared_mutex> read_lock(mutex_);
        if ((int) fdContexts_.size() > fd) {
            fd_ctx = fdContexts_[fd];
            read_lock.unlock();
        } else {
            read_lock.unlock();
            return false;
        }
        std::lock_guard<std::mutex> lock(fd_ctx->mutex);
        if (!(fd_ctx->events & event)) {
            return false;
        }
        Event new_events = (Event)(fd_ctx->events & ~event);
        int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
        epoll_event epevent{};
        epevent.events = EPOLLET | new_events;
        epevent.data.ptr = fd_ctx;
        int rt = epoll_ctl(epfd_, op, fd, &epevent);
        if (rt) {
            std::cerr << "delEvent::epoll_ctl failed: " << strerror(errno) << std::endl;
            return false;
        }
        -- pendingEventCount_;
        fd_ctx->events = new_events;
        FdContext::EventContext& event_ctx = fd_ctx->getEventContext(event);
        fd_ctx->resetEventContext(event_ctx);
        return true;
    }

    bool IOManager::cancelEvent(int fd, Event event) {
        FdContext* fd_ctx = nullptr;
        std::shared_lock<std::shared_mutex> read_lock(mutex_);
        if ((int) fdContexts_.size() > fd) {
            fd_ctx = fdContexts_[fd];
            read_lock.unlock();
        } else {
            read_lock.unlock();
            return false;
        }
        std::lock_guard<std::mutex> lock(fd_ctx->mutex);
        if (!(fd_ctx->events & event)) {
            return false;
        }
        Event new_events = (Event)(fd_ctx->events & ~event);
        int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
        epoll_event epevent{};
        epevent.events = EPOLLET | new_events;
        epevent.data.ptr = fd_ctx;
        int rt = epoll_ctl(epfd_, op, fd, &epevent);
        if (rt) {
            std::cerr << "cancelEvent::epoll_ctl failed: " << strerror(errno) << std::endl;
            return false;
        }
        -- pendingEventCount_;
        fd_ctx->events = new_events;
        return true;
    }

    bool IOManager::cancelAll(int fd) {
        FdContext* fd_ctx = nullptr;
        std::shared_lock<std::shared_mutex> read_lock(mutex_);
        if ((int) fdContexts_.size() > fd) {
            fd_ctx = fdContexts_[fd];
            read_lock.unlock();
        } else {
            read_lock.unlock();
            return false;
        }
        std::lock_guard<std::mutex> lock(fd_ctx->mutex);
        if (!fd_ctx->events) {
            return false;
        }
        int op = EPOLL_CTL_DEL;
        epoll_event epevent{};
        epevent.events = 0;
        epevent.data.ptr = fd_ctx;

        int rt = epoll_ctl(epfd_, op, fd, &epevent);
        if (rt) {
            std::cerr << "IOManager::epoll_ctl failed: " << strerror(errno) << std::endl;
            return false;
        }
        if (fd_ctx->events & Event::READ) {
            fd_ctx->triggerEvent(Event::READ);
            -- pendingEventCount_;
        }
        if (fd_ctx->events & Event::WRITE) {
            fd_ctx->triggerEvent(Event::WRITE);
            -- pendingEventCount_;
        }
        assert(fd_ctx->events == 0);
        return true;
    }

    void IOManager::tickle() {
        if (hasIdleThreads()) {
            return;
        }
        int rt = write(tickleFds_[1], "T", 1);
        assert(rt == 1);
    }

    bool IOManager::stopping() {
        uint64_t timeout = getNextTimer();
        return timeout == ~0ull && pendingEventCount_ == 0 && Scheduler::stopping();
    }

    void IOManager::idle() {
        static const uint64_t MAX_EVENTS = 256;
        std::unique_ptr<epoll_event[]> events(new epoll_event[MAX_EVENTS]);
        while (true) {
            if (debug) {
                std::cout << "IOManager::idle() run in thread " << Thread::GetThreadId() << std::endl;
            }

            if (stopping()) {
                if (debug) std::cout << "name = " << getName() << " IOManager::idle() exits in thread " << Thread::GetThreadId() << " stopping" << std::endl;
                break;
            }

            int rt = 0;
            while (true) {
                static const uint64_t MAX_TIMEOUT = 5000;
                uint64_t next_timeout = getNextTimer();
                next_timeout = std::min(next_timeout, MAX_TIMEOUT);

                rt = epoll_wait(epfd_, events.get(), MAX_EVENTS, (int) next_timeout);
                if (rt < 0 && errno == EINTR) {
                    continue;
                } else {
                    break;
                }
            }
            std::vector<std::function<void()>> cbs;
            listExpiredCb(cbs);
            if (!cbs.empty()) {
                for (const auto& cb : cbs) {
                    scheduleLock(cb);
                }
                cbs.clear();
            }
            for (int i = 0; i < rt; ++i) {
                epoll_event& event = events[i];
                if (event.data.fd == tickleFds_[0]) {
                    uint8_t dummy[256];
                    while (read(tickleFds_[0], dummy, sizeof(dummy)) > 0) {
                        
                    }
                    continue;
                }

                FdContext* fd_ctx = (FdContext*) event.data.ptr;
                std::lock_guard<std::mutex> lock(fd_ctx->mutex);
                if (event.events & (EPOLLERR | EPOLLHUP)) {
                    event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
                }
                int real_events = NONE;
                if (event.events & EPOLLIN) {
                    real_events |= Event::READ;
                }
                if (event.events & EPOLLOUT) {
                    real_events |= Event::WRITE;
                }
                if ((fd_ctx->events & real_events) == NONE) {
                    continue;
                }
                int left_events = (fd_ctx->events & ~real_events);
                int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
                event.events = EPOLLET | left_events;

                int rt2 = epoll_ctl(epfd_, op, fd_ctx->fd, &event);
                if (rt2) {
                    std::cerr << "idle::epoll_ctl failed: " << strerror(errno) << std::endl;
                    continue;
                }
                if (real_events & Event::READ) {
                    fd_ctx->triggerEvent(Event::READ);
                    -- pendingEventCount_;
                }
                if (real_events & Event::WRITE) {
                    fd_ctx->triggerEvent(Event::WRITE);
                    -- pendingEventCount_;
                }
            }
            Fiber::GetThis()->yield();
        }
        
    }

    void IOManager::onTimerInsertedAtFront() {
        tickle();
    }
};