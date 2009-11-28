#undef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <time.h>
#include <sys/timeb.h>
#include "config.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef SWAP
#include <string>
#include <sstream>
#ifdef MMAP
#include <boost/iostreams/device/mapped_file.hpp>
#else
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif
#endif

// ******************************************************************************************************

#ifdef MULTITHREADING
#if defined(THREAD_BOOST)
#include "thread_boost.cpp"
#elif defined(THREAD_WIN32)
#include "thread_win32.cpp"
#elif defined(THREAD_WIN32_SPIN)
#include "thread_win32_spin.cpp"
#else
#error Thread engine not set
#endif
// TODO: look into user-mode scheduling
#endif

// ******************************************************************************************************

#include <fstream>
#include <queue>
#include "Kwirk.cpp"
#include "hsiehhash.cpp"

// ******************************************************************************************************

State initialState;

// ******************************************************************************************************

typedef uint32_t NODEI;
typedef uint32_t FRAME;

#pragma pack(1)
struct Step
{
	unsigned action:3;
	unsigned x:5;
	unsigned y:4;
	unsigned extraSteps:4; // steps above dx+dy
};

#ifdef DFS
#include "node_fw.h"
#else
#include "node_bw.h"
#endif

// ******************************************************************************************************

NODEI nodeCount = 0;

#ifndef SWAP
#include "cache_none.cpp"
#else

INLINE const Node* cachePeek(NODEI index);
#ifdef DEBUG_VERBOSE
void testNode(const Node* data, NODEI index, const char* comment);
#else
#define testNode(x,y,z)
#endif

#if defined (MMAP)
#include "swap_mmap.cpp"
#elif defined(RAM)
#include "swap_ram.cpp"
#elif defined(WINFILES)
#include "swap_file_windows.cpp"
#elif defined(POSIX)
#include "swap_file_posix.cpp"
#else
#error No swap engine
#endif

// ******************************************************************************************************

typedef uint32_t CACHEI;

#include "stats_cache.cpp"

#include "cache.cpp"

#endif // SWAP

// ******************************************************************************************************

INLINE int replayStep(State* state, FRAME* frame, Step step)
{
	Player* p = &state->players[state->activePlayer];
	int nx = step.x+1;
	int ny = step.y+1;
	int steps = abs((int)p->x - nx) + abs((int)p->y - ny) + step.extraSteps;
	p->x = nx;
	p->y = ny;
	assert(state->map[ny][nx]==0, "Bad coordinates");
	int res = state->perform((Action)step.action);
	assert(res>0, "Replay failed");
	*frame += steps * DELAY_MOVE + res;
	return steps; // not counting actual action
}

void dumpNodesToDisk()
{
	FILE* f = fopen("nodes-" BOOST_PP_STRINGIZE(LEVEL) ".bin", "wb");
	for (NODEI n=1; n<nodeCount; n++)
	{
		const Node* np = getNodeFast(n);
		fwrite(np, sizeof(Node), 1, f);
	}
	fclose(f);
}

void printTime()
{
	time_t t;
	time(&t);
	char* tstr = ctime(&t);
	tstr[strlen(tstr)-1] = 0;
	printf("[%s] ", tstr);
}

// ******************************************************************************************************

#define HASHSIZE 28
//#define HASHSIZE 26
typedef uint32_t HASH;
NODEI lookup[1<<HASHSIZE];
#ifdef MULTITHREADING
#define PARTITIONS ((MAX_NODES+1)>>8)
MUTEX lookupMutex[PARTITIONS];
#endif

FRAME maxFrames;

#ifndef DFS
#include "search_bfs.cpp"
#else
#include "search_dfs.cpp"
#endif

timeb startTime;

void printExecutionTime()
{
	timeb endTime;
	ftime(&endTime);
	int ms = (endTime.time    - startTime.time)*1000
	       + (endTime.millitm - startTime.millitm);
	printf("Time: %d.%03d seconds.\n", ms/1000, ms%1000);
}

// ***********************************************************************************

int run(int argc, const char* argv[])
{
	printf("Level %d: %dx%d, %d players\n", LEVEL, X, Y, PLAYERS);
#ifdef HAVE_VALIDATOR
	printf("Level state validator present\n");
#endif
#ifdef DEBUG
	printf("Debug version\n");
#else
	printf("Optimized version\n");
#endif
#ifdef MULTITHREADING
#if defined(THREAD_BOOST)
	printf("Using %d Boost threads\n", THREADS);
#elif defined(THREAD_WIN32)
	printf("Using %d Win32 threads\n", THREADS);
#elif defined(THREAD_WIN32_SPIN)
	printf("Using %d Win32 threads with spinlocks\n", THREADS);
#else
#error Thread engine not set
#endif
#endif
	printf("Using node lookup hashtable of %d elements (%lld bytes)\n", 1<<HASHSIZE, (long long)(1<<HASHSIZE) * sizeof(NODEI));

#ifndef DFS
	printf("Using breadth-first search\n");
	//enforce(sizeof(Node) == 10, format("sizeof Node is %d", sizeof(Node)));
#else
	printf("Using depth-first search\n");
	enforce(sizeof(Node) == 16, format("sizeof Node is %d", sizeof(Node)));
#endif
	enforce(sizeof(Action) == 1, format("sizeof Action is %d", sizeof(Action)));

#ifdef SWAP
	printf("Using node cache of %d records (%lld bytes)\n", CACHE_SIZE, (long long)CACHE_SIZE * sizeof(CacheNode));

#if defined(MMAP)
	printf("Using memory-mapped swap files of %d records (%lld bytes) each\n", ARCHIVE_CLUSTER_SIZE, (long long)ARCHIVE_CLUSTER_SIZE * sizeof(Node));
#elif defined(RAM)
	printf("Using RAM blocks for swap testing of %d records (%lld bytes) each\n", ARCHIVE_CLUSTER_SIZE, (long long)ARCHIVE_CLUSTER_SIZE * sizeof(Node));
#elif defined(WINFILES)
	printf("Using Windows API swap file\n");
#elif defined(POSIX)
	printf("Using POSIX swap file\n");
#else
#error No swap engine
#endif

#ifdef SPLAY
	printf("Using splay tree caching\n");
	enforce(sizeof(CacheNode) == sizeof(Node)+14, format("sizeof CacheNode is %d", sizeof(CacheNode)));
#else
	printf("Using hashtable caching\n");
	printf("Using cache lookup hashtable of %d elements (%lld bytes)\n", CACHE_LOOKUPSIZE, (long long)CACHE_LOOKUPSIZE * sizeof(CACHEI));
	printf("Cache lookup hashtable is trimmed to %d elements\n", cacheTrimThreshold+1);
	enforce(cacheTrimThreshold>0, "Cache lookup hashtable trim threshold too low");
	enforce(cacheTrimThreshold<=16, "Cache lookup hashtable trim threshold too high");
	enforce(sizeof(CacheNode) == sizeof(Node)+10, format("sizeof CacheNode is %d", sizeof(CacheNode)));
#endif
#endif
	
	initialState.load();

	maxFrames = MAX_FRAMES;
	if (argc>2)
		error("Too many arguments");
	if (argc==2)
		maxFrames = strtol(argv[1], NULL, 10);

#ifdef SWAP
#ifdef ARCHIVE_STATS
	atexit(&printCacheStats);
#endif
#endif
	ftime(&startTime);
	atexit(&printExecutionTime);

	int result = search();
	//dumpNodes();
	return result;
}

// ***********************************************************************************

int main(int argc, const char* argv[]) { return run(argc, argv); }
//#include "test_body.cpp"
