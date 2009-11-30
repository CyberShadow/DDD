#ifdef _WIN32
#include <windows.h>
#define SLEEP(x) Sleep(x)
#else
#include <unistd.h>
#define SLEEP(x) usleep((x)*1000)
#endif

class Condition
{
private:
	volatile LONG x;
	LONG waiting;
public:
	Condition() : x(0) {}

	void wait(ScopedLock& lock)
	{
		waiting++;
		lock.unlock();
		while (!x)
		{
			SLEEP(1);
		}
		lock.lock();
		waiting--;
	}
	void notify()
	{
		x = 1;
	}
	void reset()
	{
#ifdef DEBUG
		if (waiting)
			throw "Bad reset?";
#endif
		x = 0;
	}
	void barrier()
	{
		while(waiting)
		{
			SLEEP(1);
		}
	}
};

#define CONDITION Condition
#define CONDITION_RESET(condition) (condition).reset()
#define CONDITION_WAIT(condition, lock) (condition).wait(lock)
#define CONDITION_NOTIFY(condition, lock) (condition).notify()
#define CONDITION_BARRIER(condition) (condition).barrier()
