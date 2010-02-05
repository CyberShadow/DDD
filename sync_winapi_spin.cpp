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
        //do;
		while (InterlockedExchange(&x, 1)) {}
    }

    inline void enter_nice()
    {
        //do;
        while (InterlockedExchange(&x, 1))
			SLEEP(1);
    }

#ifdef DEBUG
    void leave()
    {
        int old = InterlockedExchange(&x, 0);
		if (old==0)
			throw "CriticalSection wasn't locked";
    }
#else
    inline void leave()
    {
		x = 0;
    }
#endif
};

#define MUTEX CriticalSection
#define MUTEX_SET_SPIN_COUNT(mutex, spin)

template<bool NICE>
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
		if (NICE)
			cs->enter_nice();
		else
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

#define SCOPED_LOCK ScopedLock<false>
#define SCOPED_LOCK_NICE ScopedLock<true>

#include "sync_simple_condition.cpp"
