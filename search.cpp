#undef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include "config.h"

#include <time.h>
#include <sys/timeb.h>
//#include <fstream>
#include <algorithm>
#include <list>
#include <queue>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __GNUC__
#include <stdint.h>
#else
#include "pstdint.h"
#endif

#ifdef _WIN32
#define SLEEP(x) Sleep(x)
#else
#include <unistd.h>
#define SLEEP(x) usleep((x)*1000)
#endif

#ifdef MULTITHREADING
#if defined(THREAD_BOOST)
#include "thread_boost.cpp"
#elif defined(THREAD_WINAPI)
#include "thread_winapi.cpp"
#else
#error Thread plugin not set
#endif

#if defined(SYNC_BOOST)
#include "sync_boost.cpp"
#elif defined(SYNC_WINAPI)
#include "sync_winapi.cpp"
#elif defined(SYNC_WINAPI_SPIN)
#include "sync_winapi_spin.cpp"
#elif defined(SYNC_INTEL_SPIN)
#include "sync_intel_spin.cpp"
#else
#error Sync plugin not set
#endif
// TODO: look into user-mode scheduling
#endif

// ******************************************** Utility code ********************************************

#define INRANGE(n,a,b) ((unsigned)((n)-(a))<=(unsigned)((b)-(a)))
#define INRANGEX(n,a,b) ((unsigned)((n)-(a))<(unsigned)((b)-(a)))

void error(const char* message = NULL)
{
	if (message)
		throw message;
	else
		throw "Unspecified error";
}

char* getTempString()
{
	static char buffers[64][1024];
	static int bufIndex = 0;
	int index;
	{
#ifdef MULTITHREADING
		static MUTEX mutex;
		SCOPED_LOCK lock(mutex);
#endif
		index = bufIndex++ % 64;
	}
		
	return buffers[index];
}

const char* format(const char *fmt, ...)
{    
	va_list argptr;
	va_start(argptr,fmt);
	//static char buf[1024];
	//char* buf = (char*)malloc(1024);
	char* buf = getTempString();
	vsprintf(buf, fmt, argptr);
	va_end(argptr);
	return buf;
}

const char* defaultstr(const char* a, const char* b = NULL) { return b ? b : a; }

// enforce - check condition in both DEBUG/RELEASE, error() on fail
// assert - check condition in DEBUG builds, try to instruct compiler to assume the condition is true in RELEASE builds
// debug_assert - check condition in DEBUG builds, do nothing in RELEASE builds (classic ASSERT)

#define enforce(expr,...) \
	while (!(expr)) \
	{ \
		error(defaultstr(format("Check failed at %s:%d", __FILE__,  __LINE__), __VA_ARGS__)); \
		throw "Unreachable"; \
	}

#undef assert
#ifdef DEBUG
#define assert enforce
#define debug_assert enforce
#define INLINE
#define DEBUG_ONLY(x) x
#else
#if defined(_MSC_VER)
#define assert(expr,...) __assume((expr)!=0)
#define INLINE __forceinline
#elif defined(__GNUC__)
#define assert(expr,...) __builtin_expect(!(expr),0)
#define INLINE inline
#else
#error Unknown compiler
#endif
#define debug_assert(...) do{}while(0)
#define DEBUG_ONLY(x) do{}while(0)
#endif

const char* hexDump(const void* data, size_t size, int columns = 0)
{
	char* buf = getTempString();
	enforce(size*3+1 < 1024);
	const uint8_t* s = (const uint8_t*)data;
	for (size_t x=0; x<size; x++)
		sprintf(buf+x*3, "%02X ", s[x]);
	if (columns)
		for (size_t x=columns; x<size; x+=columns)
			buf[x*3-1] = '\n';
	return buf;
}

void printTime()
{
	time_t t;
	time(&t);
	char* tstr = ctime(&t);
	tstr[strlen(tstr)-1] = 0;
	printf("[%s] ", tstr);
}

#define DO_STRINGIZE(x) #x
#define STRINGIZE(x) DO_STRINGIZE(x)

// *********************************************** Types ************************************************

typedef int32_t FRAME;
typedef int32_t FRAME_GROUP;

enum { PREFERRED_STATE_COMPRESSED, PREFERRED_STATE_UNCOMPRESSED, PREFERRED_STATE_NEITHER };

// ************************************* CompressedState comparison *************************************

#define     PIECE(block,byteoffset,type)      (*(const type*)(((const uint8_t*)&(block))+(byteoffset)))
#define MASKPIECE(block,byteoffset,type,mask) (*(const type*)(((const uint8_t*)&(block))+(byteoffset))&mask)

// ********************************************** Problem ***********************************************

#include STRINGIZE(PROBLEM/PROBLEM.cpp)

// ************************************ Types dependent on Problem **************************************

#if   (MAX_FRAMES<0x100)
#define PACKED_FRAME_BYTES 1
typedef uint8_t  PACKED_FRAME;
#elif (MAX_FRAMES<0x10000)
#define PACKED_FRAME_BYTES 2
typedef uint16_t PACKED_FRAME;
#else
#define PACKED_FRAME_BYTES 4
typedef uint32_t PACKED_FRAME;
#endif

// ************************************* CompressedState comparison *************************************

#ifndef COMPRESSED_BYTES
#define COMPRESSED_BYTES (((COMPRESSED_BITS) + 7) / 8)
#endif

// It is very important that these comparison operators are as fast as possible.
// Note that the optimized comparison operators are incompatible with the memcmp operators, due to the former being "middle-endian" and the latter being big-endian.
// TODO: relax ranges for !GROUP_FRAMES

#if   (!defined(USE_MEMCMP) && COMPRESSED_BITS >   0 && COMPRESSED_BITS <=   8) // 1 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,0,uint8_t) == PIECE(b,0,uint8_t); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,0,uint8_t) != PIECE(b,0,uint8_t); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,0,uint8_t) <  PIECE(b,0,uint8_t); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,0,uint8_t) <= PIECE(b,0,uint8_t); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS >   8 && COMPRESSED_BITS <=  16) // 2 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,0,uint16_t) == PIECE(b,0,uint16_t); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,0,uint16_t) != PIECE(b,0,uint16_t); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,0,uint16_t) <  PIECE(b,0,uint16_t); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,0,uint16_t) <= PIECE(b,0,uint16_t); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS >  16 && COMPRESSED_BITS <=  24) // 3 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return (MASKPIECE(a,-1,uint32_t,0xFFFFFF00)) == (MASKPIECE(b,-1,uint32_t,0xFFFFFF00)); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return (MASKPIECE(a,-1,uint32_t,0xFFFFFF00)) != (MASKPIECE(b,-1,uint32_t,0xFFFFFF00)); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return (MASKPIECE(a,-1,uint32_t,0xFFFFFF00)) <  (MASKPIECE(b,-1,uint32_t,0xFFFFFF00)); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return (MASKPIECE(a,-1,uint32_t,0xFFFFFF00)) <= (MASKPIECE(b,-1,uint32_t,0xFFFFFF00)); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS >  24 && COMPRESSED_BITS <=  32) // 4 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,0,uint32_t) == PIECE(b,0,uint32_t); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,0,uint32_t) != PIECE(b,0,uint32_t); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,0,uint32_t) <  PIECE(b,0,uint32_t); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,0,uint32_t) <= PIECE(b,0,uint32_t); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS >  32 && COMPRESSED_BITS <=  40) // 5 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return (MASKPIECE(a,-3,uint64_t,0xFFFFFFFFFF000000LL)) == (MASKPIECE(b,-3,uint64_t,0xFFFFFFFFFF000000LL)); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return (MASKPIECE(a,-3,uint64_t,0xFFFFFFFFFF000000LL)) != (MASKPIECE(b,-3,uint64_t,0xFFFFFFFFFF000000LL)); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return (MASKPIECE(a,-3,uint64_t,0xFFFFFFFFFF000000LL)) <  (MASKPIECE(b,-3,uint64_t,0xFFFFFFFFFF000000LL)); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return (MASKPIECE(a,-3,uint64_t,0xFFFFFFFFFF000000LL)) <= (MASKPIECE(b,-3,uint64_t,0xFFFFFFFFFF000000LL)); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS >  40 && COMPRESSED_BITS <=  48) // 6 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return (MASKPIECE(a,-2,uint64_t,0xFFFFFFFFFFFF0000LL)) == (MASKPIECE(b,-2,uint64_t,0xFFFFFFFFFFFF0000LL)); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return (MASKPIECE(a,-2,uint64_t,0xFFFFFFFFFFFF0000LL)) != (MASKPIECE(b,-2,uint64_t,0xFFFFFFFFFFFF0000LL)); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return (MASKPIECE(a,-2,uint64_t,0xFFFFFFFFFFFF0000LL)) <  (MASKPIECE(b,-2,uint64_t,0xFFFFFFFFFFFF0000LL)); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return (MASKPIECE(a,-2,uint64_t,0xFFFFFFFFFFFF0000LL)) <= (MASKPIECE(b,-2,uint64_t,0xFFFFFFFFFFFF0000LL)); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS >  48 && COMPRESSED_BITS <=  56) // 7 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return (MASKPIECE(a,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)) == (MASKPIECE(b,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return (MASKPIECE(a,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)) != (MASKPIECE(b,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return (MASKPIECE(a,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)) <  (MASKPIECE(b,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return (MASKPIECE(a,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)) <= (MASKPIECE(b,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS >  56 && COMPRESSED_BITS <=  64) // 8 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,0,uint64_t) == PIECE(b,0,uint64_t); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,0,uint64_t) != PIECE(b,0,uint64_t); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,0,uint64_t) <  PIECE(b,0,uint64_t); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,0,uint64_t) <= PIECE(b,0,uint64_t); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS >  64 && COMPRESSED_BITS <=  72) // 9 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,1,uint64_t) == PIECE(b,1,uint64_t) && PIECE(a,0,uint8_t) == PIECE(b,0,uint8_t); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,1,uint64_t) != PIECE(b,1,uint64_t) || PIECE(a,0,uint8_t) != PIECE(b,0,uint8_t); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,1,uint64_t) <  PIECE(b,1,uint64_t) || 
                                                                                   (PIECE(a,1,uint64_t) == PIECE(b,1,uint64_t) && PIECE(a,0,uint8_t) <  PIECE(b,0,uint8_t)); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,1,uint64_t) <  PIECE(b,1,uint64_t) || 
                                                                                   (PIECE(a,1,uint64_t) == PIECE(b,1,uint64_t) && PIECE(a,0,uint8_t) <= PIECE(b,0,uint8_t)); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS >  72 && COMPRESSED_BITS <=  80) // 10 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,2,uint64_t) == PIECE(b,2,uint64_t) && PIECE(a,0,uint16_t) == PIECE(b,0,uint16_t); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,2,uint64_t) != PIECE(b,2,uint64_t) || PIECE(a,0,uint16_t) != PIECE(b,0,uint16_t); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,2,uint64_t) <  PIECE(b,2,uint64_t) || 
                                                                                   (PIECE(a,2,uint64_t) == PIECE(b,2,uint64_t) && PIECE(a,0,uint16_t) <  PIECE(b,0,uint16_t)); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,2,uint64_t) <  PIECE(b,2,uint64_t) || 
                                                                                   (PIECE(a,2,uint64_t) == PIECE(b,2,uint64_t) && PIECE(a,0,uint16_t) <= PIECE(b,0,uint16_t)); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS >  80 && COMPRESSED_BITS <=  88) // 11 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,3,uint64_t) == PIECE(b,3,uint64_t) && (MASKPIECE(a,-1,uint32_t,0xFFFFFF00)) == (MASKPIECE(b,-1,uint32_t,0xFFFFFF00)); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,3,uint64_t) != PIECE(b,3,uint64_t) || (MASKPIECE(a,-1,uint32_t,0xFFFFFF00)) != (MASKPIECE(b,-1,uint32_t,0xFFFFFF00)); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,3,uint64_t) <  PIECE(b,3,uint64_t) || 
                                                                                   (PIECE(a,3,uint64_t) == PIECE(b,3,uint64_t) && (MASKPIECE(a,-1,uint32_t,0xFFFFFF00)) <  (MASKPIECE(b,-1,uint32_t,0xFFFFFF00))); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,3,uint64_t) <  PIECE(b,3,uint64_t) || 
                                                                                   (PIECE(a,3,uint64_t) == PIECE(b,3,uint64_t) && (MASKPIECE(a,-1,uint32_t,0xFFFFFF00)) <= (MASKPIECE(b,-1,uint32_t,0xFFFFFF00))); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS >  88 && COMPRESSED_BITS <=  96) // 12 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,4,uint64_t) == PIECE(b,4,uint64_t) && PIECE(a,0,uint32_t) == PIECE(b,0,uint32_t); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,4,uint64_t) != PIECE(b,4,uint64_t) || PIECE(a,0,uint32_t) != PIECE(b,0,uint32_t); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,4,uint64_t) <  PIECE(b,4,uint64_t) || 
                                                                                   (PIECE(a,4,uint64_t) == PIECE(b,4,uint64_t) && PIECE(a,0,uint32_t) <  PIECE(b,0,uint32_t)); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,4,uint64_t) <  PIECE(b,4,uint64_t) || 
                                                                                   (PIECE(a,4,uint64_t) == PIECE(b,4,uint64_t) && PIECE(a,0,uint32_t) <= PIECE(b,0,uint32_t)); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS >  96 && COMPRESSED_BITS <= 104) // 13 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,5,uint64_t) == PIECE(b,5,uint64_t) && (MASKPIECE(a,-3,uint64_t,0xFFFFFFFFFF000000LL)) == (MASKPIECE(b,-3,uint64_t,0xFFFFFFFFFF000000LL)); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,5,uint64_t) != PIECE(b,5,uint64_t) || (MASKPIECE(a,-3,uint64_t,0xFFFFFFFFFF000000LL)) != (MASKPIECE(b,-3,uint64_t,0xFFFFFFFFFF000000LL)); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,5,uint64_t) <  PIECE(b,5,uint64_t) || 
                                                                                   (PIECE(a,5,uint64_t) == PIECE(b,5,uint64_t) && (MASKPIECE(a,-3,uint64_t,0xFFFFFFFFFF000000LL)) <  (MASKPIECE(b,-3,uint64_t,0xFFFFFFFFFF000000LL))); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,5,uint64_t) <  PIECE(b,5,uint64_t) || 
                                                                                   (PIECE(a,5,uint64_t) == PIECE(b,5,uint64_t) && (MASKPIECE(a,-3,uint64_t,0xFFFFFFFFFF000000LL)) <= (MASKPIECE(b,-3,uint64_t,0xFFFFFFFFFF000000LL))); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS > 104 && COMPRESSED_BITS <= 112) // 14 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,6,uint64_t) == PIECE(b,6,uint64_t) && (MASKPIECE(a,-2,uint64_t,0xFFFFFFFFFFFF0000LL)) == (MASKPIECE(b,-2,uint64_t,0xFFFFFFFFFFFF0000LL)); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,6,uint64_t) != PIECE(b,6,uint64_t) || (MASKPIECE(a,-2,uint64_t,0xFFFFFFFFFFFF0000LL)) != (MASKPIECE(b,-2,uint64_t,0xFFFFFFFFFFFF0000LL)); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,6,uint64_t) <  PIECE(b,6,uint64_t) || 
                                                                                   (PIECE(a,6,uint64_t) == PIECE(b,6,uint64_t) && (MASKPIECE(a,-2,uint64_t,0xFFFFFFFFFFFF0000LL)) <  (MASKPIECE(b,-2,uint64_t,0xFFFFFFFFFFFF0000LL))); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,6,uint64_t) <  PIECE(b,6,uint64_t) || 
                                                                                   (PIECE(a,6,uint64_t) == PIECE(b,6,uint64_t) && (MASKPIECE(a,-2,uint64_t,0xFFFFFFFFFFFF0000LL)) <= (MASKPIECE(b,-2,uint64_t,0xFFFFFFFFFFFF0000LL))); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS > 112 && COMPRESSED_BITS <= 120) // 15 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,7,uint64_t) == PIECE(b,7,uint64_t) && (MASKPIECE(a,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)) == (MASKPIECE(b,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,7,uint64_t) != PIECE(b,7,uint64_t) || (MASKPIECE(a,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)) != (MASKPIECE(b,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,7,uint64_t) <  PIECE(b,7,uint64_t) || 
                                                                                   (PIECE(a,7,uint64_t) == PIECE(b,7,uint64_t) && (MASKPIECE(a,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)) <  (MASKPIECE(b,-1,uint64_t,0xFFFFFFFFFFFFFF00LL))); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,7,uint64_t) <  PIECE(b,7,uint64_t) || 
                                                                                   (PIECE(a,7,uint64_t) == PIECE(b,7,uint64_t) && (MASKPIECE(a,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)) <= (MASKPIECE(b,-1,uint64_t,0xFFFFFFFFFFFFFF00LL))); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS > 120 && COMPRESSED_BITS <= 128) // 16 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,8,uint64_t) == PIECE(b,8,uint64_t) && PIECE(a,0,uint64_t) == PIECE(b,0,uint64_t); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,8,uint64_t) != PIECE(b,8,uint64_t) || PIECE(a,0,uint64_t) != PIECE(b,0,uint64_t); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,8,uint64_t) <  PIECE(b,8,uint64_t) || 
                                                                                   (PIECE(a,8,uint64_t) == PIECE(b,8,uint64_t) && PIECE(a,0,uint64_t) <  PIECE(b,0,uint64_t)); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,8,uint64_t) <  PIECE(b,8,uint64_t) || 
                                                                                   (PIECE(a,8,uint64_t) == PIECE(b,8,uint64_t) && PIECE(a,0,uint64_t) <= PIECE(b,0,uint64_t)); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS > 128 && COMPRESSED_BITS <= 136) // 17 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,9,uint64_t) == PIECE(b,9,uint64_t) &&  PIECE(a,1,uint64_t) == PIECE(b,1,uint64_t) && PIECE(a,0,uint8_t) == PIECE(b,0,uint8_t); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,9,uint64_t) != PIECE(b,9,uint64_t) ||  PIECE(a,1,uint64_t) != PIECE(b,1,uint64_t) || PIECE(a,0,uint8_t) != PIECE(b,0,uint8_t); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,9,uint64_t) <  PIECE(b,9,uint64_t) || 
                                                                                   (PIECE(a,9,uint64_t) == PIECE(b,9,uint64_t) && (PIECE(a,1,uint64_t) <  PIECE(b,1,uint64_t) ||
                                                                                                                                  (PIECE(a,1,uint64_t) == PIECE(b,1,uint64_t) && PIECE(a,0,uint8_t) <  PIECE(b,0,uint8_t)))); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,9,uint64_t) <  PIECE(b,9,uint64_t) || 
                                                                                   (PIECE(a,9,uint64_t) == PIECE(b,9,uint64_t) && (PIECE(a,1,uint64_t) <  PIECE(b,1,uint64_t) ||
                                                                                                                                  (PIECE(a,1,uint64_t) == PIECE(b,1,uint64_t) && PIECE(a,0,uint8_t) <= PIECE(b,0,uint8_t)))); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS > 136 && COMPRESSED_BITS <= 144) // 18 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,10,uint64_t) == PIECE(b,10,uint64_t) &&  PIECE(a,2,uint64_t) == PIECE(b,2,uint64_t) && PIECE(a,0,uint16_t) == PIECE(b,0,uint16_t); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,10,uint64_t) != PIECE(b,10,uint64_t) ||  PIECE(a,2,uint64_t) != PIECE(b,2,uint64_t) || PIECE(a,0,uint16_t) != PIECE(b,0,uint16_t); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,10,uint64_t) <  PIECE(b,10,uint64_t) || 
                                                                                   (PIECE(a,10,uint64_t) == PIECE(b,10,uint64_t) && (PIECE(a,2,uint64_t) <  PIECE(b,2,uint64_t) ||
                                                                                                                                    (PIECE(a,2,uint64_t) == PIECE(b,2,uint64_t) && PIECE(a,0,uint16_t) <  PIECE(b,0,uint16_t)))); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,10,uint64_t) <  PIECE(b,10,uint64_t) || 
                                                                                   (PIECE(a,10,uint64_t) == PIECE(b,10,uint64_t) && (PIECE(a,2,uint64_t) <  PIECE(b,2,uint64_t) ||
                                                                                                                                    (PIECE(a,2,uint64_t) == PIECE(b,2,uint64_t) && PIECE(a,0,uint16_t) <= PIECE(b,0,uint16_t)))); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS > 144 && COMPRESSED_BITS <= 152) // 19 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,11,uint64_t) == PIECE(b,11,uint64_t) &&  PIECE(a,3,uint64_t) == PIECE(b,3,uint64_t) && (MASKPIECE(a,-1,uint32_t,0xFFFFFF00)) == (MASKPIECE(b,-1,uint32_t,0xFFFFFF00)); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,11,uint64_t) != PIECE(b,11,uint64_t) ||  PIECE(a,3,uint64_t) != PIECE(b,3,uint64_t) || (MASKPIECE(a,-1,uint32_t,0xFFFFFF00)) != (MASKPIECE(b,-1,uint32_t,0xFFFFFF00)); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,11,uint64_t) <  PIECE(b,11,uint64_t) || 
                                                                                   (PIECE(a,11,uint64_t) == PIECE(b,11,uint64_t) && (PIECE(a,3,uint64_t) <  PIECE(b,3,uint64_t) ||
                                                                                                                                    (PIECE(a,3,uint64_t) == PIECE(b,3,uint64_t) && (MASKPIECE(a,-1,uint32_t,0xFFFFFF00)) <  (MASKPIECE(b,-1,uint32_t,0xFFFFFF00))))); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,11,uint64_t) <  PIECE(b,11,uint64_t) || 
                                                                                   (PIECE(a,11,uint64_t) == PIECE(b,11,uint64_t) && (PIECE(a,3,uint64_t) <  PIECE(b,3,uint64_t) ||
                                                                                                                                    (PIECE(a,3,uint64_t) == PIECE(b,3,uint64_t) && (MASKPIECE(a,-1,uint32_t,0xFFFFFF00)) <= (MASKPIECE(b,-1,uint32_t,0xFFFFFF00))))); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS > 152 && COMPRESSED_BITS <= 160) // 20 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,12,uint64_t) == PIECE(b,12,uint64_t) &&  PIECE(a,4,uint64_t) == PIECE(b,4,uint64_t) && PIECE(a,0,uint32_t) == PIECE(b,0,uint32_t); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,12,uint64_t) != PIECE(b,12,uint64_t) ||  PIECE(a,4,uint64_t) != PIECE(b,4,uint64_t) || PIECE(a,0,uint32_t) != PIECE(b,0,uint32_t); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,12,uint64_t) <  PIECE(b,12,uint64_t) || 
                                                                                   (PIECE(a,12,uint64_t) == PIECE(b,12,uint64_t) && (PIECE(a,4,uint64_t) <  PIECE(b,4,uint64_t) ||
                                                                                                                                    (PIECE(a,4,uint64_t) == PIECE(b,4,uint64_t) && PIECE(a,0,uint32_t) <  PIECE(b,0,uint32_t)))); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,12,uint64_t) <  PIECE(b,12,uint64_t) || 
                                                                                   (PIECE(a,12,uint64_t) == PIECE(b,12,uint64_t) && (PIECE(a,4,uint64_t) <  PIECE(b,4,uint64_t) ||
                                                                                                                                    (PIECE(a,4,uint64_t) == PIECE(b,4,uint64_t) && PIECE(a,0,uint32_t) <= PIECE(b,0,uint32_t)))); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS > 160 && COMPRESSED_BITS <= 168) // 21 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,13,uint64_t) == PIECE(b,13,uint64_t) &&  PIECE(a,5,uint64_t) == PIECE(b,5,uint64_t) && (MASKPIECE(a,-3,uint64_t,0xFFFFFFFFFF000000LL)) == (MASKPIECE(b,-3,uint64_t,0xFFFFFFFFFF000000LL)); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,13,uint64_t) != PIECE(b,13,uint64_t) ||  PIECE(a,5,uint64_t) != PIECE(b,5,uint64_t) || (MASKPIECE(a,-3,uint64_t,0xFFFFFFFFFF000000LL)) != (MASKPIECE(b,-3,uint64_t,0xFFFFFFFFFF000000LL)); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,13,uint64_t) <  PIECE(b,13,uint64_t) || 
                                                                                   (PIECE(a,13,uint64_t) == PIECE(b,13,uint64_t) && (PIECE(a,5,uint64_t) <  PIECE(b,5,uint64_t) ||
                                                                                                                                    (PIECE(a,5,uint64_t) == PIECE(b,5,uint64_t) && (MASKPIECE(a,-3,uint64_t,0xFFFFFFFFFF000000LL)) <  (MASKPIECE(b,-3,uint64_t,0xFFFFFFFFFF000000LL))))); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,13,uint64_t) <  PIECE(b,13,uint64_t) || 
                                                                                   (PIECE(a,13,uint64_t) == PIECE(b,13,uint64_t) && (PIECE(a,5,uint64_t) <  PIECE(b,5,uint64_t) ||
                                                                                                                                    (PIECE(a,5,uint64_t) == PIECE(b,5,uint64_t) && (MASKPIECE(a,-3,uint64_t,0xFFFFFFFFFF000000LL)) <= (MASKPIECE(b,-3,uint64_t,0xFFFFFFFFFF000000LL))))); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS > 168 && COMPRESSED_BITS <= 176) // 22 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,14,uint64_t) == PIECE(b,14,uint64_t) &&  PIECE(a,6,uint64_t) == PIECE(b,6,uint64_t) && (MASKPIECE(a,-2,uint64_t,0xFFFFFFFFFFFF0000LL)) == (MASKPIECE(b,-2,uint64_t,0xFFFFFFFFFFFF0000LL)); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,14,uint64_t) != PIECE(b,14,uint64_t) ||  PIECE(a,6,uint64_t) != PIECE(b,6,uint64_t) || (MASKPIECE(a,-2,uint64_t,0xFFFFFFFFFFFF0000LL)) != (MASKPIECE(b,-2,uint64_t,0xFFFFFFFFFFFF0000LL)); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,14,uint64_t) <  PIECE(b,14,uint64_t) || 
                                                                                   (PIECE(a,14,uint64_t) == PIECE(b,14,uint64_t) && (PIECE(a,6,uint64_t) <  PIECE(b,6,uint64_t) ||
                                                                                                                                    (PIECE(a,6,uint64_t) == PIECE(b,6,uint64_t) && (MASKPIECE(a,-2,uint64_t,0xFFFFFFFFFFFF0000LL)) <  (MASKPIECE(b,-2,uint64_t,0xFFFFFFFFFFFF0000LL))))); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,14,uint64_t) <  PIECE(b,14,uint64_t) || 
                                                                                   (PIECE(a,14,uint64_t) == PIECE(b,14,uint64_t) && (PIECE(a,6,uint64_t) <  PIECE(b,6,uint64_t) ||
                                                                                                                                    (PIECE(a,6,uint64_t) == PIECE(b,6,uint64_t) && (MASKPIECE(a,-2,uint64_t,0xFFFFFFFFFFFF0000LL)) <= (MASKPIECE(b,-2,uint64_t,0xFFFFFFFFFFFF0000LL))))); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS > 176 && COMPRESSED_BITS <= 184) // 23 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,15,uint64_t) == PIECE(b,15,uint64_t) &&  PIECE(a,7,uint64_t) == PIECE(b,7,uint64_t) && (MASKPIECE(a,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)) == (MASKPIECE(b,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,15,uint64_t) != PIECE(b,15,uint64_t) ||  PIECE(a,7,uint64_t) != PIECE(b,7,uint64_t) || (MASKPIECE(a,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)) != (MASKPIECE(b,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,15,uint64_t) <  PIECE(b,15,uint64_t) || 
                                                                                   (PIECE(a,15,uint64_t) == PIECE(b,15,uint64_t) && (PIECE(a,7,uint64_t) <  PIECE(b,7,uint64_t) ||
                                                                                                                                    (PIECE(a,7,uint64_t) == PIECE(b,7,uint64_t) && (MASKPIECE(a,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)) <  (MASKPIECE(b,-1,uint64_t,0xFFFFFFFFFFFFFF00LL))))); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,15,uint64_t) <  PIECE(b,15,uint64_t) || 
                                                                                   (PIECE(a,15,uint64_t) == PIECE(b,15,uint64_t) && (PIECE(a,7,uint64_t) <  PIECE(b,7,uint64_t) ||
                                                                                                                                    (PIECE(a,7,uint64_t) == PIECE(b,7,uint64_t) && (MASKPIECE(a,-1,uint64_t,0xFFFFFFFFFFFFFF00LL)) <= (MASKPIECE(b,-1,uint64_t,0xFFFFFFFFFFFFFF00LL))))); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS > 184 && COMPRESSED_BITS <= 192) // 24 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,16,uint64_t) == PIECE(b,16,uint64_t) &&  PIECE(a,8,uint64_t) == PIECE(b,8,uint64_t) && PIECE(a,0,uint64_t) == PIECE(b,0,uint64_t); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,16,uint64_t) != PIECE(b,16,uint64_t) ||  PIECE(a,8,uint64_t) != PIECE(b,8,uint64_t) || PIECE(a,0,uint64_t) != PIECE(b,0,uint64_t); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,16,uint64_t) <  PIECE(b,16,uint64_t) || 
                                                                                   (PIECE(a,16,uint64_t) == PIECE(b,16,uint64_t) && (PIECE(a,8,uint64_t) <  PIECE(b,8,uint64_t) ||
                                                                                                                                    (PIECE(a,8,uint64_t) == PIECE(b,8,uint64_t) && PIECE(a,0,uint64_t) <  PIECE(b,0,uint64_t)))); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,16,uint64_t) <  PIECE(b,16,uint64_t) || 
                                                                                   (PIECE(a,16,uint64_t) == PIECE(b,16,uint64_t) && (PIECE(a,8,uint64_t) <  PIECE(b,8,uint64_t) ||
                                                                                                                                    (PIECE(a,8,uint64_t) == PIECE(b,8,uint64_t) && PIECE(a,0,uint64_t) <= PIECE(b,0,uint64_t)))); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS > 192 && COMPRESSED_BITS <= 200) // 25 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,17,uint64_t) == PIECE(b,17,uint64_t) &&  PIECE(a,9,uint64_t) == PIECE(b,9,uint64_t) &&  PIECE(a,1,uint64_t) == PIECE(b,1,uint64_t) && PIECE(a,0,uint8_t) == PIECE(b,0,uint8_t); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,17,uint64_t) != PIECE(b,17,uint64_t) ||  PIECE(a,9,uint64_t) != PIECE(b,9,uint64_t) ||  PIECE(a,1,uint64_t) != PIECE(b,1,uint64_t) || PIECE(a,0,uint8_t) != PIECE(b,0,uint8_t); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,17,uint64_t) <  PIECE(b,17,uint64_t) || 
                                                                                   (PIECE(a,17,uint64_t) == PIECE(b,17,uint64_t) && (PIECE(a,9,uint64_t) <  PIECE(b,9,uint64_t) ||
                                                                                                                                    (PIECE(a,9,uint64_t) == PIECE(b,9,uint64_t) && (PIECE(a,1,uint64_t) <  PIECE(b,1,uint64_t) ||
																																	                                               (PIECE(a,1,uint64_t) == PIECE(b,1,uint64_t) && PIECE(a,0,uint8_t) <  PIECE(b,0,uint8_t)))))); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,17,uint64_t) <  PIECE(b,17,uint64_t) || 
                                                                                   (PIECE(a,17,uint64_t) == PIECE(b,17,uint64_t) && (PIECE(a,9,uint64_t) <  PIECE(b,9,uint64_t) ||
                                                                                                                                    (PIECE(a,9,uint64_t) == PIECE(b,9,uint64_t) && (PIECE(a,1,uint64_t) <  PIECE(b,1,uint64_t) ||
																																	                                               (PIECE(a,1,uint64_t) == PIECE(b,1,uint64_t) && PIECE(a,0,uint8_t) <= PIECE(b,0,uint8_t)))))); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS > 200 && COMPRESSED_BITS <= 208) // 26 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,18,uint64_t) == PIECE(b,18,uint64_t) &&  PIECE(a,10,uint64_t) == PIECE(b,10,uint64_t) &&  PIECE(a,2,uint64_t) == PIECE(b,2,uint64_t) && PIECE(a,0,uint16_t) == PIECE(b,0,uint16_t); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,18,uint64_t) != PIECE(b,18,uint64_t) ||  PIECE(a,10,uint64_t) != PIECE(b,10,uint64_t) ||  PIECE(a,2,uint64_t) != PIECE(b,2,uint64_t) || PIECE(a,0,uint16_t) != PIECE(b,0,uint16_t); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,18,uint64_t) <  PIECE(b,18,uint64_t) || 
                                                                                   (PIECE(a,18,uint64_t) == PIECE(b,18,uint64_t) && (PIECE(a,10,uint64_t) <  PIECE(b,10,uint64_t) ||
                                                                                                                                    (PIECE(a,10,uint64_t) == PIECE(b,10,uint64_t) && (PIECE(a,2,uint64_t) <  PIECE(b,2,uint64_t) ||
																																	                                                 (PIECE(a,2,uint64_t) == PIECE(b,2,uint64_t) && PIECE(a,0,uint16_t) <  PIECE(b,0,uint16_t)))))); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,18,uint64_t) <  PIECE(b,18,uint64_t) || 
                                                                                   (PIECE(a,18,uint64_t) == PIECE(b,18,uint64_t) && (PIECE(a,10,uint64_t) <  PIECE(b,10,uint64_t) ||
                                                                                                                                    (PIECE(a,10,uint64_t) == PIECE(b,10,uint64_t) && (PIECE(a,2,uint64_t) <  PIECE(b,2,uint64_t) ||
																																	                                                 (PIECE(a,2,uint64_t) == PIECE(b,2,uint64_t) && PIECE(a,0,uint16_t) <= PIECE(b,0,uint16_t)))))); }
