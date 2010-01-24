class Condition
{
private:
	volatile int x;
public:
	Condition() : x(0) {}

	void wait(ScopedLock& lock)
	{
		int old = x;
		do
		{
			lock.unlock();
			SLEEP(1);
			lock.lock();
		} while (old == x);
	}
	
	void notify(ScopedLock& lock) // must be synchronized
	{
#ifdef DEBUG
		if (!lock.locked)
			throw "Unsynchronized notify";
#endif
		x++;
	}
};

#define CONDITION Condition
#define CONDITION_WAIT(condition, lock) (condition).wait(lock)
#define CONDITION_NOTIFY(condition, lock) (condition).notify(lock)
