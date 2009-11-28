#ifndef _WIN32
#error Wrong platform?
#endif

class CriticalSection
{
private:
    volatile LONG x;
public:
    inline CriticalSection() : x(0) {}

    inline void enter()
    {
        while (InterlockedExchange(&x, 1))
        {
        	//Sleep(0);
        }
    }

    inline void leave()
    {
        int old = InterlockedExchange(&x, 0);
#ifdef DEBUG
		if (old==0) throw "CriticalSection wasn't locked";
#endif
    }
};

#define MUTEX CriticalSection
#define MUTEX_SET_SPIN_COUNT(mutex, spin)

class ScopedLock
{
public:
	bool locked;
	CriticalSection* cs;
	
	ScopedLock(CriticalSection& cs) : locked(false), cs(&cs)
	{
		lock();
	};
	~ScopedLock()
	{
		if (locked)
			unlock();
	};
	void lock()
	{
#ifdef DEBUG
		if (locked) throw "Already locked";
#endif
		cs->enter();
		locked = true;
	}
	void unlock()
	{
#ifdef DEBUG
		if (!locked) throw "Already unlocked";
#endif
		cs->leave();
		locked = false;
	}
};

#define SCOPED_LOCK ScopedLock

class Condition
{
private:
	volatile LONG x;
public:
	Condition() : x(0) {}

	void wait(ScopedLock& lock)
	{
		lock.unlock();
		while (!InterlockedExchange(&x, 0))
		{
			//Sleep(0);
		}
		lock.lock();
	}
	void notify_all()
	{
		InterlockedExchange(&x, 1);
	}
};

#define CONDITION Condition