#elif (!defined(USE_MEMCMP) && COMPRESSED_BITS > 208 && COMPRESSED_BITS <= 216) // 27 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return PIECE(a,19,uint64_t) == PIECE(b,19,uint64_t) &&  PIECE(a,11,uint64_t) == PIECE(b,11,uint64_t) &&  PIECE(a,3,uint64_t) == PIECE(b,3,uint64_t) && (MASKPIECE(a,-1,uint32_t,0xFFFFFF00)) == (MASKPIECE(b,-1,uint32_t,0xFFFFFF00)); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return PIECE(a,19,uint64_t) != PIECE(b,19,uint64_t) ||  PIECE(a,11,uint64_t) != PIECE(b,11,uint64_t) ||  PIECE(a,3,uint64_t) != PIECE(b,3,uint64_t) || (MASKPIECE(a,-1,uint32_t,0xFFFFFF00)) != (MASKPIECE(b,-1,uint32_t,0xFFFFFF00)); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return PIECE(a,19,uint64_t) <  PIECE(b,19,uint64_t) || 
                                                                                   (PIECE(a,19,uint64_t) == PIECE(b,19,uint64_t) && (PIECE(a,11,uint64_t) <  PIECE(b,11,uint64_t) ||
                                                                                                                                    (PIECE(a,11,uint64_t) == PIECE(b,11,uint64_t) && (PIECE(a,3,uint64_t) <  PIECE(b,3,uint64_t) ||
																																	                                                 (PIECE(a,3,uint64_t) == PIECE(b,3,uint64_t) && (MASKPIECE(a,-1,uint32_t,0xFFFFFF00)) <  (MASKPIECE(b,-1,uint32_t,0xFFFFFF00))))))); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return PIECE(a,19,uint64_t) <  PIECE(b,19,uint64_t) || 
                                                                                   (PIECE(a,19,uint64_t) == PIECE(b,19,uint64_t) && (PIECE(a,11,uint64_t) <  PIECE(b,11,uint64_t) ||
                                                                                                                                    (PIECE(a,11,uint64_t) == PIECE(b,11,uint64_t) && (PIECE(a,3,uint64_t) <  PIECE(b,3,uint64_t) ||
																																	                                                 (PIECE(a,3,uint64_t) == PIECE(b,3,uint64_t) && (MASKPIECE(a,-1,uint32_t,0xFFFFFF00)) <= (MASKPIECE(b,-1,uint32_t,0xFFFFFF00))))))); }
#else
#pragma message("Performance warning: using memcmp for CompressedState comparison")
#define SLOW_COMPARE
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return memcmp(&a, &b, COMPRESSED_BYTES)==0; }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return memcmp(&a, &b, COMPRESSED_BYTES)!=0; }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return memcmp(&a, &b, COMPRESSED_BYTES)< 0; }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return memcmp(&a, &b, COMPRESSED_BYTES)<=0; }
#endif

INLINE bool operator> (const CompressedState& a, const CompressedState& b) { return b< a; }
INLINE bool operator>=(const CompressedState& a, const CompressedState& b) { return b<=a; }

// ******************************************** Frame groups ********************************************

#ifdef GROUP_FRAMES

#define GET_FRAME(frameGroup, cs) ((frameGroup) * FRAMES_PER_GROUP + (cs).subframe)
#define SET_SUBFRAME(cs, frame) (cs).subframe = (frame) % FRAMES_PER_GROUP

#define GROUP_STR "-group"
#if (FRAMES_PER_GROUP == 10)
#define GROUP_FORMAT "%ux"
#define GROUP_ALIGNED_FORMAT "%3ux"
#else
#define GROUP_FORMAT "g%u"
#define GROUP_ALIGNED_FORMAT "g%3u"
#endif

#else // GROUP_FRAMES

#define FRAMES_PER_GROUP 1
#define GET_FRAME(frameGroup, cs) (frameGroup)
#define SET_SUBFRAME(cs, frame)
#define GROUP_STR ""
#define GROUP_FORMAT "%u"

#endif

FRAME_GROUP currentFrameGroup;

// ************************************************ Disk ************************************************

#if defined(DISK_WINFILES)
#include "disk_file_windows.cpp"
#elif defined(DISK_POSIX)
#include "disk_file_posix.cpp"
#else
#error Disk plugin not set
#endif

// *********************************************** Memory ***********************************************

// Allocate RAM at start, use it for different purposes depending on what we're doing
// Even if we won't use all of it, most OSes shouldn't reserve physical RAM for the entire amount
void* ram = malloc(RAM_SIZE);
void* ramEnd = (char*)ram + RAM_SIZE;

#ifndef STANDARD_BUFFER_SIZE
#define STANDARD_BUFFER_SIZE (1024*1024 / sizeof(Node)) // 1 MB
#endif
#ifndef ALL_FILE_BUFFER_SIZE
#define ALL_FILE_BUFFER_SIZE (1024*1024 / sizeof(Node)) // 1 MB
#endif
#ifndef EXPECTED_MERGING_RATIO
#define EXPECTED_MERGING_RATIO 0.6
#endif

#ifndef USE_ALL
#undef ALL_FILE_BUFFER_SIZE
#define ALL_FILE_BUFFER_SIZE 0
#endif

struct PackedCompressedState
{
	uint8_t bytes[COMPRESSED_BYTES];

	bool operator ==(const CompressedState& state) { return *(CompressedState*)this == state; }

	PackedCompressedState& operator =(const CompressedState& state) { return *this =  (PackedCompressedState&)state; }
	PackedCompressedState& operator =(const CompressedState* state) { return *this = *(PackedCompressedState*)state; }
};

typedef PackedCompressedState BareNode;

struct Node
{
	PackedCompressedState state;
#if COMPRESSED_BYTES%4 == 1
	uint8_t _align1;
#endif
	uint8_t subframe;
#if (COMPRESSED_BYTES+(COMPRESSED_BYTES%4==1?1:0)+1)%4 != 0
	uint8_t _align2[4-(COMPRESSED_BYTES+(COMPRESSED_BYTES%4==1?1:0)+1)%4];
#endif

	CompressedState& getState() const { return (CompressedState&)state; }
};

struct OpenNode
{
	PACKED_FRAME frame;
#if (COMPRESSED_BYTES+PACKED_FRAME_BYTES)%4 != 0
	uint8_t padding[4-(COMPRESSED_BYTES+PACKED_FRAME_BYTES)%4];
#endif
	PackedCompressedState state;

	CompressedState& getState() const { return (CompressedState&)state; }
};

INLINE bool operator==(const Node& a, const Node& b) { return a.getState() == b.getState(); }
INLINE bool operator!=(const Node& a, const Node& b) { return a.getState() != b.getState(); }
INLINE bool operator< (const Node& a, const Node& b) { return a.getState() <  b.getState(); }
INLINE bool operator<=(const Node& a, const Node& b) { return a.getState() <= b.getState(); }
INLINE bool operator> (const Node& a, const Node& b) { return a.getState() >  b.getState(); }
INLINE bool operator>=(const Node& a, const Node& b) { return a.getState() >= b.getState(); }

INLINE bool operator==(const OpenNode& a, const OpenNode& b) { return a.getState() == b.getState(); }
INLINE bool operator!=(const OpenNode& a, const OpenNode& b) { return a.getState() != b.getState(); }
INLINE bool operator< (const OpenNode& a, const OpenNode& b) { return a.getState() <  b.getState(); }
INLINE bool operator<=(const OpenNode& a, const OpenNode& b) { return a.getState() <= b.getState(); }
INLINE bool operator> (const OpenNode& a, const OpenNode& b) { return a.getState() >  b.getState(); }
INLINE bool operator>=(const OpenNode& a, const OpenNode& b) { return a.getState() >= b.getState(); }

// For deduplication
INLINE unsigned     getFrame(const     Node* node) { return node->subframe; }
INLINE PACKED_FRAME getFrame(const OpenNode* node) { return node->frame;    }
INLINE void setFrame(    Node* node, uint8_t      frame)  { node->subframe = frame; }
INLINE void setFrame(OpenNode* node, PACKED_FRAME frame)  { node->frame    = frame; }

const size_t BUFFER_SIZE = RAM_SIZE / sizeof(Node);
const size_t OPENNODE_BUFFER_SIZE = RAM_SIZE / sizeof(OpenNode);
Node* buffer = (Node*) ram;

// ****************************************** Buffered streams ******************************************

template<class NODE>
class Buffer
{
public:
	NODE* buf;
	uint32_t size;

	Buffer(uint32_t size) : size(size), buf(NULL)
	{
	}

	// A pre-specified size of 0 is allowed. The calling code must later call reallocate() or assign() before any read/write operations.
	void allocate()
	{
		if (!buf && size)
			buf = new NODE[size];
	}

	// When we need to use the default constructor (e.g. arrays)
	void setSize(uint32_t newSize)
	{
		assert(!buf, "Trying to set the buffer size after the buffer was allocated");
		size = newSize;
	}

	void reallocate(uint32_t newSize)
	{
		if (size != newSize)
		{
			deallocate();
			size = newSize;
			if (size)
				buf = new NODE[size];
		}
	}

	void assign(NODE* newBuf, uint32_t newSize)
	{
		deallocate();
		assert(newBuf >= ram && newBuf < ramEnd);
		buf = newBuf;
		size = newSize;
	}

	void clear()
	{
		assert(buf);
		memset(buf, 0, size * sizeof(NODE));
	}

	void deallocate()
	{
		if (buf)
		{
			if (buf < ram || buf >= ramEnd)
				delete[] buf;
			buf = NULL;
		}
	}

	~Buffer()
	{
		deallocate();
	}
};

template<class STREAM>
class BufferedStreamBase
{
protected:
	STREAM s;

public:
	uint64_t size() { return s.size(); }
	bool isOpen() { return s.isOpen(); }
	void close() { return s.close(); }
};

template<class STREAM, class NODE>
class WriteBuffer : virtual public BufferedStreamBase<STREAM>
{
	uint32_t pos;
protected:
	Buffer<NODE> buffer;
public:
	WriteBuffer(uint32_t size) : buffer(size), pos(0) {}

	void write(const NODE* p, bool verify=false)
	{
		buffer.buf[pos++] = *p;
#ifdef DEBUG
		if (verify && pos > 1)
			assert(buffer.buf[pos-1] > buffer.buf[pos-2], "Output is not sorted");
#endif
		if (pos == buffer.size)
			flushBuffer();
	}

	uint64_t size()
	{
		return s.size() + pos;
	}

	void clearBuffer()
	{
		buffer.clear();
	}

	void flushBuffer()
	{
		if (pos)
		{
			s.write(buffer.buf, pos);
			pos = 0;
		}
	}

