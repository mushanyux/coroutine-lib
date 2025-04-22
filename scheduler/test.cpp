/*** 
 * @Author: mushanyu
 * @Date: 2025-04-22 15:42:47
 * @LastEditTime: 2025-04-22 16:34:35
 * @LastEditors: mushanyu
 * @Description: 
 * @FilePath: /coroutine-lib/scheduler/test.cpp
 * @mushanyu
 */
#include "scheduler.h"

using namespace mushanyu;

static unsigned int test_number;
std::mutex mutex_count;

void task() {
    {
        std::lock_guard<std::mutex> lock(mutex_count);
        std::cout << "task " << test_number ++ << " is under processing in thread " << Thread::GetThreadId() << std::endl;
    }
    sleep(1);
}

int main() {
    {
        std::shared_ptr<Scheduler> scheduler = std::make_shared<Scheduler> (3, true, "scheduler_1");
        
        scheduler->start();

        sleep(2);

        std::cout << "\nbeging post\n\n";
        for (int i = 0; i < 10; i ++) {
            std::shared_ptr<Fiber> f = std::make_shared<Fiber>(task);
            scheduler->scheduleLock(f);
        }

        sleep(6);

        std::cout << "\npost again\n\n";

        for (int i = 0; i < 10; i ++) {
            std::shared_ptr<Fiber> f = std::make_shared<Fiber>(task);
            scheduler->scheduleLock(f);
        }

        sleep(6);

        scheduler->stop();
    }
    return 0;
}