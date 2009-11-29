class Condition
{
private:
	volatile LONG x;
public:
	Condition() : x(0) {}

	void wait(ScopedLock& lock)
	{
		lock.unlock();
		while (!x)
		{
			//Sleep(0);
		}
		lock.lock();
	}
	void notify_all()
	{
		x = 1;
	}
	void reset()
	{
		x = 0;
	}
};

#define CONDITION Condition
#define CONDITION_RESET(condition) (condition).reset()