	void flush()
	{
		flushBuffer();
#ifndef NO_DISK_FLUSH
		s.flush();
#endif
	}

	void close()
	{
		flushBuffer();
		BufferedStreamBase<STREAM>::close();
	}

	~WriteBuffer()
	{
		flushBuffer();
	}

	void setWriteBuffer(NODE* buf, uint32_t size)
	{
		flushBuffer();
		buffer.assign(buf, size);
	}

	void setWriteBufferSize(uint32_t size)
	{
		flushBuffer();
		buffer.reallocate(size);
	}

	enum { WRITABLE = true };
};

template<class STREAM, class NODE>
class ReadBuffer : virtual public BufferedStreamBase<STREAM>
{
	uint32_t pos, end;
protected:
	Buffer<NODE> buffer;
public:
	ReadBuffer(uint32_t size) : buffer(size), pos(0), end(0) {}

	const NODE* read()
	{
		if (pos == end)
		{
			fillBuffer();
			if (end == 0)
				return NULL;
		}
#ifdef DEBUG
		if (pos > 0) 
			assert(buffer.buf[pos-1] < buffer.buf[pos], "Input is not sorted");
#endif
		return &buffer.buf[pos++];
	}

	void fillBuffer()
	{
		pos = 0;
		uint64_t left = s.size() - s.position();
		end = (uint32_t)s.read(buffer.buf, (size_t)(left < buffer.size ? left : buffer.size));
	}

	void setReadBuffer(NODE* buf, uint32_t size)
	{
		assert(pos == end, "Buffer is dirty");
		buffer.assign(buf, size);
	}

	// Useable only before allocation
	void setReadBufferSize(uint32_t size)
	{
		buffer.setSize(size);
	}

	void rewind()
	{
		assert(pos > 0);
		pos--;
	}

	// some InputHeap compatibility
	INLINE const NODE* getHead()
	{
		if (pos == 0)
			return NULL;
		return buffer.buf + (pos-1);
	}

	bool next()
	{
		if (pos == end)
		{
			fillBuffer();
			if (pos == 0)
				return false;
		}
#ifdef DEBUG
		if (pos > 0) 
			assert(buffer.buf[pos-1] < buffer.buf[pos], "Input is not sorted");
#endif
		pos++;
		return true;
	}

	template<bool checkFirst, class OUTPUT>
	int scanTo(const NODE* target, OUTPUT* output)
	{
		if (checkFirst)
		{
			const NODE* headState = getHead();
			if (headState && *headState >= *target)
				return (*headState > *target);
		}
		else
			debug_assert(getHead()==NULL || *getHead() < *target);

		const NODE* node;
		do
		{
			if (OUTPUT::WRITABLE)
			{
				const NODE* head = getHead();
				if (head)
					output->write(head);
			}
			node = read();
			if (node == NULL)
				return -1;
		} while (*node < *target);
		return (*node > *target);
	}

	template<bool checkFirst, class OUTPUT1, class OUTPUT2>
	int scanTo(const NODE* target, OUTPUT1* output1, OUTPUT2* output2)
	{
		if (checkFirst)
		{
			const NODE* headState = getHead();
			if (headState && *headState >= *target)
				return (*headState > *target);
		}
		else
			debug_assert(getHead()==NULL || *getHead() < *target);

		const NODE* node;
		do
		{
			if (OUTPUT1::WRITABLE || OUTPUT2::WRITABLE)
			{
				const NODE* head = getHead();
				if (head)
				{
					if (OUTPUT1::WRITABLE) output1->write(head);
					if (OUTPUT2::WRITABLE) output2->write(head);
				}
			}
			node = read();
			if (node == NULL)
				return -1;
		} while (*node < *target);
		return (*node > *target);
	}
};

template<class NODE>
class BufferedInputStream : public ReadBuffer<InputStream<NODE>, NODE>
{
public:
	BufferedInputStream(uint32_t size = STANDARD_BUFFER_SIZE) : ReadBuffer(size) {}
	BufferedInputStream(const char* filename, uint32_t size = STANDARD_BUFFER_SIZE) : ReadBuffer(size) { open(filename); }
	void open(const char* filename) { s.open(filename); buffer.allocate(); }
};

template<class NODE>
class BufferedOutputStream : public WriteBuffer<OutputStream<NODE>, NODE>
{
public:
	BufferedOutputStream(uint32_t size = STANDARD_BUFFER_SIZE) : WriteBuffer(size) {}
	BufferedOutputStream(const char* filename, bool resume=false, uint32_t size = STANDARD_BUFFER_SIZE) : WriteBuffer(size) { open(filename, resume); }
	void open(const char* filename, bool resume=false) { s.open(filename, resume); buffer.allocate(); }
};

template<class NODE>
class BufferedRewriteStream : public ReadBuffer<RewriteStream<NODE>, NODE>, public WriteBuffer<RewriteStream<NODE>, NODE>
{
public:
	BufferedRewriteStream(uint32_t readSize = STANDARD_BUFFER_SIZE, uint32_t writeSize = STANDARD_BUFFER_SIZE) : ReadBuffer(readSize), WriteBuffer(writeSize) {}
	BufferedRewriteStream(const char* filename, uint32_t readSize = STANDARD_BUFFER_SIZE, uint32_t writeSize = STANDARD_BUFFER_SIZE) : ReadBuffer(readSize), WriteBuffer(writeSize) { open(filename); }
	void open(const char* filename) { s.open(filename); ReadBuffer<RewriteStream>::buffer.allocate(); WriteBuffer<RewriteStream>::buffer.allocate(); }
	void truncate() { s.truncate(); }
};

template<class NODE>
class MemoryInputStream
{
	NODE *start, *pos, *end;
public:
	MemoryInputStream() {}
	MemoryInputStream(NODE* _start, NODE* _end) : start(_start), pos(_start), end(_end) {}
	bool isOpen() const { return true; }

	void open(NODE* _start, NODE* _end) { pos=start=_start; end=_end; }

	const NODE* read()
	{
		if (pos==end)
			return NULL;
		return pos++;
	}
	void rewind()
	{
		pos = start;
	}
};

#if 0
template<class NODE>
class SplitInputStream : public InputStream<NODE>
{
private:
	uint64_t start, end;
	uint64_t pos;
public:
	SplitInputStream() : InputStream(), start(0), end(0), pos(0) {}

	SplitInputStream(InputStream<NODE>& _stream, uint64_t _start, uint64_t _end) : InputStream(filename), start(_start), end(_end), pos(_start) {}

	void open(const char* filename, uint64_t _start, uint64_t _end)
	{
		start = _start;
		end = _end;
		pos = _start;

		InputStream::open(filename);
		assert(end <= InputStream::size());
		if (start != 0)
			InputStream::seek(start);
	}

	uint64_t size()
	{
		return end - start;
	}
	
	uint64_t position()
	{
		return pos - start;
	}

	void seek(uint64_t _pos)
	{
		pos = start + _pos;
		if (pos > end)
			pos = end;
		InputStream::seek(pos);
	}

	size_t read(NODE* p, size_t n)
	{
		if (n > end - pos)
			n = end - pos;
		n = InputStream::read(p, n);
		pos += n;
		return n;
	}
};

template<class NODE>
class BufferedSplitInputStream : public ReadBuffer<SplitInputStream<NODE>, NODE>
{
public:
	BufferedSplitInputStream(uint32_t size = STANDARD_BUFFER_SIZE) : ReadBuffer(size) {}
	BufferedSplitInputStream(InputStream<NODE>& stream, uint64_t start, uint64_t end, uint32_t size = STANDARD_BUFFER_SIZE) : ReadBuffer(size) { open(stream, start, end); }
	void open(const char* filename, uint64_t start, uint64_t end) { s.open(filename, start, end); buffer.allocate(); }
};

template<class NODE, unsigned PIECES>
class BufferedSplitInputStreamSet
{
private:
	BufferedSplitInputStream<NODE> bufferedStream[PIECES];
public:
	void setReadBuffer(NODE* buf, uint32_t size)
	{
		uint32_t pos = 0;
		uint32_t numerator;
		unsigned i;
		for (i=0, numerator=size; i<PIECES; i++, numerator+=size)
		{
			uint32_t endPos = numerator / PIECES;
			bufferedStream[i].setReadBuffer(buf + pos, endPos - pos);
			pos = endPos;
		}
	}

	void open(const char* filename)
	{
		uint64_t fileSize;
		{
			InputStream<NODE> getSize(filename);
			fileSize = getSize.size();
		}
		uint64_t pos = 0;
		uint64_t numerator;
		unsigned i;
		for (i=0, numerator=fileSize; i<PIECES; i++, numerator+=fileSize)
		{
			uint64_t endPos = numerator / PIECES;
			bufferedStream[i].open(filename, pos, endPos);
			pos = endPos;
		}
	}

	void close()
	{
		for (unsigned i=0; i<PIECES; i++)
			bufferedStream[i].close();
	}

	BufferedSplitInputStream<NODE>& stream(unsigned n)
	{
		return bufferedStream[n];
	}
};
#endif

template<class NODE>
void copyFile(const char* from, const char* to)
{
	InputStream<NODE> input(from);
	OutputStream<NODE> output(to);
	uint64_t amount = input.size();
	if (amount > BUFFER_SIZE)
		amount = BUFFER_SIZE;
	size_t records;
	while (records = input.read(buffer, (size_t)amount))
		output.write(buffer, records);
	output.flush(); // force disk flush
}

// ***************************************** Stream operations ******************************************

template<class INPUT, class NODE>
class InputHeap
{
protected:    
	struct HeapNode
	{
		const NODE* state;
		INPUT* input;
		INLINE bool operator<(const HeapNode& b) const { return *this->state < *b.state; }
	};

	HeapNode *heap, *head;
	int size;

public:
	InputHeap(INPUT inputs[], int count)
	{
		if (count==0)
			error("No inputs");
		heap = new HeapNode[count];
		size = 0;
		for (int i=0; i<count; i++)
		{
			if (inputs[i].isOpen())
			{
				heap[size].input = &inputs[i];
				heap[size].state = inputs[i].read();
				if (heap[size].state)
					size++;
			}
		}
		std::sort(heap, heap+size);
		head = heap;
		heap--; // heap[0] is now invalid, use heap[1] to heap[size] inclusively; head == heap[1]
		if (size==0 && count>0)
			head->state = NULL;
		if (size)
		{
			head->input->rewind(); // prepare for first next() call
			head->state = NULL;
		}
		//test(); // this test is broken, because it dereferences heap[1]->state after it has been explicitly NULLed (head->state = NULL, above)
	}

	~InputHeap()
	{
		heap++;
		delete[] heap;
	}

	const NODE* getHead() const { return head->state; }
	INPUT* getHeadInput() const { return head->input; }

	bool next()
	{
		//test(); // this test is broken
		if (size == 0)
			return false;
		head->state = head->input->read();
		if (head->state == NULL)
		{
			*head = heap[size];
			size--;
			if (size==0)
				return false;
		}
		bubbleDown();
		test();
		return true;
	}

	INLINE const NODE* read()
	{
		if (!next())
		{
			//return NULL;
			assert(getHead() == NULL);
		}
		return getHead();
	}

	/// output receives "old" states (that is, *getHead() on function entry but not on exit)
	template<bool checkFirst, class OUTPUT>
	int scanTo(const NODE* target, OUTPUT* output)
	{
		test();
		if (size == 0)
			return -1;

		if (checkFirst)
		{
			const NODE* headState = getHead();
			if (headState && *headState >= *target)
				return (*headState > *target);
		}
		else
			debug_assert(getHead()==NULL || *getHead() < *target);

		if (size>1)
		{
			do
			{
				NODE readUntil = *target;
				const NODE* minChild = heap[2].state;
				if (size>2 && *minChild > *heap[3].state)
					minChild = heap[3].state;
				if (readUntil > *minChild)
					readUntil = *minChild;
				
				do
				{
					if (OUTPUT::WRITABLE && head->state) // can we get rid of this check?
						output->write(head->state);
					head->state = head->input->read();
				}
				while (head->state && *head->state < readUntil);

				if (head->state == NULL)
				{
					*head = heap[size];
					size--;
				}
				else
					if (*head->state <= *minChild)
						continue;
				bubbleDown();
				test();
				if (size==1)
					if (*head->state < *target)
						goto size1;
					else
						break;
			} while (*head->state < *target);
			test();
			return (*head->state > *target);
		}
		else
		{
		size1:
			do
			{
				if (OUTPUT::WRITABLE && head->state) // can we get rid of this check?
					output->write(head->state);
				head->state = head->input->read();
				if (head->state == NULL)
				{
					size = 0;
					return -1;
				}
			} while (*head->state < *target);
			test();
			return (*head->state > *target);
		}
	}

	void bubbleDown()
	{
		// Force local variables
		intptr_t c = 1;
		intptr_t size = this->size;
		HeapNode* heap = this->heap;
		HeapNode* pp = head; // pointer to parent
		while (1)
		{
			c = c*2;
			if (c > size)
				return;
			HeapNode* pc = &heap[c];
			if (c < size) // if (c+1 <= size)
			{
				HeapNode* pc2 = pc+1;
				if (*pc2->state < *pc->state)
				{
					pc = pc2;
					c++;
				}
			}
			if (*pp->state <= *pc->state)
				return;
			HeapNode t = *pp;
			*pp = *pc;
			*pc = t;
			pp = pc;
		}
	}

	void test() const
	{
#ifdef DEBUG
		for (int p=1; p<size; p++)
		{
			assert(p*2   > size || *heap[p].state <= *heap[p*2  ].state);
			assert(p*2+1 > size || *heap[p].state <= *heap[p*2+1].state);
		}
#endif
	}
};

template <class NODE, class INPUT, class OUTPUT>
void copyStream(INPUT* input, OUTPUT* output)
{
	const NODE* node;
	while (node = input->read())
		output->write(node, false);
}

template<class NODE, class INPUT, class OUTPUT>
void mergeStreams(INPUT inputs[], int inputCount, OUTPUT* output)
{
	InputHeap<INPUT, NODE> heap(inputs, inputCount);

	const NODE* first = heap.read();
	if (!first)
		return;
	NODE cs = *(NODE*)first;
	const NODE* cs2;
	
	while (cs2 = heap.read())
	{
		debug_assert(*cs2 >= cs);
		if (cs == *cs2) // CompressedState::operator== does not compare subframe
		{
#ifdef GROUP_FRAMES
			if (getFrame(&cs) > getFrame(cs2)) // in case of duplicate frames, pick the one from the smallest frame
				setFrame(&cs,   getFrame(cs2));
#endif
		}
		else
		{
			output->write(&cs, true);
			cs = *cs2;
		}
	}
	output->write(&cs, true);
}

#if 0
void mergeStreams(BufferedInputStream<Node> inputs[], int inputCount, BufferedOutputStream<BareNode>* output)
{
	InputHeap<BufferedInputStream<Node>, Node> heap(inputs, inputCount);

	const CompressedState* first = heap.read();
	if (!first)
		return;
	CompressedState cs = *first;
	const CompressedState* cs2;
	
	while (cs2 = heap.read())
	{
		debug_assert(*cs2 >= cs);
		if (cs != *cs2) // CompressedState::operator!= does not compare subframe
		{
			output->write(&(BareNode&)cs, true);
			cs = *cs2;
		}
	}
	output->write(&(BareNode&)cs, true);
}
#endif

class NullOutput
{	
public:
	enum { WRITABLE = false };
	void write(const CompressedState* cs, bool verify=false) {}
};

NullOutput nullOutput;

template <class NODE, class A, class B>
class DoubleOutput : public A, public B
{
public:
	INLINE void write(const NODE* cs, bool verify=false)
	{
		A::write(cs, verify);
		B::write(cs, verify);
	}

	INLINE A* a() { return this; }
	INLINE B* b() { return this; }
};

template<class CLOSED, class MERGED, class NODE>
void filterStreams(CLOSED* closed, BufferedRewriteStream<NODE> open[], int openCount, MERGED* merged)
{
	if (openCount==0)
	{
		copyStream(closed, merged);
		return;
	}
	
	InputHeap<BufferedRewriteStream> openHeap(open, openCount);
	openHeap.next();

	bool done = false;
	while (!done)
	{
		CompressedState o = *openHeap.getHead();
		FRAME lowestFrame = MAX_FRAMES;
		do
		{
			FRAME_GROUP group = (FRAME_GROUP)(openHeap.getHeadInput() - open);
			FRAME frame = GET_FRAME(group, *openHeap.getHead());
			if (lowestFrame > frame)
				lowestFrame = frame;
			if (!openHeap.next())
			{
				done = true;
				break;
			}
			if (o > *openHeap.getHead())
				error(format("Unsorted open node file for frame" GROUP_STR " " GROUP_FORMAT "/" GROUP_FORMAT, group, openHeap.getHeadInput() - open));
		} while (o == *openHeap.getHead());

		int r = closed->scanTo<false>(&o, merged);
		if (r == 0)
			closed->next();
		else
		{
			SET_SUBFRAME(o, lowestFrame);
			open[lowestFrame/FRAMES_PER_GROUP].write(&o, true);
		}
		merged->write(&o, true);
	}
}

// In-place deduplicate sorted nodes in memory. Return new number of nodes.
template<class COMPRESSED_STATE>
size_t deduplicate(COMPRESSED_STATE* start, size_t records)
{
	if (records==0)
		return 0;
	COMPRESSED_STATE *read=start+1, *write=start+1;
	COMPRESSED_STATE *end = start+records;
	while (read < end)
	{
		debug_assert(*read >= *(read-1));
		if (*read == *(write-1)) // CompressedState::operator== does not compare subframe
		{
#ifdef GROUP_FRAMES
			if (getFrame(write-1) > getFrame(read))
				setFrame(write-1,   getFrame(read));
#endif
		}
		else
		{
			*write = *read;
            write++;
		}
        read++;
	}
	return write-start;
}

// ********************************************* File names *********************************************

const char* formatFileName(const char* name)
{
	return formatProblemFileName(name, NULL, "bin");
}

const char* formatFileName(const char* name, FRAME_GROUP g)
{
	return formatProblemFileName(name, format(GROUP_FORMAT, g), "bin");
}

const char* formatFileName(const char* name, FRAME_GROUP g, unsigned chunk)
{
	return formatProblemFileName(name, format(GROUP_FORMAT "-%u", g, chunk), "bin");
}

// ****************************************** Processing queue ******************************************

#ifdef MULTITHREADING

#define WORKERS (THREADS-1)
#define PROCESS_QUEUE_SIZE 0x100000
Node processQueue[PROCESS_QUEUE_SIZE]; // circular buffer
size_t processQueueHead=0, processQueueTail=0;
MUTEX processQueueMutex; // for head/tail
CONDITION processQueueReadCondition, processQueueWriteCondition, processQueueExitCondition;
volatile int runningWorkers = 0;
volatile bool stopWorkers = false;
CONDITION specialWorkersExitCondition;
volatile int runningSpecialWorkers = 0;
volatile bool stopSpecialWorkers = false;

#ifdef ENABLE_EXPANSION_SPILLOVER
void expansionReadSpilloverThread(THREAD_ID threadID);
#endif
void expansionSortFinalRegions(THREAD_ID threadID);

void queueState(const Node* state)
{
	SCOPED_LOCK lock(processQueueMutex);

	while (processQueueHead == processQueueTail+PROCESS_QUEUE_SIZE) // while full
		CONDITION_WAIT(processQueueReadCondition, lock);
	processQueue[processQueueHead++ % PROCESS_QUEUE_SIZE] = *state;
	CONDITION_NOTIFY(processQueueWriteCondition, lock);
}

bool dequeueState(Node* state)
{
	SCOPED_LOCK lock(processQueueMutex);
		
	while (processQueueHead == processQueueTail) // while empty
	{
		if (stopWorkers)
			return false;
		CONDITION_WAIT(processQueueWriteCondition, lock);
	}
	*state = processQueue[processQueueTail++ % PROCESS_QUEUE_SIZE];
	CONDITION_NOTIFY(processQueueReadCondition, lock);
	return true;
}

void doNothing(THREAD_ID threadID) {}

template<void (*STATE_HANDLER)(const Node*, THREAD_ID threadID), void (*FINALIZATION_HANDLER)(THREAD_ID threadID)>
void worker(THREAD_ID threadID)
{
	Node cs;
	while (dequeueState(&cs))
		STATE_HANDLER(&cs, threadID);

	FINALIZATION_HANDLER(threadID);

    /* LOCK */
	{
		SCOPED_LOCK lock(processQueueMutex);
		runningWorkers--;
		CONDITION_NOTIFY(processQueueExitCondition, lock);
	}
}

template<void (*STATE_HANDLER)(const Node*, THREAD_ID threadID), void (*FINALIZATION_HANDLER)(THREAD_ID threadID)>
void startWorkers()
{
#ifdef ENABLE_EXPANSION_SPILLOVER
	{
		SCOPED_LOCK lock(processQueueMutex);
		runningSpecialWorkers++;
	}
	THREAD_CREATE<expansionReadSpilloverThread>(0);
#endif

	{
		SCOPED_LOCK lock(processQueueMutex);
		runningWorkers += WORKERS;
	}
	for (THREAD_ID threadID=0; threadID<WORKERS; threadID++)
		THREAD_CREATE<worker<STATE_HANDLER,FINALIZATION_HANDLER>>(threadID);
}

void flushProcessingQueue()
{
	SCOPED_LOCK lock(processQueueMutex);

#ifdef ENABLE_EXPANSION_SPILLOVER
	// finish expansionReadSpilloverThread
	stopSpecialWorkers = true;
	while (runningSpecialWorkers)
		CONDITION_WAIT(specialWorkersExitCondition, lock);
	stopSpecialWorkers = false;
#endif

	stopWorkers = true;
	CONDITION_NOTIFY(processQueueWriteCondition, lock);
	while (runningWorkers)
		CONDITION_WAIT(processQueueExitCondition, lock);
	stopWorkers = false;

	// wait for expansionWriteChunkThread to finish
	//stopSpecialWorkers = true;
	while (runningSpecialWorkers)
		CONDITION_WAIT(specialWorkersExitCondition, lock);
	//stopSpecialWorkers = false;
}

#endif

// ************************************** Disk queue (open nodes) ***************************************

#define MAX_FRAME_GROUPS (MAX_FRAMES/FRAMES_PER_GROUP)

