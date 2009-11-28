#if defined(_M_IX86)
#define REG_THIS ecx
#elif defined(_M_X64)
#define REG_THIS rcx
#else
#error Unsupported architecture
#endif

//const char* debug = "OMGWTFBBQ";

class CriticalSection
{
private:
	volatile uint32_t x;
public:
	inline CriticalSection() : x(0) {}

	inline void __fastcall enter()
	{
		__asm
		{
			mov REG_THIS, this
			mov eax, 1
		l:
			xchg eax, dword ptr [REG_THIS]
			test eax, eax
			jnz l
		}
	}

	inline void __fastcall leave()
	{
		__asm
		{
			xor eax, eax
			mov REG_THIS, this
			xchg eax, dword ptr [REG_THIS]
		}
	}
};

#define MUTEX CriticalSection
#define MUTEX_SET_SPIN_COUNT(mutex, spin)

class ScopedLock
{
public:
	bool locked;
	CriticalSection* cs;
	
	inline ScopedLock(CriticalSection& cs) : locked(false), cs(&cs)
	{
		lock();
	};
	inline ~ScopedLock()
	{
		if (locked)
			cs->leave();
	};
	inline void lock()
	{
#ifdef DEBUG
		if (locked) throw "Already locked";
#endif
		cs->enter();
		locked = true;
	}
	inline void unlock()
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
	volatile uint32_t x;
public:
	Condition() : x(0) {}

	inline void __fastcall wait_sync()
	{
		__asm
		{
			mov eax, 0
			mov REG_THIS, this
		l:
			xchg eax, dword ptr [REG_THIS]
			test eax, eax
			jz l
		}
	}

	inline void wait(ScopedLock& lock)
	{
		lock.unlock();
		wait_sync();
		lock.lock();
	}

	inline void __fastcall notify_all()
	{
		__asm
		{
			mov eax, 1
			mov REG_THIS, this
			xchg eax, dword ptr [REG_THIS]
		}
	}
};

#define CONDITION Condition

#undef REG_THIS
