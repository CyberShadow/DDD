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

//#ifdef _M_IX86
#if 0
//__declspec( naked )
//inline 
LONG /*__fastcall*/ MyInterlockedExchange(volatile LONG* pTargetAddress, LONG nValue)
{
    __asm
    {
        mov edx, pTargetAddress
        mov eax, nValue
        lock xchg eax, dword ptr [edx]
    }
}
#else
#define MyInterlockedExchange InterlockedExchange
#endif

#define THREAD HANDLE
#define THREAD_CREATE(delegate) CreateThread(NULL, 0, &CallCFunction, delegate, 0, NULL)
#define THREAD_JOIN(thread) WaitForSingleObject(thread, INFINITE);
#define THREAD_DESTROY(thread) CloseHandle(thread)

class CriticalSection
{
private:
    volatile LONG x;
public:
    inline CriticalSection() : x(0) {}

    inline void enter()
    {
        while (MyInterlockedExchange(&x, 1)==1)
        {
        	//Sleep(0);
        }
    }

    inline void leave(void)
    {
        int old = MyInterlockedExchange(&x, 0);
#ifdef DEBUG
		if (old==0) throw "CriticalSection wasn't locked";
#endif
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
#define MUTEX_SET_SPIN_COUNT(mutex, spin)

#define SCOPED_LOCK ScopedLock
//#define SCOPED_LOCK_UNLOCK(lock) (lock).unlock()
//#define SCOPED_LOCK_LOCK(lock) (lock).lock()

class Condition
{
private:
	volatile LONG x;
public:
	Condition() : x(0) {}

	void wait(ScopedLock& lock)
	{
		lock.unlock();
		while (MyInterlockedExchange(&x, 0)==0)
		{
			//Sleep(0);
		}
		lock.lock();
	}
	void notify_all()
	{
		MyInterlockedExchange(&x, 1);
	}
};

#define CONDITION Condition
//#define CONDITION_WAIT(condition, lock) (condition).wait(lock)
//#define CONDITION_NOTIFY_ALL(condition) condition.notify_all()
