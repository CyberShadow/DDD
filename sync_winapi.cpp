#ifndef _WIN32
#error Wrong platform?
#endif

class CriticalSection
{
public:
	CRITICAL_SECTION cs;

	CriticalSection()
	{
		InitializeCriticalSection(&cs);
	}
	~CriticalSection()
	{
		DeleteCriticalSection(&cs);
	}
	void enter()
	{
		EnterCriticalSection(&cs);
	}
	void leave()
	{
		LeaveCriticalSection(&cs);
	}
	void set_spin_count(DWORD dwSpinCount)
	{
		SetCriticalSectionSpinCount(&cs, dwSpinCount);
	}
};

#define MUTEX CriticalSection
#define MUTEX_SET_SPIN_COUNT(mutex, spin) (mutex).set_spin_count(spin)

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

// note: requires Windows Vista+
class Condition
{
public:
	CONDITION_VARIABLE cv;
	
	Condition()
	{
		InitializeConditionVariable(&cv);
	}
	void wait(ScopedLock& lock)
	{
		SleepConditionVariableCS(&cv, &lock.cs->cs, INFINITE);
	}
	void notify()
	{
		WakeAllConditionVariable(&cv);
	}
};

#define CONDITION Condition
#define CONDITION_WAIT(condition, lock) (condition).wait(lock)
#define CONDITION_NOTIFY(condition, lock) (condition).notify()
