#ifndef _WIN32
#error Wrong platform?
#endif

DWORD WINAPI CallCFunction(__in LPVOID lpParameter)
{
	typedef void (*C_FUNC)();
	C_FUNC f = (C_FUNC)lpParameter;
	f();
	return 0;
}

#define THREAD HANDLE
#define THREAD_CREATE(delegate) CreateThread(NULL, 0, &CallCFunction, delegate, 0, NULL)
#define THREAD_JOIN(thread) WaitForSingleObject(thread, INFINITE);
#define THREAD_DESTROY(thread) CloseHandle(thread)

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

#define MUTEX CriticalSection
#define MUTEX_SET_SPIN_COUNT(mutex, spin) (mutex).set_spin_count(spin)

#define SCOPED_LOCK ScopedLock
//#define SCOPED_LOCK_UNLOCK(lock) (lock).unlock()
//#define SCOPED_LOCK_LOCK(lock) (lock).lock()

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
	void notify_all()
	{
		WakeAllConditionVariable(&cv);
	}
};

#define CONDITION Condition
//#define CONDITION_WAIT(condition, lock) (condition).wait(lock)
//#define CONDITION_NOTIFY_ALL(condition) condition.notify_all()
