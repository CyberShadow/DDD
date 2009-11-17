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

int replayStep(State* state, FRAME* frame, Step step)
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

int main(int argc, const char* argv[]) { return run(argc, argv); }
//#include "test_body.cpp"
