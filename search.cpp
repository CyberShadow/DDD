#undef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <time.h>
#include "config.h"
#ifdef MULTITHREADING
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/barrier.hpp>
#include <boost/thread/condition.hpp>
#endif
#ifdef SWAP
#include <string>
#include <sstream>
#include <boost/iostreams/device/mapped_file.hpp>
#endif
#include <fstream>
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
#ifdef MULTITHREADING
int threadsRunning = 0;
#endif

#ifndef SWAP
#include "nodes_flat.cpp"
#else
#include "nodes_swap.cpp"
#endif

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
typedef uint32_t HASH;
NODEI lookup[1<<HASHSIZE];
#ifdef MULTITHREADING
#define PARTITIONS 1024*1024
boost::mutex lookupMutex[PARTITIONS];
#endif

FRAME maxFrames;

#ifndef DFS
#include "search_bfs.cpp"
#else
#include "search_dfs.cpp"
#endif

// ***********************************************************************************

int run(int argc, const char* argv[])
{
	printf("Level %d: %dx%d, %d players\n", LEVEL, X, Y, PLAYERS);
#ifdef HAVE_VALIDATOR
	printf("Level validator present\n");
#endif
#ifdef DEBUG
	printf("Debug version\n");
#else
	printf("Optimized version\n");
#endif
#ifdef MULTITHREADING
	printf("Using %d threads\n", THREADS);
#endif
	printf("Using node lookup hashtable of %d elements (%lld bytes)\n", 1<<HASHSIZE, (long long)(1<<HASHSIZE) * sizeof(NODEI));

#ifndef DFS
	printf("Using breadth-first search\n");
	enforce(sizeof(Node) == 10, format("sizeof Node is %d", sizeof(Node)));
#else
	printf("Using depth-first search\n");
	enforce(sizeof(Node) == 16, format("sizeof Node is %d", sizeof(Node)));
#endif
	enforce(sizeof(Action) == 1, format("sizeof Action is %d", sizeof(Action)));

#ifdef SWAP
	printf("Using node cache of %d records (%lld bytes)\n", CACHE_SIZE, (long long)CACHE_SIZE * sizeof(CacheNode));
	printf("Using swap files of %d records (%lld bytes) each\n", ARCHIVE_CLUSTER_SIZE, (long long)ARCHIVE_CLUSTER_SIZE * sizeof(Node));
#ifdef SPLAY
	printf("Using splay tree caching\n");
	enforce(sizeof(CacheNode) == 24, format("sizeof CacheNode is %d", sizeof(CacheNode)));
#else
	printf("Using hashtable caching\n");
	printf("Using cache lookup hashtable of %d elements (%lld bytes)\n", 1<<CACHE_HASHSIZE, (long long)(1<<CACHE_HASHSIZE) * sizeof(CACHEI));
	printf("Cache lookup hashtable is trimmed to %d elements\n", cacheTrimThreshold+1);
	enforce(cacheTrimThreshold>0, "Cache lookup hashtable trim threshold too low");
	enforce(cacheTrimThreshold<=16, "Cache lookup hashtable trim threshold too high");
	enforce(sizeof(CacheNode) == 20, format("sizeof CacheNode is %d", sizeof(CacheNode)));
#endif
#endif
	
	initialState.load();

	maxFrames = MAX_FRAMES;
	if (argc>2)
		error("Too many arguments");
	if (argc==2)
		maxFrames = strtol(argv[1], NULL, 10);

	int result = search();
	//dumpNodes();
#ifdef SWAP
#ifdef ARCHIVE_STATS
	printf("%lu archive operations, %lu unarchive operations\n", archived, unarchived);
#endif
#endif
	return result;
}

// ***********************************************************************************

int main(int argc, const char* argv[]) { return run(argc, argv); }
//#include "test_body.cpp"