uint64_t closedNodesInCurrentFrameGroup;
uint64_t combinedNodesTotal;

BufferedOutputStream<Node> closedNodeFile;


#ifdef ENABLE_EXPANSION_SPILLOVER
bool expansionSpilloverLocked; // true if expansion spillover is currently being read or written to

OutputStream<OpenNode> expansionSpilloverOut;
bool expansionSpilloverOutOpen;
unsigned expansionSpilloverChunkOut;
size_t   expansionSpilloverChunkOutPos;

InputStream<OpenNode> expansionSpilloverIn;
bool expansionSpilloverInOpen;
unsigned expansionSpilloverChunkIn;
size_t   expansionSpilloverChunkInPos;

size_t expansionSpilloverNodesQueued;
#endif


#define EXPANSION_BUFFER_SLOTS (OPENNODE_BUFFER_SIZE / EXPANSION_NODES_PER_QUEUE_ELEMENT)
#define EXPANSION_BUFFER_SIZE (EXPANSION_BUFFER_SLOTS * EXPANSION_NODES_PER_QUEUE_ELEMENT)

const unsigned EXPANSION_BUFFER_FILL_THRESHOLD ((unsigned)(EXPANSION_BUFFER_SLOTS * EXPANSION_BUFFER_FILL_RATIO)  <=   EXPANSION_BUFFER_SLOTS - (WORKERS-1) ?
                                                (unsigned)(EXPANSION_BUFFER_SLOTS * EXPANSION_BUFFER_FILL_RATIO)  >=   1                                    ?
                                                (unsigned)(EXPANSION_BUFFER_SLOTS * EXPANSION_BUFFER_FILL_RATIO)     : 1
                                                                                                                     : EXPANSION_BUFFER_SLOTS - (WORKERS-1));
#ifdef ENABLE_EXPANSION_SPILLOVER
const unsigned EXPANSION_SPILLOVER_WRITE_THRESHOLD  = EXPANSION_BUFFER_SLOTS - EXPANSION_BUFFER_FILL_THRESHOLD;
const unsigned EXPANSION_SPILLOVER_READ_THRESHOLD   = EXPANSION_BUFFER_SLOTS - EXPANSION_BUFFER_FILL_THRESHOLD + 1;

const unsigned EXPANSION_SPILLOVER_SLACK = (0x200000 + EXPANSION_NODES_PER_QUEUE_ELEMENT-1) / EXPANSION_NODES_PER_QUEUE_ELEMENT;
#endif

OpenNode* const EXPANSION_BUFFER     = (OpenNode*)ram;
OpenNode* const EXPANSION_BUFFER_END = (OpenNode*)ram + EXPANSION_BUFFER_SIZE;

MUTEX expansionMutex;
unsigned expansionChunks;
enum EXPANSION_BUFFER_REGION_TYPE
{
	EXPANSION_BUFFER_REGION_EMPTY,
	EXPANSION_BUFFER_REGION_FILLING,  // threadID==0
//	EXPANSION_BUFFER_REGION_FILLING+1 is threadID==1
//	EXPANSION_BUFFER_REGION_FILLING+2 is threadID==2, etc...
	EXPANSION_BUFFER_REGION_FILLED = EXPANSION_BUFFER_REGION_FILLING + WORKERS,
	EXPANSION_BUFFER_REGION_READING,
	EXPANSION_BUFFER_REGION_SORTING,
	EXPANSION_BUFFER_REGION_WRITING,
	EXPANSION_BUFFER_REGION_MERGING,
};
struct ExpansionBufferRegion
{
	unsigned pos, length;
	EXPANSION_BUFFER_REGION_TYPE type;
};
std::list<ExpansionBufferRegion> expansionBufferRegions;
std::list<ExpansionBufferRegion>::iterator expansionThreadIter[WORKERS];
struct ExpansionBufferSortedRegion
{
	OpenNode *start, *end;
};
std::queue<ExpansionBufferSortedRegion> expansionBufferRegionsToMerge;
unsigned numSortsInProgress;
bool expansionChunkWriteInProgress;
bool expansionThreadFinalized[WORKERS];
#ifdef DEBUG_EXPANSION
FILE *expansionDebug;
#endif

struct
{
	OpenNode* buffer;
	int i, increment;
	OpenNode* finalSortBufferEnd;
} expansionThread[WORKERS];

#ifdef DEBUG_EXPANSION
void dumpExpansionDebug(THREAD_ID threadID)
{
	timeb time1;
	ftime(&time1);
	fprintf(expansionDebug, "%9d.%03d: ", time1.time, time1.millitm);

	fputc('1'+(char)threadID, expansionDebug);
	fputc(':', expansionDebug);
	fputc(' ', expansionDebug);

	for (std::list<ExpansionBufferRegion>::iterator i=expansionBufferRegions.begin(); i!=expansionBufferRegions.end(); i++)
	{
		debug_assert(!(i->type == EXPANSION_BUFFER_REGION_EMPTY && i->length==0));

		for (unsigned x=0; x<i->length; x++)
		{
			switch (i->type)
			{
			case EXPANSION_BUFFER_REGION_EMPTY:   fputc('.', expansionDebug); break;
			case EXPANSION_BUFFER_REGION_FILLED:  fputc('#', expansionDebug); break;
			default: fputc('1'+i->type-EXPANSION_BUFFER_REGION_FILLING, expansionDebug); break;
			case EXPANSION_BUFFER_REGION_READING: fputc(x==0?'R':'r', expansionDebug); break;
			case EXPANSION_BUFFER_REGION_SORTING: fputc(x==0?'S':'s', expansionDebug); break;
			case EXPANSION_BUFFER_REGION_WRITING: fputc(x==0?'W':'w', expansionDebug); break;
			case EXPANSION_BUFFER_REGION_MERGING: fputc(x==0?'M':'m', expansionDebug); break;
			}
		}
	}
	fputc('\n', expansionDebug);
	fflush(expansionDebug);
}
#endif

void initExpansion()
{
#ifdef ENABLE_EXPANSION_SPILLOVER
	expansionSpilloverLocked = false;
	expansionSpilloverOutOpen = false;
	expansionSpilloverChunkOut = 0;
	expansionSpilloverChunkOutPos = 0;
	expansionSpilloverInOpen = false;
	expansionSpilloverChunkIn = 0;
	expansionSpilloverChunkInPos = 0;
	expansionSpilloverNodesQueued = 0;
#endif

	numSortsInProgress = 0;
	expansionChunkWriteInProgress = false;

	expansionBufferRegions.clear();
	expansionBufferRegionsToMerge = std::queue<ExpansionBufferSortedRegion>();
	OpenNode* slot = EXPANSION_BUFFER;
	for (THREAD_ID threadID=0; threadID<WORKERS; threadID++)
	{
		expansionThreadFinalized[threadID] = false;

		expansionThread[threadID].buffer = slot;
		slot += EXPANSION_NODES_PER_QUEUE_ELEMENT;
		expansionThread[threadID].i = 0;
		expansionThread[threadID].increment = +1;

		ExpansionBufferRegion region;
		region.pos = (unsigned)threadID;
		region.length = 1;
		region.type = (EXPANSION_BUFFER_REGION_TYPE)(EXPANSION_BUFFER_REGION_FILLING + threadID);
		expansionBufferRegions.push_back(region);
		expansionThreadIter[threadID] = expansionBufferRegions.end();
		expansionThreadIter[threadID]--;
	}
	{
		ExpansionBufferRegion region;
		region.pos = WORKERS;
		region.length = EXPANSION_BUFFER_SLOTS - WORKERS;
		region.type = EXPANSION_BUFFER_REGION_EMPTY;
		expansionBufferRegions.push_back(region);
	}

	expansionChunks = 0;

#ifdef DEBUG_EXPANSION
	expansionDebug = fopen("debug.log", "at");
	fprintf(expansionDebug, "Frame group %u\n", currentFrameGroup);
	dumpExpansionDebug(-1);
#endif
}

void expansionRegionMarkFilled(std::list<ExpansionBufferRegion>::iterator& regionToFill, THREAD_ID threadID)
{
	std::list<ExpansionBufferRegion>::iterator before = regionToFill;
	std::list<ExpansionBufferRegion>::iterator after  = regionToFill; ++after;
	if (before != expansionBufferRegions.begin() && (--before)->type == EXPANSION_BUFFER_REGION_FILLED)
	{
		if (after != expansionBufferRegions.end() && after->type == EXPANSION_BUFFER_REGION_FILLED)
		{
			before->length += regionToFill->length + after->length;
			expansionBufferRegions.erase(regionToFill, ++after);
		}
		else
		{
			before->length += regionToFill->length;
			expansionBufferRegions.erase(regionToFill);
		}
	}
	else
	if (after != expansionBufferRegions.end() && after->type == EXPANSION_BUFFER_REGION_FILLED)
	{
		after->pos    -= regionToFill->length;
		after->length += regionToFill->length;
		expansionBufferRegions.erase(regionToFill);
	}
	else
		regionToFill->type = EXPANSION_BUFFER_REGION_FILLED;

	regionToFill = expansionBufferRegions.end();

#ifdef DEBUG_EXPANSION
	dumpExpansionDebug(threadID);
#endif
}

void expansionRegionMarkEmpty(std::list<ExpansionBufferRegion>::iterator& regionToEmpty, THREAD_ID threadID)
{
	std::list<ExpansionBufferRegion>::iterator before = regionToEmpty;
	std::list<ExpansionBufferRegion>::iterator after  = regionToEmpty; ++after;
	if (before != expansionBufferRegions.begin() && (--before)->type == EXPANSION_BUFFER_REGION_EMPTY)
	{
		if (after != expansionBufferRegions.end() && after->type == EXPANSION_BUFFER_REGION_EMPTY)
		{
			before->length += regionToEmpty->length + after->length;
			expansionBufferRegions.erase(regionToEmpty, ++after);
		}
		else
		{
			before->length += regionToEmpty->length;
			expansionBufferRegions.erase(regionToEmpty);
		}
	}
	else
	if (after != expansionBufferRegions.end() && after->type == EXPANSION_BUFFER_REGION_EMPTY)
	{
		after->pos    -= regionToEmpty->length;
		after->length += regionToEmpty->length;
		expansionBufferRegions.erase(regionToEmpty);
	}
	else
		regionToEmpty->type = EXPANSION_BUFFER_REGION_EMPTY;
}

OutputStream<OpenNode> expansionWriteChunkThreadStream;
OpenNode* expansionWriteChunkThreadBuffer;
size_t expansionWriteChunkThreadCount;
std::list<ExpansionBufferRegion>::iterator expansionWriteChunkThreadRegion;
void expansionWriteChunkThread(THREAD_ID threadID)
{
	expansionWriteChunkThreadStream.write(expansionWriteChunkThreadBuffer, expansionWriteChunkThreadCount);
	expansionWriteChunkThreadStream.close();

	{
		SCOPED_LOCK lock(expansionMutex);

		expansionChunkWriteInProgress = false;

		expansionRegionMarkEmpty(expansionWriteChunkThreadRegion, threadID);
#ifdef DEBUG_EXPANSION
		dumpExpansionDebug(threadID);
#endif
	}

	{
		SCOPED_LOCK lock(processQueueMutex);
		runningSpecialWorkers--;
		CONDITION_NOTIFY(specialWorkersExitCondition, lock);
	}
}

void sortExpansionRegion(std::list<ExpansionBufferRegion>::iterator& regionToSort, THREAD_ID threadID, SCOPED_LOCK& lock)
{
	if (regionToSort->length > EXPANSION_BUFFER_FILL_THRESHOLD)
	{
		ExpansionBufferRegion region;
		region.pos = regionToSort->pos + EXPANSION_BUFFER_FILL_THRESHOLD;
		region.length = regionToSort->length - EXPANSION_BUFFER_FILL_THRESHOLD;
		region.type = EXPANSION_BUFFER_REGION_FILLED;
		std::list<ExpansionBufferRegion>::iterator insert = regionToSort;
		expansionBufferRegions.insert(++insert, region);
		regionToSort->length = EXPANSION_BUFFER_FILL_THRESHOLD;
	}

	regionToSort->type = EXPANSION_BUFFER_REGION_SORTING;
	OpenNode *bufferToSort = EXPANSION_BUFFER + regionToSort->pos * EXPANSION_NODES_PER_QUEUE_ELEMENT;
	size_t count = regionToSort->length * EXPANSION_NODES_PER_QUEUE_ELEMENT;
	
#ifdef DEBUG_EXPANSION
	dumpExpansionDebug(threadID);
#endif

	numSortsInProgress++;
	lock.unlock();
	
	std::sort(bufferToSort, bufferToSort + count);
	count = deduplicate(bufferToSort, count);

	lock.lock();
	numSortsInProgress--;

	unsigned oldLength = regionToSort->length;
	unsigned newLength = (unsigned)((count + EXPANSION_NODES_PER_QUEUE_ELEMENT-1) / EXPANSION_NODES_PER_QUEUE_ELEMENT);
	regionToSort->length = newLength;
	regionToSort->type = EXPANSION_BUFFER_REGION_WRITING;
	std::list<ExpansionBufferRegion>::iterator nextRegion = regionToSort; ++nextRegion;
	if (nextRegion != expansionBufferRegions.end() && nextRegion->type == EXPANSION_BUFFER_REGION_EMPTY)
	{
		nextRegion->pos    -= oldLength - newLength;
		nextRegion->length += oldLength - newLength;
	}
	else
	if (oldLength > newLength)
	{
		ExpansionBufferRegion region;
		region.pos = regionToSort->pos + newLength;
		region.length = oldLength - newLength;
		region.type = EXPANSION_BUFFER_REGION_EMPTY;
		expansionBufferRegions.insert(nextRegion, region);
	}

#ifdef DEBUG_EXPANSION
	dumpExpansionDebug(threadID);
#endif

	while (expansionChunkWriteInProgress)
	{
		lock.unlock();
		SLEEP(1);
		lock.lock();
	}

	unsigned chunk = expansionChunks++;

	expansionWriteChunkThreadStream.open(formatFileName("expanded", currentFrameGroup, chunk), false);
	expansionWriteChunkThreadBuffer = bufferToSort;
	expansionWriteChunkThreadCount = count;
	expansionWriteChunkThreadRegion = regionToSort;

	expansionChunkWriteInProgress = true;

	{
		SCOPED_LOCK lock(processQueueMutex);
		runningSpecialWorkers++;
	}
	THREAD_CREATE<expansionWriteChunkThread>(0);
}

#ifdef ENABLE_EXPANSION_SPILLOVER

void sortExpansionSpilloverThread(THREAD_ID threadID)
{
	ExpansionBufferSortedRegion region;
	region.start = expansionThread[threadID].buffer;
	region.end = expansionThread[threadID].finalSortBufferEnd;

	std::sort(region.start, region.end);

	{
		SCOPED_LOCK lock(expansionMutex);
		expansionBufferRegionsToMerge.push(region);
	}

	{
		SCOPED_LOCK lock(processQueueMutex);
		runningSpecialWorkers--;
		CONDITION_NOTIFY(specialWorkersExitCondition, lock);
	}
}

void expansionWriteSpillover(OpenNode *bufferToWrite, size_t count)
{
	while (true)
	{
		if (!expansionSpilloverOutOpen)
		{
			expansionSpilloverOut.open(formatFileName("expansionSpillover", currentFrameGroup, expansionSpilloverChunkOut));
			expansionSpilloverChunkOutPos = 0;
			expansionSpilloverOutOpen = true;
		}

		fpos_t spilloverToNextChunk = expansionSpilloverChunkOutPos + count - SPILLOVER_CHUNK_SIZE * EXPANSION_NODES_PER_QUEUE_ELEMENT;
		if (spilloverToNextChunk > 0)
			count -= spilloverToNextChunk;

		expansionSpilloverOut.write(bufferToWrite, count);

		expansionSpilloverChunkOutPos += count;
		expansionSpilloverNodesQueued += count;

		if (spilloverToNextChunk >= 0)
		{
			expansionSpilloverOut.close();
			expansionSpilloverChunkOut++;
			expansionSpilloverChunkOutPos = 0;
			expansionSpilloverOutOpen = false;
		}
		if (spilloverToNextChunk <= 0)
			break;

		count = spilloverToNextChunk;
	}
}

void expansionReadSpillover(OpenNode *bufferToRead, size_t count)
{
	while (true)
	{
		debug_assert(!(expansionSpilloverChunkIn >  expansionSpilloverChunkOut ||
		               expansionSpilloverChunkIn == expansionSpilloverChunkOut && expansionSpilloverChunkInPos + count > expansionSpilloverChunkOutPos));

		bool reopen = false;
		if (expansionSpilloverOutOpen && expansionSpilloverChunkIn == expansionSpilloverChunkOut)
		{
			expansionSpilloverOut.close();
			reopen = true;
		}

		if (!expansionSpilloverInOpen)
		{
			expansionSpilloverIn.open(formatFileName("expansionSpillover", currentFrameGroup, expansionSpilloverChunkIn));
			if (expansionSpilloverChunkInPos)
				expansionSpilloverIn.seek(expansionSpilloverChunkInPos);
			expansionSpilloverInOpen = true;
		}

		fpos_t spilloverToNextChunk = expansionSpilloverChunkInPos + count - SPILLOVER_CHUNK_SIZE * EXPANSION_NODES_PER_QUEUE_ELEMENT;
		if (spilloverToNextChunk > 0)
			count -= spilloverToNextChunk;

		expansionSpilloverIn.read(bufferToRead, count);

		expansionSpilloverChunkInPos  += count;
		expansionSpilloverNodesQueued -= count;

		assert(expansionSpilloverNodesQueued >= 0);

		if (expansionSpilloverNodesQueued == 0)
		{
			expansionSpilloverIn.close();
			deleteFile(formatFileName("expansionSpillover", currentFrameGroup, expansionSpilloverChunkIn));
			expansionSpilloverOutOpen = false;
			expansionSpilloverChunkOut = 0;
			expansionSpilloverChunkOutPos = 0;
			expansionSpilloverInOpen = false;
			expansionSpilloverChunkIn = 0;
			expansionSpilloverChunkInPos = 0;
			break;
		}

		if (spilloverToNextChunk >= 0)
		{
			expansionSpilloverIn.close();
			deleteFile(formatFileName("expansionSpillover", currentFrameGroup, expansionSpilloverChunkIn));
			expansionSpilloverChunkIn++;
			expansionSpilloverChunkInPos = 0;
			expansionSpilloverInOpen = false;
		}
		else
		if (reopen)
		{
			if (expansionSpilloverInOpen)
			{
				expansionSpilloverIn.close();
				expansionSpilloverInOpen = false;
			}
			expansionSpilloverOut.open(formatFileName("expansionSpillover", currentFrameGroup, expansionSpilloverChunkOut), true);
		}

		if (spilloverToNextChunk <= 0)
			break;

		count = spilloverToNextChunk;
	}
}

struct
{
	OpenNode *buffer;
	size_t    count;
	std::list<ExpansionBufferRegion>::iterator region;
} expansionSpilloverThreadBuffer[2];

void expansionWriteSpilloverThread(THREAD_ID threadID)
{
	expansionWriteSpillover(expansionSpilloverThreadBuffer[0].buffer, expansionSpilloverThreadBuffer[0].count);
	if (expansionSpilloverThreadBuffer[1].buffer)
		expansionWriteSpillover(expansionSpilloverThreadBuffer[1].buffer, expansionSpilloverThreadBuffer[1].count);
	
	{
		SCOPED_LOCK lock(expansionMutex);

		expansionSpilloverLocked = false;

		expansionRegionMarkEmpty(expansionSpilloverThreadBuffer[0].region, threadID);
		if (expansionSpilloverThreadBuffer[1].buffer)
			expansionRegionMarkEmpty(expansionSpilloverThreadBuffer[1].region, threadID);
#ifdef DEBUG_EXPANSION
		dumpExpansionDebug(threadID);
#endif
	}
}

void expansionReadSpilloverThread(THREAD_ID threadID)
{
	SCOPED_LOCK lock(expansionMutex);

	while (true)
	{
		{
			SCOPED_LOCK lock(processQueueMutex);
			if (stopSpecialWorkers)
			{
				runningSpecialWorkers--;
				CONDITION_NOTIFY(specialWorkersExitCondition, lock);
				return;
			}
		}
		if (expansionSpilloverNodesQueued && !expansionSpilloverLocked && !expansionChunkWriteInProgress)
		{
			std::list<ExpansionBufferRegion>::iterator firstEmptyRegionToFill;
			bool foundEmptyRegionToFill = false;

			for (std::list<ExpansionBufferRegion>::iterator i=expansionBufferRegions.begin(); i != expansionBufferRegions.end(); i++)
			{
				if (i->type == EXPANSION_BUFFER_REGION_EMPTY)
				{
					if (!foundEmptyRegionToFill)
					{
						foundEmptyRegionToFill = true;
						firstEmptyRegionToFill = i;
						break;
					}
				}
			}

			if (foundEmptyRegionToFill && firstEmptyRegionToFill->length >= EXPANSION_SPILLOVER_READ_THRESHOLD)
			{
				size_t expansionSpilloverPresented = expansionSpilloverNodesQueued;
				if (expansionSpilloverPresented > EXPANSION_SPILLOVER_READ_THRESHOLD * EXPANSION_NODES_PER_QUEUE_ELEMENT)
					expansionSpilloverPresented = EXPANSION_SPILLOVER_READ_THRESHOLD * EXPANSION_NODES_PER_QUEUE_ELEMENT;

				size_t count = firstEmptyRegionToFill->length * EXPANSION_NODES_PER_QUEUE_ELEMENT;
				
				if (count <= expansionSpilloverPresented)
					firstEmptyRegionToFill->type = EXPANSION_BUFFER_REGION_READING;
				else
				{
					count = expansionSpilloverPresented;
					ExpansionBufferRegion region;
					region.pos = firstEmptyRegionToFill->pos;
					assert(count % EXPANSION_NODES_PER_QUEUE_ELEMENT == 0);
					region.length = (unsigned)(count / EXPANSION_NODES_PER_QUEUE_ELEMENT);
					region.type = EXPANSION_BUFFER_REGION_READING;
					firstEmptyRegionToFill->pos    += region.length;
					firstEmptyRegionToFill->length -= region.length;
					firstEmptyRegionToFill = expansionBufferRegions.insert(firstEmptyRegionToFill, region);
				}
	#ifdef DEBUG_EXPANSION
				dumpExpansionDebug(threadID);
	#endif

				OpenNode *buffer = EXPANSION_BUFFER + firstEmptyRegionToFill->pos * EXPANSION_NODES_PER_QUEUE_ELEMENT;

				expansionSpilloverLocked = true;
				lock.unlock();

				expansionReadSpillover(buffer, count);
		
				lock.lock();
				expansionSpilloverLocked = false;

				expansionRegionMarkFilled(firstEmptyRegionToFill, threadID);

				continue;
			}
		}

		lock.unlock();
		SLEEP(1);
		lock.lock();
	}
}

