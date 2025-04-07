#include "fiber.h"
#include <vector>

using namespace mushanyu;

class Scheduler {
public:
	void schedule(std::shared_ptr<Fiber> task) {
		tasks_.push_back(task);
	}

	void run() {
		std::cout << " number " << tasks_.size() << std::endl;
		std::shared_ptr<Fiber> task;
		auto it = tasks_.begin();
		while(it != tasks_.end()) {
			task = *it;
			task->resume();
			it ++;
		}
		tasks_.clear();
	}

private:
	std::vector<std::shared_ptr<Fiber>> tasks_;
};

void test_fiber(int i) {
	std::cout << "hello world " << i << std::endl;
}

int main() {
	Fiber::GetThis();

	Scheduler sc;
	for(auto i = 0; i < 20; i ++) {
		std::shared_ptr<Fiber> fiber = std::make_shared<Fiber>(std::bind(test_fiber, i), 0, false);
		sc.schedule(fiber);
	}
	sc.run();
	return 0;
}