#include <mutex>
#include <condition_variable>

#define MUTEX std::mutex

#define SCOPED_LOCK std::unique_lock<std::mutex>

class Condition
{
	bool ready;
	std::condition_variable cv;

public:
	Condition() : ready(false), cv() {}

	void wait(SCOPED_LOCK& lock)
	{
		cv.wait(lock, [this] { return ready; });
	}

	void notify(SCOPED_LOCK& lock)
	{
		ready = true;
		cv.notify_all();
	}
};

#define CONDITION std::condition_variable
#define CONDITION_WAIT(condition, lock) (condition).wait(lock)
#define CONDITION_NOTIFY(condition, lock) (condition).notify_all()