#endif

void expansionHandleFilledQueueElement(THREAD_ID threadID)
{
	SCOPED_LOCK lock(expansionMutex);

	expansionRegionMarkFilled(expansionThreadIter[threadID], threadID);
	expansionThread[threadID].buffer = NULL;

	while (true)
	{
		std::list<ExpansionBufferRegion>::iterator firstEmptyRegionToFill;
		bool foundEmptyRegionToFill = false;
		bool regionToFillAdjacentToFilledAdjacentToSortingBoundary = false;
		bool regionToFillAdjacentToSortingBoundary = false;
		bool lastRegionAdjacentToSortingBoundary = true;
		bool regionToFillAdjacentToFilled = false;
		bool lastRegionWasFilled = false;
		unsigned totalEmptyLength = 0;

		std::list<ExpansionBufferRegion>::iterator longestFilledRegionToSort;
		unsigned longestFilledLength = 0;
		
#ifdef ENABLE_EXPANSION_SPILLOVER
		std::list<ExpansionBufferRegion>::iterator rightmostFilledRegionToSpillover;
		std::list<ExpansionBufferRegion>::iterator secondRightmostFilledRegionToSpillover;
		bool foundRightmostFilledRegion = false;
		bool foundSecondRightmostFilledRegion = false;
#endif

		for (std::list<ExpansionBufferRegion>::iterator i=expansionBufferRegions.begin(); i != expansionBufferRegions.end(); i++)
		{
			if (i->type == EXPANSION_BUFFER_REGION_EMPTY)
			{
				if (!foundEmptyRegionToFill ||
					!regionToFillAdjacentToFilledAdjacentToSortingBoundary && lastRegionAdjacentToSortingBoundary ||
					!regionToFillAdjacentToFilledAdjacentToSortingBoundary && !regionToFillAdjacentToFilled && lastRegionWasFilled ||
					(!regionToFillAdjacentToFilledAdjacentToSortingBoundary || !regionToFillAdjacentToFilled) && lastRegionAdjacentToSortingBoundary && lastRegionWasFilled)
				{
					foundEmptyRegionToFill = true;
					firstEmptyRegionToFill = i;
					regionToFillAdjacentToFilledAdjacentToSortingBoundary = lastRegionAdjacentToSortingBoundary;
					regionToFillAdjacentToFilled = lastRegionWasFilled;
				}
				lastRegionAdjacentToSortingBoundary = false;
				lastRegionWasFilled = false;
				totalEmptyLength += i->length;
			}
			else
			if (i->type == EXPANSION_BUFFER_REGION_FILLED)
			{
				if (longestFilledLength < i->length)
				{
					longestFilledLength = i->length;
					longestFilledRegionToSort = i;
				}
#ifdef ENABLE_EXPANSION_SPILLOVER
				if (foundRightmostFilledRegion)
				{
					secondRightmostFilledRegionToSpillover = rightmostFilledRegionToSpillover;
					foundSecondRightmostFilledRegion = true;
				}
				rightmostFilledRegionToSpillover = i;
				foundRightmostFilledRegion = true;
#endif
				lastRegionAdjacentToSortingBoundary = lastRegionAdjacentToSortingBoundary || (i->pos % EXPANSION_BUFFER_FILL_THRESHOLD == 0);
				lastRegionWasFilled = true;
			}
			else
			if (INRANGEX(i->type, EXPANSION_BUFFER_REGION_FILLING, EXPANSION_BUFFER_REGION_FILLING+WORKERS))
			{
				lastRegionWasFilled = true;
			}
			else
			{
				lastRegionAdjacentToSortingBoundary = false;
				lastRegionWasFilled = false;
			}
		}

		if (longestFilledLength >= EXPANSION_BUFFER_FILL_THRESHOLD)
		{
			sortExpansionRegion(longestFilledRegionToSort, threadID, lock);
			continue;
		}

#ifdef ENABLE_EXPANSION_SPILLOVER
		if (totalEmptyLength <= EXPANSION_SPILLOVER_SLACK && longestFilledLength + EXPANSION_SPILLOVER_SLACK < EXPANSION_BUFFER_FILL_THRESHOLD
			&& !expansionSpilloverLocked && !expansionChunkWriteInProgress && foundRightmostFilledRegion)
		{
			std::list<ExpansionBufferRegion>::iterator& regionToWrite = rightmostFilledRegionToSpillover;
			debug_assert(regionToWrite->length != 0);

			if (regionToWrite->length <= EXPANSION_SPILLOVER_WRITE_THRESHOLD)
				regionToWrite->type = EXPANSION_BUFFER_REGION_WRITING;
			else
			{
				ExpansionBufferRegion region;
				region.pos = regionToWrite->pos + regionToWrite->length - EXPANSION_SPILLOVER_WRITE_THRESHOLD;
				region.length = EXPANSION_SPILLOVER_WRITE_THRESHOLD;
				region.type = EXPANSION_BUFFER_REGION_WRITING;

				regionToWrite->length -= EXPANSION_SPILLOVER_WRITE_THRESHOLD;

				if (++regionToWrite == expansionBufferRegions.end())
				{
					expansionBufferRegions.push_back(region);
					regionToWrite = expansionBufferRegions.end();
					regionToWrite--;
				}
				else
					regionToWrite = expansionBufferRegions.insert(regionToWrite, region);
			}

			expansionSpilloverThreadBuffer[0].buffer = EXPANSION_BUFFER + regionToWrite->pos * EXPANSION_NODES_PER_QUEUE_ELEMENT;
			expansionSpilloverThreadBuffer[0].count  = regionToWrite->length * EXPANSION_NODES_PER_QUEUE_ELEMENT;
			expansionSpilloverThreadBuffer[0].region = regionToWrite;
			expansionSpilloverThreadBuffer[1].buffer = NULL;

			if (foundSecondRightmostFilledRegion && regionToWrite->length < EXPANSION_SPILLOVER_WRITE_THRESHOLD)
			{
				std::list<ExpansionBufferRegion>::iterator& secondRegionToWrite = secondRightmostFilledRegionToSpillover;

				if (secondRegionToWrite->length + regionToWrite->length <= EXPANSION_SPILLOVER_WRITE_THRESHOLD)
					secondRegionToWrite->type = EXPANSION_BUFFER_REGION_WRITING;
				else
				{
					ExpansionBufferRegion region;
					region.pos = secondRegionToWrite->pos + secondRegionToWrite->length + regionToWrite->length - EXPANSION_SPILLOVER_WRITE_THRESHOLD;
					region.length = EXPANSION_SPILLOVER_WRITE_THRESHOLD - regionToWrite->length;
					region.type = EXPANSION_BUFFER_REGION_WRITING;

					secondRegionToWrite->length -= EXPANSION_SPILLOVER_WRITE_THRESHOLD - regionToWrite->length;

					if (++secondRegionToWrite == expansionBufferRegions.end())
					{
						expansionBufferRegions.push_back(region);
						secondRegionToWrite = expansionBufferRegions.end();
						secondRegionToWrite--;
					}
					else
						secondRegionToWrite = expansionBufferRegions.insert(regionToWrite, region);
				}

				expansionSpilloverThreadBuffer[1].buffer = EXPANSION_BUFFER + secondRegionToWrite->pos * EXPANSION_NODES_PER_QUEUE_ELEMENT;
				expansionSpilloverThreadBuffer[1].count  = secondRegionToWrite->length * EXPANSION_NODES_PER_QUEUE_ELEMENT;
				expansionSpilloverThreadBuffer[1].region = secondRegionToWrite;
			}

#ifdef DEBUG_EXPANSION
			dumpExpansionDebug(threadID);
#endif

			expansionSpilloverLocked = true;

			THREAD_CREATE<expansionWriteSpilloverThread>(0);

			continue;
		}
#endif

		if (foundEmptyRegionToFill)
		{
			ExpansionBufferRegion region;
			region.length = 1;
			region.type = (EXPANSION_BUFFER_REGION_TYPE)(EXPANSION_BUFFER_REGION_FILLING + threadID);

			region.pos = firstEmptyRegionToFill->pos;
			if (firstEmptyRegionToFill->length == 1)
			{
				firstEmptyRegionToFill->type = region.type;
				expansionThreadIter[threadID] = firstEmptyRegionToFill;
			}
			else
			{
				expansionThreadIter[threadID] = expansionBufferRegions.insert(firstEmptyRegionToFill, region);
				firstEmptyRegionToFill->pos++;
				firstEmptyRegionToFill->length--;
			}

			expansionThread[threadID].i = 0;
			expansionThread[threadID].increment = +1;

			debug_assert(expansionThread[threadID].buffer == NULL);
			expansionThread[threadID].buffer = EXPANSION_BUFFER + region.pos * EXPANSION_NODES_PER_QUEUE_ELEMENT;
	#ifdef DEBUG_EXPANSION
			dumpExpansionDebug(threadID);
	#endif
			break;
		}

		{
			SCOPED_LOCK lock(processQueueMutex);
			if (processQueueHead == processQueueTail && stopWorkers)
				return;
		}
		// wait until another thread empties queue element(s)
		lock.unlock();
		SLEEP(1);
		lock.lock();
	}
}

template<class NODE>
void writeOpenState(const NODE* state, FRAME frame, THREAD_ID threadID)
{
	if (frame > MAX_FRAMES)
		return;
	FRAME_GROUP group = frame/FRAMES_PER_GROUP;

	expansionThread[threadID].buffer[expansionThread[threadID].i].state = *state;
	expansionThread[threadID].buffer[expansionThread[threadID].i].frame = (PACKED_FRAME)frame;
	expansionThread[threadID].i += expansionThread[threadID].increment;
	if (expansionThread[threadID].i == (expansionThread[threadID].increment<0 ? -1 : EXPANSION_NODES_PER_QUEUE_ELEMENT))
		expansionHandleFilledQueueElement(threadID);
}

void expansionSortFinalRegions(THREAD_ID threadID)
{
	SCOPED_LOCK lock(expansionMutex);

	expansionThreadFinalized[threadID] = true;

sortNextFilledRegion:

	for (std::list<ExpansionBufferRegion>::iterator i=expansionBufferRegions.begin(); i!=expansionBufferRegions.end(); i++)
	{
		if (i->type != EXPANSION_BUFFER_REGION_FILLED)
			continue;

		numSortsInProgress++;

		size_t bufferOffset = 0;
		fpos_t countOffset = 0;

		if (numSortsInProgress < WORKERS)
		{
			unsigned oldLength = i->length;
			unsigned denominator = WORKERS - numSortsInProgress + 1;
			i->length = (i->length + denominator-1) / denominator;
			ExpansionBufferRegion region;
			region.pos = i->pos + i->length;
			region.length = oldLength - i->length;
			region.type = EXPANSION_BUFFER_REGION_FILLED;
			std::list<ExpansionBufferRegion>::iterator insert = i;
			expansionBufferRegions.insert(++insert, region);
		}
		else
		{
			// if there is a queue element that were still being filled when expansion ended, and it is adjacent to this region on the RIGHT side with no gap, treat it as part of this region for the purposes of sorting and merging
			std::list<ExpansionBufferRegion>::iterator after = i; ++after;
			if (after != expansionBufferRegions.end() && INRANGEX(after->type, EXPANSION_BUFFER_REGION_FILLING, EXPANSION_BUFFER_REGION_FILLING+WORKERS))
			{
				THREAD_ID threadID = after->type - EXPANSION_BUFFER_REGION_FILLING;
				if (expansionThreadFinalized[threadID] && expansionThread[threadID].increment == +1)
				{
					after->length = 0;
					i->length++;

					countOffset = expansionThread[threadID].i - EXPANSION_NODES_PER_QUEUE_ELEMENT;
				}
			}
		}
		// if there is a queue element that were still being filled when expansion ended, and it is adjacent to this region on the LEFT side with no gap, treat it as part of this region for the purposes of sorting and merging
		std::list<ExpansionBufferRegion>::iterator before = i;
		if (before != expansionBufferRegions.begin() && INRANGEX((--before)->type, EXPANSION_BUFFER_REGION_FILLING, EXPANSION_BUFFER_REGION_FILLING+WORKERS))
		{
			THREAD_ID threadID = before->type - EXPANSION_BUFFER_REGION_FILLING;
			if (expansionThreadFinalized[threadID] && expansionThread[threadID].increment == -1)
			{
				before->length = 0;
				i->pos--;
				i->length++;

				bufferOffset = expansionThread[threadID].i + 1;
				countOffset -= bufferOffset;
			}
		}

		i->type = EXPANSION_BUFFER_REGION_SORTING;
#ifdef DEBUG_EXPANSION
		dumpExpansionDebug(threadID);
#endif

		OpenNode* bufferToSort = EXPANSION_BUFFER + i->pos * EXPANSION_NODES_PER_QUEUE_ELEMENT + bufferOffset;
		size_t count = i->length * EXPANSION_NODES_PER_QUEUE_ELEMENT + countOffset;
		
		lock.unlock();

		std::sort(bufferToSort, bufferToSort + count);

		lock.lock();

		ExpansionBufferSortedRegion region;
		region.start = bufferToSort;
		region.end   = bufferToSort + count;
		expansionBufferRegionsToMerge.push(region);
		numSortsInProgress--;

#ifdef DEBUG_EXPANSION
		i->type = EXPANSION_BUFFER_REGION_MERGING;
		dumpExpansionDebug(threadID);
#endif

		goto sortNextFilledRegion;
	}

	if (expansionThreadIter[threadID]->length)
	{
		if (expansionThread[threadID].i != (expansionThread[threadID].increment<0 ? EXPANSION_NODES_PER_QUEUE_ELEMENT-1 : 0))
		{
			expansionThreadIter[threadID]->type = EXPANSION_BUFFER_REGION_SORTING;
			// this is such a small sort, that it should not be counted towards numSortsInProgress

			lock.unlock();

			OpenNode* buffer = expansionThread[threadID].buffer;
			size_t    count  = expansionThread[threadID].i;
			if (expansionThread[threadID].increment < 0)
			{
				buffer += expansionThread[threadID].i + 1;
				count = EXPANSION_NODES_PER_QUEUE_ELEMENT-1 - expansionThread[threadID].i;
			}

			std::sort(buffer, buffer + count);
			
			ExpansionBufferSortedRegion region;
			region.start = buffer;
			region.end = buffer + count;

			lock.lock();

			expansionBufferRegionsToMerge.push(region);
#ifdef DEBUG_EXPANSION
			expansionThreadIter[threadID]->type = EXPANSION_BUFFER_REGION_MERGING;
			dumpExpansionDebug(threadID);
#endif
		}
		else
		{
#ifdef DEBUG_EXPANSION
			expansionThreadIter[threadID]->type = EXPANSION_BUFFER_REGION_EMPTY;
			dumpExpansionDebug(threadID);
#endif
		}
	}
}

void expansionMergeRegionsToDisk()
{
	unsigned numInputs = 0;
	MemoryInputStream<OpenNode>* inputs = new MemoryInputStream<OpenNode> [expansionBufferRegionsToMerge.size()];
	while (!expansionBufferRegionsToMerge.empty())
	{
		std::queue<ExpansionBufferSortedRegion>::reference region = expansionBufferRegionsToMerge.front();
		inputs[numInputs++].open(region.start, region.end);
		expansionBufferRegionsToMerge.pop();
	}

	BufferedOutputStream<OpenNode> output(STANDARD_BUFFER_SIZE); // allocate buffer outside of "ram"; reserve "ram" exclusively for expansion
	output.open(formatFileName("expanded", currentFrameGroup, expansionChunks));

	mergeStreams<OpenNode>(inputs, numInputs, &output);

	expansionChunks++;
}

void expansionWriteFinalChunk()
{
#ifdef DEBUG_EXPANSION
	{
		timeb time1;
		ftime(&time1);
		fprintf(expansionDebug, "Merging sorted region(s) to disk.\n", time1.time, time1.millitm, WORKERS);
		fflush(expansionDebug);
	}
#endif

	expansionBufferRegions.clear();

	if (!expansionBufferRegionsToMerge.empty())
		expansionMergeRegionsToDisk();

#ifdef ENABLE_EXPANSION_SPILLOVER
	if (expansionSpilloverOutOpen)
		expansionSpilloverOut.close();

	while (expansionSpilloverNodesQueued)
	{
		size_t count = OPENNODE_BUFFER_SIZE;
		if (count > expansionSpilloverNodesQueued)
			count = expansionSpilloverNodesQueued;
#ifdef DEBUG_EXPANSION
		{
			timeb time1;
			ftime(&time1);
			fprintf(expansionDebug, "%9d.%03d: Reading %llu nodes of spillover...\n", time1.time, time1.millitm, count);
			fflush(expansionDebug);
		}
#endif

		expansionReadSpillover(EXPANSION_BUFFER, count);

		size_t numerator = 0;
		for (THREAD_ID threadID=0; threadID<WORKERS; threadID++)
		{
			expansionThread[threadID].buffer             = EXPANSION_BUFFER + numerator/WORKERS;
			numerator += count;
			expansionThread[threadID].finalSortBufferEnd = EXPANSION_BUFFER + numerator/WORKERS;
		}

#ifdef DEBUG_EXPANSION
		{
			timeb time1;
			ftime(&time1);
			fprintf(expansionDebug, "%9d.%03d: Sorting spillover with %u threads...\n", time1.time, time1.millitm, WORKERS);
			fflush(expansionDebug);
		}
#endif

		{
			SCOPED_LOCK lock(processQueueMutex);
			runningSpecialWorkers += WORKERS;
		}
		for (THREAD_ID threadID=0; threadID<WORKERS; threadID++)
			THREAD_CREATE<sortExpansionSpilloverThread>(threadID);
		{
			SCOPED_LOCK lock(processQueueMutex);
			while (runningSpecialWorkers)
				CONDITION_WAIT(specialWorkersExitCondition, lock);
		}

#ifdef DEBUG_EXPANSION
		{
			timeb time1;
			ftime(&time1);
			fprintf(expansionDebug, "%9d.%03d: Merging sorted spillover to disk...\n", time1.time, time1.millitm);
			fflush(expansionDebug);
		}
#endif

		expansionMergeRegionsToDisk();
	}
#endif

#ifdef DEBUG_EXPANSION
	{
		timeb time1;
		ftime(&time1);
		fprintf(expansionDebug, "%9d.%03d: Finished writing final expansion chunk.\n", time1.time, time1.millitm);
		fflush(expansionDebug);
	}
#endif

#ifdef DEBUG_EXPANSION
	fclose(expansionDebug);
#endif
}

void mergeExpanded()
{
	if (expansionChunks>1)
	{
		BufferedOutputStream<OpenNode>* output = new BufferedOutputStream<OpenNode>;
		BufferedInputStream<OpenNode>* inputs = new BufferedInputStream<OpenNode>[expansionChunks];
		
		double outbuf_inbuf_ratio = sqrt(EXPECTED_MERGING_RATIO * expansionChunks);
		uint32_t bufferSize = (uint32_t)floor(OPENNODE_BUFFER_SIZE / (expansionChunks + outbuf_inbuf_ratio));
		
		if (expansionChunks <= OPENNODE_BUFFER_SIZE && bufferSize && (expansionChunks+1)*bufferSize <= OPENNODE_BUFFER_SIZE)
		{
			output->setWriteBuffer((OpenNode*)ram + expansionChunks*bufferSize, OPENNODE_BUFFER_SIZE - expansionChunks*bufferSize);
			for (unsigned i=0; i<expansionChunks; i++)
				inputs[i].setReadBuffer((OpenNode*)ram + i*bufferSize, (uint32_t)bufferSize);
		}
		else
			output->setWriteBuffer((OpenNode*)ram, OPENNODE_BUFFER_SIZE);

		for (unsigned i=0; i<expansionChunks; i++)
			inputs[i].open(formatFileName("expanded", currentFrameGroup, i));
		
		output->open(formatFileName("merging", currentFrameGroup));

		mergeStreams<OpenNode>(inputs, expansionChunks, output);
		
		delete[] inputs;
		delete output;

		renameFile(formatFileName("merging", currentFrameGroup), formatFileName("expanded", currentFrameGroup));
#ifndef KEEP_PAST_FILES
		for (unsigned i=0; i<expansionChunks; i++)
			deleteFile(formatFileName("expanded", currentFrameGroup, i));
#endif
	}
	else
	if (expansionChunks)
		renameFile(formatFileName("expanded", currentFrameGroup, 0), formatFileName("expanded", currentFrameGroup));
	else
		OutputStream<OpenNode> output(formatFileName("expanded", currentFrameGroup), false); // create zero byte file

	expansionChunks = 0;
}

// *********************************************** Cache ************************************************

INLINE uint32_t hashState(const CompressedState* state)
{
	// Based on MurmurHash ( http://murmurhash.googlepages.com/MurmurHash2.cpp )
	
	const uint32_t m = 0x5bd1e995;
	const int r = 24;

	uint32_t h = sizeof(CompressedState);

	const uint32_t* data = (const uint32_t*)state;

	for (int i=0; i<sizeof(CompressedState)/4; i++) // should unroll
	{
		uint32_t k = *data;

		k *= m; 
		k ^= k >> r; 
		k *= m; 
		
		h *= m; 
		h ^= k;

		data ++;
	}
	
	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
}

void addState(const CompressedState* cs, FRAME frame, THREAD_ID threadID)
{
	writeOpenState(cs, frame, threadID);
}

// ******************************************** Exit tracing ********************************************

CompressedState exitSearchCompressedState;
State exitSearchState     , exitSearchStateParent     ;
FRAME exitSearchStateFrame, exitSearchStateParentFrame;
Step exitSearchStateStep;
bool exitSearchStateFound = false;
int exitSearchFrameGroup; // allow negative
#ifdef MULTITHREADING
MUTEX exitSearchStateMutex;
#endif
#ifdef DEBUG
int statesQueued, statesDequeued;
#endif

class FinishCheckChildHandler
{
public:
	enum { PREFERRED = PREFERRED_STATE_NEITHER };

	static INLINE void handleChild(const State* parent, FRAME parentFrame, Step step, const State* state, FRAME frame, THREAD_ID threadID)
	{
		if (*state==exitSearchState && frame==exitSearchStateFrame)
			found(step, parent, parentFrame);
	}

	static INLINE void handleChild(const State* parent, FRAME parentFrame, Step step, const CompressedState* state, FRAME frame, THREAD_ID threadID)
	{
		if (*state==exitSearchCompressedState && frame==exitSearchStateFrame)
			found(step, parent, parentFrame);
	}

	static void found(Step step, const State* parent, FRAME parentFrame)
	{
#ifdef MULTITHREADING
		SCOPED_LOCK lock(exitSearchStateMutex);
#endif
		exitSearchStateFound       = true;
		exitSearchStateStep        = step;
		exitSearchStateParent      = *parent;
		exitSearchStateParentFrame = parentFrame;
	}
};

INLINE void processExitState(const Node* cs, THREAD_ID threadID)
{
	State state;
	state.decompress(&cs->getState());
	FRAME frame = GET_FRAME(exitSearchFrameGroup, *cs);
	expandChildren<FinishCheckChildHandler>(frame, &state, threadID);

#ifdef DEBUG
#ifdef MULTITHREADING
	static MUTEX mutex;
	SCOPED_LOCK lock(mutex);
#endif
	statesDequeued++;
#endif
}

void saveExitTrace(Step *steps, int stepNr)
{
	FILE* f = fopen(formatFileName("solution"), "wb");
	fwrite(&exitSearchFrameGroup, sizeof(exitSearchFrameGroup), 1, f);
	fwrite(&exitSearchStateFrame, sizeof(exitSearchStateFrame), 1, f);
	fwrite(&exitSearchState     , sizeof(exitSearchState)     , 1, f);
	fwrite(&stepNr              , sizeof(stepNr)              , 1, f);
	fwrite(steps, sizeof(Step), stepNr, f);
	fclose(f);
}

void traceExit(const State* exitState, FRAME exitFrame)
{
	Step steps[MAX_STEPS];
	int stepNr = 0;
	
	if (fileExists(formatFileName("solution")))
	{
		FILE* f = fopen(formatFileName("solution"), "rb");
		fread(&exitSearchFrameGroup, sizeof(exitSearchFrameGroup), 1, f);
		fread(&exitSearchStateFrame, sizeof(exitSearchStateFrame), 1, f);
		fread(&exitSearchState     , sizeof(exitSearchState)     , 1, f);
		fread(&stepNr              , sizeof(stepNr)              , 1, f);
		fread(steps, sizeof(Step), stepNr, f);
		fclose(f);

		printTime();
		printf("Resuming exit trace from frame" GROUP_STR " " GROUP_FORMAT "...\n", exitSearchFrameGroup);

		exitSearchFrameGroup++;

		if (exitSearchFrameGroup < 0)
			goto found;
	}
	else
	if (exitState)
	{
		exitSearchState      = *exitState;
		exitSearchStateFrame =  exitFrame;
		exitSearchFrameGroup =  exitFrame / FRAMES_PER_GROUP;
	}
	else
		error("Can't resume exit tracing - partial trace solution file not found");
	
	while (exitSearchFrameGroup >= 0)
	{
		exitSearchFrameGroup--;

		if (exitSearchFrameGroup < 0)
			goto found;

		if (fileExists(formatFileName("closed", exitSearchFrameGroup)))
		{
			saveExitTrace(steps, stepNr);

			printTime();
			printf("Frame" GROUP_STR " " GROUP_FORMAT "... \r", exitSearchFrameGroup);

			exitSearchState.compress(&exitSearchCompressedState);
			exitSearchStateFound = false;
				
#ifdef MULTITHREADING
			startWorkers<&processExitState,&doNothing>();
#endif

			BufferedInputStream<Node> input(formatFileName("closed", exitSearchFrameGroup));
			const Node *cs;
			DEBUG_ONLY(statesQueued = statesDequeued = 0);
			while (cs = input.read())
			{
				DEBUG_ONLY(statesQueued++);
				if (canStatesBeParentAndChild(&cs->getState(), &exitSearchCompressedState))
				{
#ifdef MULTITHREADING
					queueState(cs);
#else
					processExitState(cs);
#endif
				}
				if (exitSearchStateFound)
					break;
			}
			
#ifdef MULTITHREADING
			flushProcessingQueue();
#endif
			debug_assert(statesQueued == statesDequeued, format("Queued %d states but dequeued only %d!", statesQueued, statesDequeued));

			if (exitSearchStateFound)
			{
				printTime(); printf("Found (at %d)!          \n", exitSearchStateParentFrame);
				steps[stepNr++]      = exitSearchStateStep;
				exitSearchState      = exitSearchStateParent;
				exitSearchStateFrame = exitSearchStateParentFrame;
				if (exitSearchFrameGroup == 0)
				{
					exitSearchFrameGroup--;
					saveExitTrace(steps, stepNr);
					goto found;
				}
			}
		}
	}
	error("Lost parent node!");
found:
    
	printf("Transcribing solution.\n");
	writeSolution(&exitSearchState, steps, stepNr);
    //deleteFile(formatFileName("solution"));
}

// **************************************** Common runmode code *****************************************

#if 0
size_t ramUsed;

void sortAndMerge(FRAME_GROUP g)
{
	// Step 1: read chunks of BUFFER_SIZE nodes, sort+dedup them in RAM and write them to disk
	int chunks = 0;

	printf("Sorting... "); fflush(stdout);
	{
		InputStream<Node> input(formatFileName("open", g));
		uint64_t amount = input.size();
		if (amount > BUFFER_SIZE)
			amount = BUFFER_SIZE;
		size_t records;
		while (records = input.read(buffer, (size_t)amount))
		{
			if (ramUsed < records * sizeof(Node))
				ramUsed = records * sizeof(Node);
			std::sort(buffer, buffer + records);
			records = deduplicate(buffer, records);
			OutputStream output(formatFileName("chunk", g, chunks));
			output.write(buffer, records);
			chunks++;
		}
	}

	// Step 2: merge + dedup chunks
	printf("Merging... "); fflush(stdout);
	if (chunks>1)
	{
		ramUsed = RAM_SIZE;
		double outbuf_inbuf_ratio = sqrt(EXPECTED_MERGING_RATIO * chunks);
		size_t bufferSize = (size_t)floor(RAM_SIZE / ((chunks + outbuf_inbuf_ratio) * sizeof(Node)));
		
		BufferedInputStream<Node>* chunkInput = new BufferedInputStream<Node>[chunks];
		for (int i=0; i<chunks; i++)
		{
			chunkInput[i].setReadBuffer(buffer + i*bufferSize, (uint32_t)bufferSize);
			chunkInput[i].open(formatFileName("chunk", g, i));
		}
		BufferedOutputStream<Node>* output = new BufferedOutputStream<Node>;
		output->setWriteBuffer(buffer + chunks*bufferSize, (uint32_t)floor(bufferSize * outbuf_inbuf_ratio));
		output->open(formatFileName("merging", g));
		mergeStreams(chunkInput, chunks, output);
		delete[] chunkInput;
		delete output;
		renameFile(formatFileName("merging", g), formatFileName("merged", g));
		//for (int i=0; i<chunks; i++)
		//	deleteFile(formatFileName("chunk", g, i));
	}
	else
	{
		renameFile(formatFileName("chunk", g, 0), formatFileName("merged", g));
	}
}

// use closed/all files implicitly, optionally update all file by merging input with existing all/closed files
template <bool updateAll>
void filterAgainstClosed(BufferedRewriteStream<Node> open[], int openCount)
{
	BufferedInputStream* closed = new BufferedInputStream[MAX_FRAME_GROUPS];
	FRAME_GROUP lastAll = -1, maxClosed = -1;
	for (FRAME_GROUP g=MAX_FRAME_GROUPS-1; g>=0; g--)
#ifdef USE_ALL
		if (fileExists(formatFileName("all", g)))
		{
			closed[g].setReadBufferSize(ALL_FILE_BUFFER_SIZE);
			closed[g].open(formatFileName("all", g));
			lastAll = g;
			break;
		}
		else
#endif
		if (fileExists(formatFileName("closed", g)))
		{
			closed[g].open(formatFileName("closed", g));
			if (maxClosed == -1)
				maxClosed = g;
		}

	{
		InputHeap<BufferedInputStream> closedHeap(closed, MAX_FRAME_GROUPS);
		
		if (updateAll)
		{
#ifdef USE_ALL
			enforce(maxClosed >= 0, "No closed node files found");
			enforce(maxClosed > lastAll, "All file already up-to-date");
			{
				BufferedOutputStream all(formatFileName("allnew", maxClosed));
				filterStreams(&closedHeap, open, openCount, &all);
			}
			renameFile(formatFileName("allnew", maxClosed), formatFileName("all", maxClosed));
			if (lastAll>=0)
				deleteFile(formatFileName("all", lastAll));
#else
			error("USE_ALL not enabled");
#endif
		}
		else
			filterStreams(&closedHeap, open, openCount, &nullOutput);
	}
	delete[] closed;
	
	for (FRAME_GROUP g=0; g<openCount; g++) // on success, truncate open nodes manually
		if (open[g].isOpen())
		{
			open[g].flush();
			open[g].truncate();
		}
}
#endif

bool checkStop(bool newline=false)
{
	if (fileExists(formatProblemFileName("stop", NULL, "txt")))
	{
		deleteFile(formatProblemFileName("stop", NULL, "txt"));
		if (newline) putchar('\n');
		printTime(); printf("Stop file found.\n");
		return true;
	}
	return false;
}

#if 0
FRAME_GROUP lastAll()
{
	for (FRAME_GROUP g=MAX_FRAME_GROUPS-1; g>=0; g--)
		if (fileExists(formatFileName("all", g)))
			return g;
	error("All file not found!");
	return 0; // compiler complains
}
#endif

enum // exit reasons
{
	EXIT_OK,
	EXIT_STOP,
	EXIT_NOTFOUND,
	EXIT_ERROR
};

// Forward declarations
int doSortOpen(FRAME_GROUP firstFrameGroup, FRAME_GROUP maxFrameGroups);
int filterOpen();
void doFilterOpen(FRAME_GROUP firstFrameGroup, FRAME_GROUP maxFrameGroups);

// *********************************************** Search ***********************************************

FRAME_GROUP firstFrameGroup, maxFrameGroups;

bool exitFound;
FRAME exitFrame;
State exitState;
#ifdef MULTITHREADING
MUTEX finishMutex;
#endif

INLINE bool finishCheck(const State* s, FRAME frame)
{
	if (s->isFinish())
	{
#ifdef MULTITHREADING
		SCOPED_LOCK lock(finishMutex);
#endif
		if (exitFound)
		{
			if (exitFrame > frame)
			{
				exitFrame = frame;
				exitState = *s;
			}
		}
		else
		{
			exitFound = true;
			exitFrame = frame;
			exitState = *s;
		}
		return true;
	}
	return false;
}

void processState(const Node* cs, THREAD_ID threadID)
{
	State s;
	s.decompress(&cs->getState());
#ifdef HAVE_VALIDATOR
	if (!s.validate())
		return;
#endif
#ifdef DEBUG
	CompressedState test;
	s.compress(&test);
	if (test != cs->getState())
	{
		puts("");
		puts(hexDump(cs, sizeof(CompressedState)));
		puts(cs->getState().toString());
		puts(hexDump(&s, sizeof(State)));
		puts(s.toString());
		puts(hexDump(&test, sizeof(CompressedState)));
		puts(test.toString());
		error("Compression/decompression failed");
	}
#endif
	FRAME currentFrame = GET_FRAME(currentFrameGroup, *cs);
	if (finishCheck(&s, currentFrame))
		return;

	class AddStateChildHandler
	{
	public:
		enum { PREFERRED = PREFERRED_STATE_COMPRESSED };

		static INLINE void handleChild(const State* parent, FRAME parentFrame, Step step, const CompressedState* cs, FRAME frame, THREAD_ID threadID)
		{
			addState(cs, frame, threadID);
		}

		static INLINE void handleChild(const State* parent, FRAME parentFrame, Step step, const State* state, FRAME frame, THREAD_ID threadID)
		{
			CompressedState cs;
			state->compress(&cs);
			addState(&cs, frame, threadID);
		}
	};

	expandChildren<AddStateChildHandler>(currentFrame, &s, threadID);
	assert(currentFrame/FRAMES_PER_GROUP == currentFrameGroup, format("Run-away currentFrameGroup: currentFrame=%u, currentFrameGroup=%u", currentFrame, currentFrameGroup));
}

INLINE void processFilteredState(const Node* state)
{
	//closedNodesInCurrentFrameGroup++;
#ifdef MULTITHREADING
	queueState(state);
#else
	processState(state, 0);
#endif
}

class ProcessStateOutput
{
public:
	INLINE static void write(const Node* state, bool verify=false)
	{
		processFilteredState(state);
	}
	INLINE static void write(const OpenNode* node, bool verify=false)
	{
		combinedNodesTotal++;
		if (node->frame / FRAMES_PER_GROUP == currentFrameGroup+1)
		{
			Node cs;
			(PackedCompressedState&)cs = node->state;
			cs.subframe = node->frame % FRAMES_PER_GROUP;
			write(&cs);
		}
	}
	enum { WRITABLE = true };
};

class ClosedNodeFilterOutput
{
public:
	INLINE static void write(const OpenNode* node, bool verify=false)
	{
		combinedNodesTotal++;
		if (node->frame / FRAMES_PER_GROUP == currentFrameGroup+1)
		{
			Node cs;
			(PackedCompressedState&)cs = node->state;
			cs.subframe = node->frame % FRAMES_PER_GROUP;
			closedNodeFile.write(&cs);
			closedNodesInCurrentFrameGroup++;
		}
	}
	enum { WRITABLE = true };
};

void searchPrintHeader()
{
	printf("Frame" GROUP_STR " " GROUP_ALIGNED_FORMAT "/" GROUP_ALIGNED_FORMAT ": ", currentFrameGroup, maxFrameGroups);
	fflush(stdout);
}

void searchPrintNodeCounts()
{
	printf("%11llu nodes, %12llu total", closedNodesInCurrentFrameGroup, combinedNodesTotal);
	fflush(stdout);
}

void searchRecalculateNodeCounts()
{
	{
		InputStream<Node> getSize(formatFileName("closed", currentFrameGroup));
		closedNodesInCurrentFrameGroup = getSize.size();
	}
	{
		InputStream<OpenNode> getSize(formatFileName("combined", currentFrameGroup));
		combinedNodesTotal = getSize.size();
	}
}

const size_t relativeSizeClosed      =   2;
const size_t relativeSizeExpanded    =  11;
const size_t relativeSizeCombined    = 100;
const size_t relativeSizeCombinedNew = 101;

int search()
{
	if (fileExists(formatProblemFileName(NULL, NULL, "txt")))
	{
		printf("Solution already found.\n");
		return EXIT_OK;
	}

	if (fileExists(formatFileName("solution")))
	{
		printf("Partial trace solution file present, resuming exit trace...\n");
		traceExit(NULL, 0);
		return EXIT_OK;
	}

	for (currentFrameGroup=MAX_FRAME_GROUPS; currentFrameGroup>=0; currentFrameGroup--)
		if (fileExists(formatFileName("combined", currentFrameGroup)))
		{
			printTime();
			printf("Resuming from frame" GROUP_STR " " GROUP_FORMAT "\n", currentFrameGroup);
			break;
	    }

	timeb time0;
	ftime(&time0);

	timeb time1 = time0;
	timeb time2;
	timeb time3;

	if (currentFrameGroup == -1)
	{
		currentFrameGroup = 0;

		printTime();
		printf("Starting search\n");

		{
			OpenNode* initialCompressedStates = (OpenNode*)ram;
			for (int i=0; i<initialStateCount; i++)
			{
				initialCompressedStates[i].frame = 0;
				initialStates[i].compress(&initialCompressedStates[i].getState());
			}
			std::sort(initialCompressedStates, initialCompressedStates + initialStateCount);
			combinedNodesTotal = deduplicate(initialCompressedStates, initialStateCount);

			OutputStream<OpenNode> output(formatFileName("combining", currentFrameGroup), false);
			output.write(initialCompressedStates, combinedNodesTotal);
		}
		{
			Node* initialCompressedStates = (Node*)ram;
			for (int i=0; i<initialStateCount; i++)
			{
				initialCompressedStates[i].subframe = 0;
				initialStates[i].compress(&initialCompressedStates[i].getState());
			}
			std::sort(initialCompressedStates, initialCompressedStates + initialStateCount);
			closedNodesInCurrentFrameGroup = deduplicate(initialCompressedStates, initialStateCount);

			OutputStream<Node> output(formatFileName("closing", currentFrameGroup), false);
			output.write(initialCompressedStates, closedNodesInCurrentFrameGroup);
		}
		renameFile(formatFileName("combining", currentFrameGroup), formatFileName("combined", currentFrameGroup));
		renameFile(formatFileName("closing", currentFrameGroup), formatFileName("closed", currentFrameGroup));
	}
	else
	if (fileExists(formatFileName("expanded", currentFrameGroup)))
	{
		searchRecalculateNodeCounts();

		searchPrintHeader();
		searchPrintNodeCounts();

		printf("; (Resuming)                                                        "); fflush(stdout);

		time3 = time1;

		goto skipToCombining;
	}
	else
	if (fileExists(formatFileName("expandedcount", currentFrameGroup)))
	{
		searchRecalculateNodeCounts();
		
		searchPrintHeader();
		searchPrintNodeCounts();

		printf("; (Resuming)              "); fflush(stdout);

		InputStream<unsigned> resumeInfo(formatFileName("expandedcount", currentFrameGroup));
		resumeInfo.read(&expansionChunks, 1);

		time2 = time1;

		goto skipToMerging;
	}
	else	
	if (fileExists(formatFileName("closed", currentFrameGroup)))
	{
		searchRecalculateNodeCounts();
	}
	else
	{
		searchPrintHeader();

		closedNodesInCurrentFrameGroup = 0;
		combinedNodesTotal = 0;
		
		printf("Extracting..."); fflush(stdout);

		const size_t sizeClosed   = (OPENNODE_BUFFER_SIZE             ) * relativeSizeClosed   / (relativeSizeClosed + relativeSizeExpanded) ?
		                            (OPENNODE_BUFFER_SIZE             ) * relativeSizeClosed   / (relativeSizeClosed + relativeSizeExpanded) : 1;
		const size_t sizeExpanded = (OPENNODE_BUFFER_SIZE - sizeClosed) * relativeSizeExpanded / (                     relativeSizeExpanded) ?
		                            (OPENNODE_BUFFER_SIZE - sizeClosed) * relativeSizeExpanded / (                     relativeSizeExpanded) : 1;

		closedNodeFile.setWriteBuffer((Node*)ram, sizeClosed * sizeof(OpenNode) / sizeof(Node));
		closedNodeFile.open(formatFileName("closing", currentFrameGroup), false);

		ClosedNodeFilterOutput output;

		BufferedInputStream<OpenNode> input;
		input.setReadBuffer((OpenNode*)ram + sizeClosed, sizeExpanded);
		input.open(formatFileName("combined", currentFrameGroup));

		copyStream<OpenNode>(&input, &output);

		closedNodeFile.flush();
		closedNodeFile.close();
		closedNodeFile.clearBuffer(); // prevent bytes from Nodes from becoming junk inside OpenNode padding
		renameFile(formatFileName("closing", currentFrameGroup), formatFileName("closed", currentFrameGroup));

		putchar('\n');
	}

	for (;; currentFrameGroup++)
	{
		searchPrintHeader();
		searchPrintNodeCounts();

		if (currentFrameGroup >= maxFrameGroups)
			break;

		if (checkStop(true))
			return EXIT_STOP;

		printf("; Expanding..."); fflush(stdout);

		{
			BufferedInputStream<Node> input(CLOSED_IN_BUFFER_SIZE); // allocate buffer outside of "ram"; reserve "ram" exclusively for expansion
			input.open(formatFileName("closed", currentFrameGroup));

			ProcessStateOutput output;

			initExpansion();

#ifdef MULTITHREADING
			startWorkers<&processState,&expansionSortFinalRegions>();
#endif
			copyStream<Node>(&input, &output);
#ifdef MULTITHREADING
			flushProcessingQueue();
#endif

			expansionWriteFinalChunk();

			//input.clearBuffer(); // prevent bytes from Nodes from becoming junk inside OpenNode padding
		}

		{
			OutputStream<unsigned> resumeInfo(formatFileName("expandedcount", currentFrameGroup), false);
			resumeInfo.write(&expansionChunks, 1);
		}
		if (closedNodesInCurrentFrameGroup==0)
			deleteFile(formatFileName("closed", currentFrameGroup));

		if (exitFound)
		{
			assert(currentFrameGroup == exitFrame / FRAMES_PER_GROUP);
			putchar('\n');
			printTime();
			printf("Exit found (at frame %u), tracing path...\n", exitFrame);
			traceExit(&exitState, exitFrame);
			return EXIT_OK;
		}

		ftime(&time2);
		{
			time_t ms = (time2.time - time1.time)*1000 + (time2.millitm - time1.millitm);
			printf("%4d.%03d s", ms/1000, ms%1000);
		}

		if (checkStop(true))
			return EXIT_STOP;

		printf("; ");

	skipToMerging:

		printf("Merging..."); fflush(stdout);
		mergeExpanded();

#ifndef KEEP_PAST_FILES
		deleteFile(formatFileName("expandedcount", currentFrameGroup));
#endif

		ftime(&time3);
		{
			InputStream<Node> getSize(formatFileName("expanded", currentFrameGroup));
			uint64_t expandedNodes = getSize.size();

			time_t ms = (time3.time - time2.time)*1000 + (time3.millitm - time2.millitm);
			printf("%4d.%03d s, %12llu nodes", ms/1000, ms%1000, expandedNodes);
		}

		if (checkStop(true))
			return EXIT_STOP;

		printf("; ");

	skipToCombining:

		closedNodesInCurrentFrameGroup = 0;
		combinedNodesTotal = 0;
		
		printf("Combining..."); fflush(stdout);

		const size_t sizeClosed   =    (OPENNODE_BUFFER_SIZE                                           ) * relativeSizeClosed      / (relativeSizeClosed + relativeSizeExpanded + relativeSizeCombined + relativeSizeCombinedNew) ?
		                               (OPENNODE_BUFFER_SIZE                                           ) * relativeSizeClosed      / (relativeSizeClosed + relativeSizeExpanded + relativeSizeCombined + relativeSizeCombinedNew) : 1;
		const size_t sizeExpanded =    (OPENNODE_BUFFER_SIZE - sizeClosed                              ) * relativeSizeExpanded    / (                     relativeSizeExpanded + relativeSizeCombined + relativeSizeCombinedNew) ?
		                               (OPENNODE_BUFFER_SIZE - sizeClosed                              ) * relativeSizeExpanded    / (                     relativeSizeExpanded + relativeSizeCombined + relativeSizeCombinedNew) : 1;
		const size_t sizeCombined =    (OPENNODE_BUFFER_SIZE - sizeClosed - sizeExpanded               ) * relativeSizeCombined    / (                                            relativeSizeCombined + relativeSizeCombinedNew) ?
		                               (OPENNODE_BUFFER_SIZE - sizeClosed - sizeExpanded               ) * relativeSizeCombined    / (                                            relativeSizeCombined + relativeSizeCombinedNew) : 1;
		const size_t sizeCombinedNew = (OPENNODE_BUFFER_SIZE - sizeClosed - sizeExpanded - sizeCombined) * relativeSizeCombinedNew / (                                                                   relativeSizeCombinedNew) ?
		                               (OPENNODE_BUFFER_SIZE - sizeClosed - sizeExpanded - sizeCombined) * relativeSizeCombinedNew / (                                                                   relativeSizeCombinedNew) : 1;

		closedNodeFile.setWriteBuffer((Node*)ram, sizeClosed * sizeof(OpenNode) / sizeof(Node));
		closedNodeFile.open(formatFileName("closing", currentFrameGroup+1), false);

		{
			BufferedInputStream<OpenNode> inputs[2];
			DoubleOutput<OpenNode, ClosedNodeFilterOutput, BufferedOutputStream<OpenNode>> output;

			inputs[1].setReadBuffer((OpenNode*)ram + sizeClosed, sizeExpanded);
			inputs[1].open(formatFileName("expanded", currentFrameGroup));

			inputs[0].setReadBuffer((OpenNode*)ram + sizeClosed + sizeExpanded, sizeCombined);
			inputs[0].open(formatFileName("combined", currentFrameGroup));

			output.b()->setWriteBuffer((OpenNode*)ram + sizeClosed + sizeExpanded + sizeCombined, sizeCombinedNew);
			output.b()->open(formatFileName("combining", currentFrameGroup+1), false);

			mergeStreams<OpenNode>(inputs, 2, &output);
		}

		closedNodeFile.close();
		closedNodeFile.clearBuffer(); // prevent bytes from Nodes from becoming junk inside OpenNode padding
		renameFile(formatFileName("closing", currentFrameGroup+1), formatFileName("closed", currentFrameGroup+1));
#ifndef KEEP_PAST_FILES
		deleteFile(formatFileName("combined", currentFrameGroup));
#endif
		renameFile(formatFileName("combining", currentFrameGroup+1), formatFileName("combined", currentFrameGroup+1));
#ifndef KEEP_PAST_FILES
		deleteFile(formatFileName("expanded", currentFrameGroup));
#endif

		timeb time4;
		ftime(&time4);
		{
			time_t ms         = (time4.time - time3.time)*1000 + (time4.millitm - time3.millitm);
			time_t ms_total   = (time4.time - time1.time)*1000 + (time4.millitm - time1.millitm);
#ifdef PRINT_RUNNING_TOTAL_TIME
			time_t ms_running = (time4.time - time0.time)*1000 + (time4.millitm - time0.millitm);
			printf("%4d.%03d s (%4d.%03d s, %6d.%03d s)", ms/1000, ms%1000, ms_total/1000, ms_total%1000, ms_running/1000, ms_running%1000);
#else
			printf("%4d.%03d s (%4d.%03d s)", ms/1000, ms%1000, ms_total/1000, ms_total%1000);
#endif
		}
		time1 = time4;

		putchar('\n');
	}
	
	printf("Exit not found.\n");
	return EXIT_NOTFOUND;
}

#if 0
// ******************************************* Grouped-search *******************************************

int groupedSearch(FRAME_GROUP groupSize, bool filterFirst)
{
	firstFrameGroup = 0;

	if (fileExists(formatFileName("solution")))
		error(format("Partial trace solution file (%s) present - if you want to resume exit tracing, run \"search\" instead, otherwise delete the file", formatFileName("solution")));

	for (FRAME_GROUP g=MAX_FRAME_GROUPS; g>=0; g--)
		if (fileExists(formatFileName("closed", g)))
		{
			printf("Resuming from frame" GROUP_STR " " GROUP_FORMAT "\n", g+1);
			firstFrameGroup = g+1;
			break;
	    }

	if (firstFrameGroup==0)
		error("No closed node files found");

	for (FRAME_GROUP g=firstFrameGroup; g<MAX_FRAME_GROUPS; g++)
		if (fileExists(formatFileName("open", g)))
		{
			printTime(); printf("Reopening queue for frame" GROUP_STR " " GROUP_FORMAT "\n", g);
			queue[g] = new BufferedOutputStream(formatFileName("open", g), true, 0);
		}

	FRAME_GROUP baseFrameGroup = firstFrameGroup;

	while (true)
	{
		bool haveOpen = false;
		for (FRAME_GROUP g=baseFrameGroup; g<MAX_FRAME_GROUPS; g++)
			if (fileExists(formatFileName("open", g)))
			{
				haveOpen = true;
				break;
			}
		if (!haveOpen)
			break;

		printTime(); printf("=== Frame"GROUP_STR"s "GROUP_FORMAT" .. "GROUP_FORMAT" ===\n", baseFrameGroup, baseFrameGroup+groupSize-1);

		if (filterFirst)
		{
			for (FRAME_GROUP g=baseFrameGroup; g<baseFrameGroup+groupSize; g++)
				if (queue[g])
				{
					delete queue[g];
					queue[g] = NULL;
				}

			printTime(); printf("Sorting...\n");
			doSortOpen(baseFrameGroup, baseFrameGroup+groupSize);
			if (checkStop()) return EXIT_STOP;

			printTime(); printf("Filtering...\n");
			doFilterOpen(baseFrameGroup, baseFrameGroup+groupSize);
			if (checkStop()) return EXIT_STOP;
		}

		for (FRAME_GROUP g=baseFrameGroup; g<baseFrameGroup+groupSize; g++)
			if (!queue[g] && fileExists(formatFileName("open", g)))
			{
				//printTime(); printf("Reopening queue for frame" GROUP_STR " " GROUP_FORMAT "\n", g);
				queue[g] = new BufferedOutputStream(formatFileName("open", g), true, 0);
			}

		prepareOpen();
		
		for (currentFrameGroup=baseFrameGroup; currentFrameGroup<baseFrameGroup+groupSize; currentFrameGroup++)
		{
			if (!queue[currentFrameGroup])
				continue;
			delete queue[currentFrameGroup];
			queue[currentFrameGroup] = NULL;

			printTime(); printf("Frame" GROUP_STR " " GROUP_FORMAT ": ", currentFrameGroup); fflush(stdout);
			ramUsed = 0;
			sortAndMerge(currentFrameGroup);

			printf("Clearing... "); fflush(stdout);
			memset(ram, 0, ramUsed); // clear cache

			printf("Expanding... "); fflush(stdout);
#ifdef MULTITHREADING
			startWorkers<&processState>();
#endif
			BufferedInputStream input(formatFileName("merged", currentFrameGroup));
			const CompressedState* cs;
			while (cs = input.read())
				processFilteredState(cs);
#ifdef MULTITHREADING
			flushProcessingQueue();
#endif
			printf("Done.\n");

			if (exitFound)
			{
				assert(currentFrameGroup == exitFrame / FRAMES_PER_GROUP);
				printf("Exit has been found, stopping.\n");
				break;
			}
		}

		printTime(); printf("Flushing...\n");
		flushOpen();

		for (FRAME_GROUP g=baseFrameGroup; g<baseFrameGroup+groupSize; g++)
			if (queue[g])
			{
				delete queue[g];
				queue[g] = NULL;
			}

		printTime(); printf("Filtering...\n");
		BufferedRewriteStream* merged = new BufferedRewriteStream[groupSize];
		for (int g=0; g<groupSize; g++)
			if (fileExists(formatFileName("merged", baseFrameGroup+g)))
			{
				//merged[g].setReadBufferSize(MERGING_BUFFER_SIZE);
				//merged[g].setWriteBufferSize(MERGING_BUFFER_SIZE);
				merged[g].open(formatFileName("merged", baseFrameGroup+g));
			}
#ifdef USE_ALL
		filterAgainstClosed<true >(merged, groupSize);
#else
		filterAgainstClosed<false>(merged, groupSize);
#endif
		delete[] merged;

		for (FRAME_GROUP g=baseFrameGroup; g<baseFrameGroup+groupSize; g++)
			if (fileExists(formatFileName("merged", g)))
			{
				deleteFile(formatFileName("open", g));
				renameFile(formatFileName("merged", g), formatFileName("closed", g));
			}

		if (exitFound)
		{
			putchar('\n');
			printTime();
			printf("Exit found (at frame %u), tracing path...\n", exitFrame);
			traceExit(&exitState, exitFrame);
			return EXIT_OK;
		}

		baseFrameGroup += groupSize;

		if (checkStop())
			return EXIT_STOP;
	}
	printf("Exit not found.\n");
	return EXIT_NOTFOUND;
}
#endif

#if 0
// ********************************************* Pack-open **********************************************

int packOpen()
{
	for (FRAME_GROUP g=firstFrameGroup; g<maxFrameGroups; g++)
    	if (fileExists(formatFileName("open", g)))
    	{
			printTime(); printf("Frame" GROUP_STR " " GROUP_FORMAT ": ", g);

			{
				InputStream input(formatFileName("open", g));
				OutputStream output(formatFileName("openpacked", g));
				uint64_t amount = input.size();
				if (amount > BUFFER_SIZE)
					amount = BUFFER_SIZE;
				size_t records;
				uint64_t read=0, written=0;
				while (records = input.read(buffer, (size_t)amount))
				{
					read += records;
					std::sort(buffer, buffer + records);
					records = deduplicate(buffer, records);
					written += records;
					output.write(buffer, records);
				}
				output.flush();

				if (read == written)
					printf("No improvement.\n");
				else
					printf("%llu -> %llu.\n", read, written);
			}
			deleteFile(formatFileName("open", g));
			renameFile(formatFileName("openpacked", g), formatFileName("open", g));
    	}
	return EXIT_OK;
}
#endif

// ************************************************ Dump ************************************************

int dump(FRAME_GROUP g)
{
	printf("Dumping frame" GROUP_STR " " GROUP_FORMAT ":\n", g);
	const char* fn = formatFileName("closed", g);
	/*if (!fileExists(fn))
		fn = formatFileName("open", g);*/
	if (!fileExists(fn))
		error(format("Can't find neither open nor closed node file for frame" GROUP_STR " " GROUP_FORMAT, g));
	
	BufferedInputStream<Node> in(fn);
	const Node* cs;
	while (cs = in.read())
	{
#ifdef GROUP_FRAMES
		printf("Frame %u:\n", GET_FRAME(g, *cs));
#endif
		State s;
		s.decompress(&cs->getState());
		puts(s.toString());
	}
	return EXIT_OK;
}

// *********************************************** Sample ***********************************************

int sample(FRAME_GROUP g, unsigned count)
{
	printf("Sampling frame" GROUP_STR " " GROUP_FORMAT " %u times:\n", g, count);
	const char* fn = formatFileName("closed", g);
	if (!fileExists(fn))
		fn = formatFileName("open", g);
	if (!fileExists(fn))
		error(format("Can't find neither open nor closed node file for frame" GROUP_STR " " GROUP_FORMAT, g));
	
	InputStream<Node> in(fn);
	srand((unsigned)time(NULL));
	for (unsigned i=0; i<count; i++)
	{
		in.seek(((uint64_t)rand() + ((uint64_t)rand()<<32)) % in.size());
		Node cs;
		in.read(&cs, 1);
#ifdef GROUP_FRAMES
		printf("Frame %u:\n", GET_FRAME(g, cs));
#endif
		State s;
		s.decompress(&cs.getState());
		puts(s.toString());
	}
	return EXIT_OK;
}

// ********************************************** Compare ***********************************************

int compare(const char* fn1, const char* fn2)
{
	BufferedInputStream<Node> i1(fn1), i2(fn2);
	printf("%s: %llu states\n%s: %llu states\n", fn1, i1.size(), fn2, i2.size());
	const Node *cs1, *cs2;
	cs1 = i1.read();
	cs2 = i2.read();
	uint64_t dups = 0;
	uint64_t switches = 0;
	int last=0, cur;
	while (cs1 && cs2)
	{
		if (*cs1 < *cs2)
			cs1 = i1.read(),
			cur = -1;
		else
		if (*cs1 > *cs2)
			cs2 = i2.read(),
			cur = 1;
		else
		{
			dups++;
			cs1 = i1.read();
			cs2 = i2.read();
			cur = 0;
		}
		if (cur != last)
			switches++;
		last = cur;
	}
	printf("%llu duplicate states\n", dups);
	printf("%llu interweaves\n", switches);
	return EXIT_OK;
}

// ********************************************** Convert ***********************************************

#ifdef GROUP_FRAMES

// This works only if the size of CompressedState is the same as the old version (without the subframe field).

// HACK: the following code uses pointer arithmetics with BufferedInputStream objects to quickly determine the subframe from which a CompressedState came from.

void convertMerge(BufferedInputStream<Node> inputs[], int inputCount, BufferedOutputStream<Node>* output)
{
	InputHeap<BufferedInputStream<Node>, Node> heap(inputs, inputCount);
	//uint64_t* positions = new uint64_t[inputCount];
	//for (int i=0; i<inputCount; i++)
	//	positions[i] = 0;

	Node cs = *heap.read();
	debug_assert(heap.getHeadInput() >= inputs && heap.getHeadInput() < inputs+FRAMES_PER_GROUP);
	cs.subframe = (uint8_t)(heap.getHeadInput() - inputs);
	bool oooFound = false, equalFound = false;
	while (heap.next())
	{
		Node cs2 = *heap.getHead();
		debug_assert(heap.getHeadInput() >= inputs && heap.getHeadInput() < inputs+FRAMES_PER_GROUP);
		uint8_t subframe = (uint8_t)(heap.getHeadInput() - inputs);
		//positions[subframe]++;
		cs2.subframe = subframe;
		if (cs2 < cs) // work around flush bug in older versions
		{
			if (!oooFound)
			{
				printf("Unordered states found in subframe %d, skipping\n", subframe);
				oooFound = true;
			}
			continue;
		}
		if (cs == cs2) // CompressedState::operator== does not compare subframe
		{
			if (!equalFound)
			{
				//printf("Duplicate states in subframes %d at %lld and %d at %lld\n", cs.subframe, positions[cs.subframe], subframe, positions[subframe]);
				printf("Duplicate states found in subframes %d and %d\n", cs.subframe, subframe);
				equalFound = true;
			}
			if (cs.subframe > subframe) // in case of duplicate frames, pick the one from the smallest frame
				cs.subframe = subframe;
		}
		else
		{
			output->write(&cs, true);
			cs = cs2;
		}
	}
	output->write(&cs, true);
}

int convert()
{
	for (FRAME_GROUP g=firstFrameGroup; g<maxFrameGroups; g++)
	{
		bool haveClosed=false, haveOpen=false;
		BufferedInputStream<Node> inputs[FRAMES_PER_GROUP];
		for (FRAME f=g*FRAMES_PER_GROUP; f<(g+1)*FRAMES_PER_GROUP; f++)
			if (fileExists(formatProblemFileName("closed", format("%u", f), "bin")))
				inputs[f%FRAMES_PER_GROUP].open(formatProblemFileName("closed", format("%u", f), "bin")),
				haveClosed = true;
			else
			if (fileExists(formatProblemFileName("open", format("%u", f), "bin")))
				inputs[f%FRAMES_PER_GROUP].open(formatProblemFileName("open", format("%u", f), "bin")),
				haveOpen = true;
		if (haveOpen || haveClosed)
		{
			printf(GROUP_FORMAT "...\n", g);
			{
				BufferedOutputStream<Node> output(formatFileName("converting", g));
				convertMerge(inputs, FRAMES_PER_GROUP, &output);
			}
			renameFile(formatFileName("converting", g), formatFileName(haveOpen ? "open" : "closed", g));
		}
		free(inputs);
	}
	return EXIT_OK;
}

// *********************************************** Unpack ***********************************************

// Unpack a closed node frame group file to individual frames.

int unpack()
{
	for (FRAME_GROUP g=firstFrameGroup; g<maxFrameGroups; g++)
		if (fileExists(formatFileName("closed", g)))
		{
			printTime(); printf("Frame" GROUP_STR " " GROUP_FORMAT "\n", g); fflush(stdout);
			BufferedInputStream<Node> input(formatFileName("closed", g));
			BufferedOutputStream<Node> outputs[FRAMES_PER_GROUP];
			for (int i=0; i<FRAMES_PER_GROUP; i++)
				outputs[i].open(formatProblemFileName("closed", format("%u", g*FRAMES_PER_GROUP+i), "bin"));
			const Node* cs;
			while (cs = input.read())
			{
				Node cs2 = *cs;
				cs2.subframe = 0;
				outputs[cs->subframe].write(&cs2, true);
			}
		}
	return EXIT_OK;
}

// *********************************************** Count ************************************************

int count()
{
	for (FRAME_GROUP g=firstFrameGroup; g<maxFrameGroups; g++)
		if (fileExists(formatFileName("closed", g)))
		{
			printTime(); printf("Frame" GROUP_STR " " GROUP_FORMAT ":\n", g);
			BufferedInputStream<Node> input(formatFileName("closed", g));
			const Node* cs;
			uint64_t counts[FRAMES_PER_GROUP] = {0};
			while (cs = input.read())
				counts[cs->subframe]++;
			for (int i=0; i<FRAMES_PER_GROUP; i++)
				if (counts[i])
					printf("Frame %u: %llu\n", g*FRAMES_PER_GROUP+i, counts[i]);
			fflush(stdout);
		}
	return EXIT_OK;
}

#endif // GROUP_FRAMES

// *********************************************** Verify ***********************************************

int verify(const char* filename)
{
	BufferedInputStream<Node> input(filename);
	Node cs = *input.read();
	bool equalFound=false, oooFound=false;
	uint64_t pos = 0;
	while (1)
	{
		const Node* cs2 = input.read();
		pos++;
		if (cs2==NULL)
			return EXIT_OK;
		if (cs == *cs2)
			if (!equalFound)
			{
				printf("Equal states found: %lld\n", pos);
				equalFound = true;
			}
		if (cs > *cs2)
			if (!oooFound)
			{
				printf("Unordered states found: %lld\n", pos);
				oooFound = true;
			}
#ifdef GROUP_FRAMES
		if (cs2->subframe >= FRAMES_PER_GROUP)
			error("Invalid subframe (corrupted data?)");
#endif
		cs = *cs2;
		if (equalFound && oooFound)
			return EXIT_OK;
	}
}

#if 0
// ********************************************* Sort-open **********************************************

int doSortOpen(FRAME_GROUP firstFrameGroup, FRAME_GROUP maxFrameGroups)
{
	// override global variables
	for (FRAME_GROUP currentFrameGroup=maxFrameGroups-1; currentFrameGroup>=firstFrameGroup; currentFrameGroup--)
	{
		if (!fileExists(formatFileName("open", currentFrameGroup)))
			continue;
		if (fileExists(formatFileName("merged", currentFrameGroup)))
			error("Merged file present");
		uint64_t initialSize, finalSize;
		{
			InputStream s(formatFileName("open", currentFrameGroup));
			initialSize = s.size();
		}
		if (initialSize==0)
			continue;

		printTime(); printf("Frame" GROUP_STR " " GROUP_FORMAT "/" GROUP_FORMAT ": ", currentFrameGroup, maxFrameGroups); fflush(stdout);

		sortAndMerge(currentFrameGroup);
		
		deleteFile(formatFileName("open", currentFrameGroup));
		renameFile(formatFileName("merged", currentFrameGroup), formatFileName("open", currentFrameGroup));

		{
			InputStream s(formatFileName("open", currentFrameGroup));
			finalSize = s.size();
		}

		printf("Done: %lld -> %lld.\n", initialSize, finalSize);

		if (checkStop())
			return EXIT_STOP;
	}
	return EXIT_OK;
}

int sortOpen()
{
	return doSortOpen(firstFrameGroup, maxFrameGroups);
}

// ****************************************** Seq-filter-open *******************************************

// Filters open node lists without expanding nodes.

int seqFilterOpen()
{
	// redeclare currentFrameGroup
	for (FRAME_GROUP currentFrameGroup=firstFrameGroup; currentFrameGroup<maxFrameGroups; currentFrameGroup++)
	{
		if (!fileExists(formatFileName("open", currentFrameGroup)))
			continue;

		printTime(); printf("Frame" GROUP_STR " " GROUP_FORMAT "/" GROUP_FORMAT ": ", currentFrameGroup, maxFrameGroups); fflush(stdout);

		uint64_t initialSize, finalSize;

		if (fileExists(formatFileName("merged", currentFrameGroup)))
		{
			printf("(reopening merged)    ");
		}
		else
		{
			{
				InputStream s(formatFileName("open", currentFrameGroup));
				initialSize = s.size();
			}

			// Step 1 & 2: sort and merge open nodes
			sortAndMerge(currentFrameGroup);
		}

		// Step 3: dedup against previous frames
		printf("Filtering... "); fflush(stdout);
		{
			class NullStateHandler
			{
			public:
				INLINE static void handle(const CompressedState* state) {}
			};

			BufferedInputStream* source = new BufferedInputStream(formatFileName("merged", currentFrameGroup));
			BufferedInputStream* inputs = new BufferedInputStream[MAX_FRAME_GROUPS+1];
			int inputCount = 0;
			for (FRAME_GROUP g=currentFrameGroup-1; g>=0; g--)
			{
#ifdef USE_ALL
				if (fileExists(formatFileName("all", g)))
				{
					Node *allReadBuffer = (Node*)ram;

					inputs[inputCount  ].setReadBuffer(allReadBuffer, ALL_FILE_BUFFER_SIZE);
					inputs[inputCount++].open(formatFileName("all", g));
					break;
				}
#endif
				const char* fn = formatFileName("open", g);
				if (!fileExists(fn))
					fn = formatFileName("closed", g);
				if (fileExists(fn))
				{
					inputs[inputCount].open(fn);
					if (inputs[inputCount].size())
						inputCount++;
					else
						inputs[inputCount].close();
				}
			}
			
			{
				InputHeap<BufferedInputStream> input(inputs, inputCount);
				BufferedOutputStream output(formatFileName("filtering", currentFrameGroup));
				filterStream(source, &input, &output, &nullOutput);
			}
			delete source;
			delete[] inputs;
			deleteFile(formatFileName("merged", currentFrameGroup));
		}

		deleteFile(formatFileName("open", currentFrameGroup));
		renameFile(formatFileName("filtering", currentFrameGroup), formatFileName("open", currentFrameGroup));

		{
			InputStream s(formatFileName("open", currentFrameGroup));
			finalSize = s.size();
		}

		printf("Done: %lld -> %lld.\n", initialSize, finalSize);

		if (checkStop())
			return EXIT_STOP;
	}
	return EXIT_OK;
}

// ******************************************** Filter-open *********************************************

void doFilterOpen(FRAME_GROUP firstFrameGroup, FRAME_GROUP maxFrameGroups)
{
	BufferedRewriteStream* open = new BufferedRewriteStream[MAX_FRAME_GROUPS];
	for (FRAME_GROUP g=firstFrameGroup; g<maxFrameGroups; g++)
		if (fileExists(formatFileName("open", g)))
		{
			enforce(!fileExists(formatFileName("closed", g)), format("Open and closed node files present for the same frame" GROUP_STR " " GROUP_FORMAT, g));
			open[g].open(formatFileName("open", g));
	    }

	filterAgainstClosed<false>(open, MAX_FRAME_GROUPS);

	delete[] open;
}

int filterOpen()
{
	doFilterOpen(0, MAX_FRAME_GROUPS);
	return EXIT_OK;
}

// ****************************************** Regenerate-open *******************************************

int regenerateOpen()
{
	for (FRAME_GROUP g=0; g<MAX_FRAME_GROUPS; g++)
		if (fileExists(formatFileName("closed", g)) || fileExists(formatFileName("open", g)))
			noQueue[g] = true;
	
	while (maxFrameGroups>0 && !fileExists(formatFileName("closed", maxFrameGroups-1)))
		maxFrameGroups--;

	uint64_t oldSize = 0;
	for (currentFrameGroup=firstFrameGroup; currentFrameGroup<maxFrameGroups; currentFrameGroup++)
		if (fileExists(formatFileName("closed", currentFrameGroup)))
		{
			printTime(); printf("Frame" GROUP_STR " " GROUP_FORMAT "/" GROUP_FORMAT ": ", currentFrameGroup, maxFrameGroups); fflush(stdout);
			
#ifdef MULTITHREADING
			startWorkers<&processState>();
#endif
			prepareOpen();

			BufferedInputStream closed(formatFileName("closed", currentFrameGroup));
			const CompressedState* cs;
			while (cs = closed.read())
				processFilteredState(cs);
			
#ifdef MULTITHREADING
			flushProcessingQueue();
#endif
			
			printf("Flushing... "); fflush(stdout);
			flushOpen();
			
			uint64_t size = 0;
			for (FRAME_GROUP g=0; g<MAX_FRAME_GROUPS; g++)
				if (queue[g])
					size += queue[g]->size();

			printf("Done (%lld).\n", size - oldSize);
			oldSize = size;

			if (checkStop())
				return EXIT_STOP;
		}
	
	return EXIT_OK;
}

// ********************************************* Create-all *********************************************

int createAll()
{
	FRAME_GROUP maxClosed = -1;
	BufferedInputStream<Node>* closed = new BufferedInputStream<Node>[MAX_FRAME_GROUPS];
	for (FRAME_GROUP g=0; g<MAX_FRAME_GROUPS; g++)
		if (fileExists(formatFileName("closed", g)))
		{
			closed[g].open(formatFileName("closed", g));
			if (maxClosed == -1)
				maxClosed = g;
		}
	enforce(maxClosed >= 0, "No closed node files found");

	{
		BufferedOutputStream<BareNode> all(formatFileName("allnew", maxClosed));
		mergeStreams(closed, MAX_FRAME_GROUPS, &all);
	}
	delete[] closed;

	renameFile(formatFileName("allnew", maxClosed), formatFileName("all", maxClosed));
	return EXIT_OK;
}
#endif

// ********************************************* Find-exit **********************************************

int findExit()
{
	if (fileExists(formatFileName("solution")))
		error(format("Partial trace solution file (%s) present - if you want to resume exit tracing, run \"search\" instead, otherwise delete the file", formatFileName("solution")));
	
	// redeclare currentFrameGroup
	for (FRAME_GROUP currentFrameGroup=firstFrameGroup; currentFrameGroup<maxFrameGroups; currentFrameGroup++)
	{
		const char* fn = formatFileName("closed", currentFrameGroup);
		if (!fileExists(fn))
			fn = formatFileName("open", currentFrameGroup);
		if (fileExists(fn))
		{
			printTime(); printf("Frame" GROUP_STR " " GROUP_FORMAT "/" GROUP_FORMAT ": ", currentFrameGroup, maxFrameGroups); fflush(stdout);
			BufferedInputStream<Node> input(fn);
			const Node* cs;
			while (cs = input.read())
			{
				State s;
				s.decompress(&cs->getState());
				if (s.isFinish())
				{
					FRAME exitFrame = GET_FRAME(currentFrameGroup, *cs);
					printTime();
					printf("Exit found (at frame %u), tracing path...\n", exitFrame);
					traceExit(&s, exitFrame);
					return EXIT_OK;
				}
			}
			printf("Done.\n");
		}
	}
	printf("Exit not found.\n");
	return EXIT_NOTFOUND;
}


// *************************************** Write-partial-solution ***************************************

int writePartialSolution()
{
	if (!fileExists(formatFileName("solution")))
		error(format("Partial trace solution file (%s) not found.", formatFileName("solution")));
	
	int stepNr;
	Step steps[MAX_STEPS];

	FILE* f = fopen(formatFileName("solution"), "rb");
	fread(&exitSearchFrameGroup, sizeof(exitSearchFrameGroup), 1, f);
	fread(&exitSearchStateFrame, sizeof(exitSearchStateFrame), 1, f);
	fread(&exitSearchState     , sizeof(exitSearchState)     , 1, f);
	fread(&stepNr              , sizeof(stepNr)              , 1, f);
	fread(steps, sizeof(Step), stepNr, f);
	fclose(f);

	writeSolution(&exitSearchState, steps, stepNr);

	return EXIT_OK;
}

// ***************************************** Win32 idle watcher *****************************************

// use background CPU and I/O priority when PC is not idle

#if defined(_WIN32)
#pragma comment(lib,"user32")
DWORD WINAPI idleWatcher(__in LPVOID lpParameter)
{
    LASTINPUTINFO lii;
    lii.cbSize = sizeof(LASTINPUTINFO);
    while (true)
    {
		do
		{
			Sleep(1000);
			GetLastInputInfo(&lii);
		}
		while (GetTickCount() - lii.dwTime > 60*1000);
		
		do
		{
			FILE* f = fopen("idle.txt", "rt");
			if (f)
			{
				int work, idle;
				fscanf(f, "%d %d", &work, &idle);
				fclose(f);
				
				Sleep(work);
				SetPriorityClass(GetCurrentProcess(), PROCESS_MODE_BACKGROUND_BEGIN);
				Sleep(idle);
				SetPriorityClass(GetCurrentProcess(), PROCESS_MODE_BACKGROUND_END);
			}
			else
				Sleep(1000);
			GetLastInputInfo(&lii);
		}
		while (GetTickCount() - lii.dwTime < 60*1000);
	}
}
#endif

// ******************************************************************************************************

timeb startTime;

void printExecutionTime()
{
	timeb endTime;
	ftime(&endTime);
	time_t ms = (endTime.time - startTime.time)*1000
	       + (endTime.millitm - startTime.millitm);
	printf("Time: %d.%03d seconds.\n", ms/1000, ms%1000);
}

// ***********************************************************************************

// Test the CompressedState comparison operators.
void testCompressedState()
{
	enforce(sizeof(Node)%4 == 0);
#ifdef GROUP_FRAMES
	enforce(COMPRESSED_BITS <= (sizeof(Node)-1)*8);
#else
	enforce(COMPRESSED_BITS <= sizeof(Node)*8);
	enforce((COMPRESSED_BITS+31)/8 >= sizeof(Node));
#endif

	Node c1, c2;
	uint8_t *p1 = (uint8_t*)&c1, *p2 = (uint8_t*)&c2;
	memset(p1, 0, sizeof(Node));
	memset(p2, 0, sizeof(Node));
	
#ifdef GROUP_FRAMES
	int subframe;

	switch (COMPRESSED_BYTES % 4)
	{
		case 0: subframe = sizeof(Node)-4; break;
		case 1: // align to word - fall through
		case 2: subframe = sizeof(Node)-2; break;
		case 3: subframe = sizeof(Node)-1; break;
	}

	p1[subframe] = 0xFF;
	enforce(c1 == c2, "Different subframe causes inequality");
	c2.subframe = 0xFF;
	enforce(c1.subframe == 0xFF && p2[subframe] == 0xFF && memcmp(p1, p2, sizeof(Node))==0, format("Misaligned subframe!\n%s\n%s", hexDump(p1, sizeof(Node)), hexDump(p2, sizeof(Node))));
#endif
	
	for (int i=0; i<COMPRESSED_BITS; i++)
	{
		p1[i/8] |= (1<<(i%8));
		enforce(c1 != c2, format("Inequality expected!\n%s\n%s", hexDump(p1, sizeof(Node)), hexDump(p2, sizeof(Node))));
		p2[i/8] |= (1<<(i%8));
		enforce(c1 == c2, format(  "Equality expected!\n%s\n%s", hexDump(p1, sizeof(Node)), hexDump(p2, sizeof(Node))));
	}
}

// ***********************************************************************************

int parseInt(const char* str)
{
	int result;
	if (!sscanf(str, "%d", &result))
		error(format("'%s' is not a valid integer", str));
	return result;
}

void parseFrameRange(int argc, const char* argv[])
{
	if (argc==0)
		firstFrameGroup = 0, 
		maxFrameGroups  = MAX_FRAME_GROUPS+1;
	else
	if (argc==1)
		firstFrameGroup = parseInt(argv[0]),
		maxFrameGroups  = firstFrameGroup+1;
	else
	if (argc==2)
		firstFrameGroup = parseInt(argv[0]),
		maxFrameGroups  = parseInt(argv[1]);
	else
		error("Too many arguments");
}

const char* usage = "\
Generic C++ DDD solver\n\
(c) 2009-2010 Vladimir \"CyberShadow\" Panteleev\n\
Usage:\n\
	search <mode> <parameters>\n\
where <mode> is one of:\n\
	search [max-frame"GROUP_STR"]\n\
		Sorts, filters and expands open nodes. 	If no open node files\n\
		are present, starts a new search from the initial state.\n\
	grouped-search <size>\n\
		Performs the same operation as \"search\", but processes <size>\n\
		open frame"GROUP_STR"s at once. The nodes for each frame"GROUP_STR"\n\
		are expanded without being filtered, and all frame"GROUP_STR"s are\n\
		sorted, filtered and \"closed\" at the end. This mode is useful\n\
		when the open node files become much smaller than the total\n\
		size of the closed node files.\n\
	grouped-search-no-filter <size>\n\
		Same as above, but does not sort and filter the node files\n\
		before expanding them. Use with care, as it may lead to an\n\
		explosion in the number of open nodes.\n\
	dump <frame"GROUP_STR">\n\
		Dumps all states from the specified frame"GROUP_STR", which\n\
		can be either open or closed.\n\
	sample <frame"GROUP_STR">\n\
		Displays a random state from the specified frame"GROUP_STR", which\n\
		can be either open or closed.\n\
	compare <filename-1> <filename-2>\n\
		Counts the number of duplicate nodes in two files. The nodes in\n\
		the files must be sorted and deduplicated.\n"
#ifdef GROUP_FRAMES
"	convert [frame"GROUP_STR"-range]\n\
		Converts individual frame files to frame"GROUP_STR" files for the\n\
		specified frame"GROUP_STR" range.\n\
	unpack [frame"GROUP_STR"-range]\n\
		Converts frame"GROUP_STR" files back to individual frame files\n\
		(reverses the \"convert\" operation).\n\
	count [frame"GROUP_STR"-range]\n\
		Counts the number of nodes in individual frames for the\n\
		specified frame"GROUP_STR" files.\n"
#endif
"	verify <filename>\n\
		Verifies that the nodes in a file are correctly sorted and\n\
		deduplicated, as well as a few additional integrity checks.\n\
	pack-open [frame"GROUP_STR"-range]\n\
		Removes duplicates within each chunk for open node files in the\n\
		specified range. Reads and writes open nodes only once.\n\
	sort-open [frame"GROUP_STR"-range]\n\
		Sorts and removes duplicates for open node files in the\n\
		specified range. File are processed in reverse order.\n\
	filter-open\n\
		Filters all open node files. Requires that all open node files\n\
		be sorted and deduplicated (run sort-open before filter-open).\n\
		Filtering is performed in-place. An aborted run shouldn't cause\n\
		data loss, but will require re-sorting.\n\
	seq-filter-open [frame"GROUP_STR"-range]\n\
		Sorts, deduplicates and filters open node files in the\n\
		specified range, one by one. Specify the range cautiously,\n\
		as this function requires that previous open node files be\n\
		sorted and deduplicated (and filtered for best performance).\n\
	regenerate-open [frame"GROUP_STR"-range]\n\
		Re-expands closed nodes in the specified frame"GROUP_STR" range.\n\
		New (open) nodes are saved only for frame"GROUP_STR"s that don't\n\
		already have an open or closed node file. Use this when an open\n\
		node file has been accidentally deleted or corrupted. To\n\
		regenerate all open nodes, delete all open node files before\n\
		running regenerate-open (this is still faster than restarting\n\
		the search).\n\
	create-all\n\
		Creates the \"all\" file from closed node files. Use when\n\
		turning on USE_ALL, or when the \"all\" file was corrupted.\n\
	find-exit [frame"GROUP_STR"-range]\n\
		Searches for exit frames in the specified frame"GROUP_STR" range\n\
		(both closed an open node files). When a state is found which\n\
		satisfies the isFinish condition, it is traced back and the\n\
		solution is written, as during normal search.\n\
	write-partial-solution\n\
		Saves the partial solution, using the partial exit trace\n\
		solution file. Allows exit tracing inspection. Warning: uses\n\
		the same code as when writing the full solution, and may\n\
		overwrite an existing solution.\n\
A [frame"GROUP_STR"-range] is a space-delimited list of zero, one or two frame"GROUP_STR"\n\
numbers. If zero numbers are specified, the range is assumed to be all\n\
frame"GROUP_STR"s. If one number is specified, the range is set to only that\n\
frame"GROUP_STR" number. If two numbers are specified, the range is set to start\n\
from the first frame"GROUP_STR" number inclusively, and end at the second\n\
frame"GROUP_STR" number NON-inclusively.\n\
";

int run(int argc, const char* argv[])
{
	enforce(sizeof(intptr_t)==sizeof(size_t), "Bad intptr_t!");
	enforce(sizeof(int)==4, "Bad int!");
	enforce(sizeof(long long)==8, "Bad long long!");

	initProblem();

#ifdef DEBUG
	printf("Debug version\n");
#else
	printf("Optimized version\n");
#endif

#ifdef MULTITHREADING
#if defined(THREAD_BOOST)
	printf("Using %u Boost threads ", THREADS);
#elif defined(THREAD_WINAPI)
	printf("Using %u WinAPI threads ", THREADS);
#else
#error Thread plugin not set
#endif

#if defined(SYNC_BOOST)
	printf("with Boost sync\n");
#elif defined(SYNC_WINAPI)
	printf("with WinAPI sync\n");
#elif defined(SYNC_WINAPI_SPIN)
	printf("with WinAPI spinlock sync\n");
#elif defined(SYNC_INTEL_SPIN)
	printf("with Intel spinlock sync\n");
#else
#error Sync plugin not set
#endif
#endif // MULTITHREADING
	
	printf("Compressed state is %u bits (%u bytes data, %u bytes with subframe, %u bytes with full frame)\n", COMPRESSED_BITS, COMPRESSED_BYTES, sizeof(Node), sizeof(OpenNode));
#ifdef SLOW_COMPARE
	printf("Using memcmp for CompressedState comparison\n");
#endif
	testCompressedState();

	enforce(ram, "RAM allocation failed");
	printf("Using %lld bytes of RAM for %lld buffer nodes\n", (long long)RAM_SIZE, (long long)OPENNODE_BUFFER_SIZE);

#if defined(DISK_WINFILES)
	printf("Using Windows API files");
#ifdef USE_UNBUFFERED_DISK_IO
	printf(" with unbuffered disk I/O\n");
#else
	printf(" with Windows disk I/O buffering\n");
#endif
#elif defined(DISK_POSIX)
	printf("Using POSIX files\n");
#else
#error Disk plugin not set
#endif

	if (fileExists(formatProblemFileName("stop", NULL, "txt")))
	{
		printf("Stop file present.\n");
		return EXIT_STOP;
	}

#if defined(_WIN32)
	CreateThread(NULL, 0, &idleWatcher, NULL, 0, NULL);
#endif

	printf("Command-line:");
	for (int i=0; i<argc; i++)
		printf(" %s", argv[i]);
	printf("\n");

	maxFrameGroups = MAX_FRAME_GROUPS+1;

	ftime(&startTime);
	atexit(&printExecutionTime);

	if (argc>1 && strcmp(argv[1], "search")==0)
	{
		if (argc>2)
			maxFrameGroups = parseInt(argv[2]);
		return search();
	}
#if 0
	if (argc>1 && strcmp(argv[1], "grouped-search")==0)
	{
		enforce(argc==3, "Specify how many frame"GROUP_STR"s to process at once");
		return groupedSearch(parseInt(argv[2]), true);
	}
	else
	if (argc>1 && strcmp(argv[1], "grouped-search-no-filter")==0)
	{
		enforce(argc==3, "Specify how many frame"GROUP_STR"s to process at once");
		return groupedSearch(parseInt(argv[2]), false);
	}
	else
#endif
	if (argc>1 && strcmp(argv[1], "dump")==0)
	{
		enforce(argc==3, "Specify a frame"GROUP_STR" to dump");
		return dump(parseInt(argv[2]));
	}
	else
	if (argc>1 && strcmp(argv[1], "sample")==0)
	{
		enforce(argc==4, "Specify a frame"GROUP_STR" to sample and a number of samples");
		return sample(parseInt(argv[2]), parseInt(argv[3]));
	}
	else
	if (argc>1 && strcmp(argv[1], "compare")==0)
	{
		enforce(argc==4, "Specify two files to compare");
		return compare(argv[2], argv[3]);
	}
	else
#ifdef GROUP_FRAMES
	if (argc>1 && strcmp(argv[1], "convert")==0)
	{
		parseFrameRange(argc-2, argv+2);
		return convert();
	}
	else
	if (argc>1 && strcmp(argv[1], "unpack")==0)
	{
		parseFrameRange(argc-2, argv+2);
		return unpack();
	}
	else
	if (argc>1 && strcmp(argv[1], "count")==0)
	{
		parseFrameRange(argc-2, argv+2);
		return count();
	}
	else
#endif
	if (argc>1 && strcmp(argv[1], "verify")==0)
	{
		enforce(argc==3, "Specify a file to verify");
		return verify(argv[2]);
	}
#if 0
	else
	if (argc>1 && strcmp(argv[1], "pack-open")==0)
	{
		parseFrameRange(argc-2, argv+2);
		return packOpen();
	}
	else
	if (argc>1 && strcmp(argv[1], "sort-open")==0)
	{
		parseFrameRange(argc-2, argv+2);
		return sortOpen();
	}
	else
	if (argc>1 && strcmp(argv[1], "filter-open")==0)
	{
		return filterOpen();
	}
	else
	if (argc>1 && strcmp(argv[1], "seq-filter-open")==0)
	{
		parseFrameRange(argc-2, argv+2);
		return seqFilterOpen();
	}
	else
	if (argc>1 && strcmp(argv[1], "regenerate-open")==0)
	{
		parseFrameRange(argc-2, argv+2);
		return regenerateOpen();
	}
	if (argc>1 && strcmp(argv[1], "create-all")==0)
	{
		return createAll();
	}
#endif
	if (argc>1 && strcmp(argv[1], "find-exit")==0)
	{
		parseFrameRange(argc-2, argv+2);
		return findExit();
	}
	else
	if (argc>1 && strcmp(argv[1], "write-partial-solution")==0)
	{
		return writePartialSolution();
	}
	else
	{
		printf("%s", usage);
		return EXIT_OK;
	}
}

// ***********************************************************************************

#ifdef NO_MAIN
// define main() in another file and #include "search.cpp"
#elif defined(PROBLEM_RELATED)
#include STRINGIZE(PROBLEM/PROBLEM_RELATED.cpp)
int main(int argc, const char* argv[])
{
	try {
		return run_related(argc, argv);
	}
	catch(const char* s) {
		printf("\n%s\n", s);
		return EXIT_ERROR;
	}
}
#else
int main(int argc, const char* argv[]) {
	try {
		return run(argc, argv);
	}
	catch(const char* s) {
		printf("\n%s\n", s);
		return EXIT_ERROR;
	}
}
#endif
