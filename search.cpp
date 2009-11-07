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

struct Node
{
	// compiler hack - minimum bitfield size is 32 bits
	union
	{
		struct
		{
			short bitfield;
			NODEI next;
			NODEI parent;
		};
		Step step; // CAREFUL! Setting "step" directly will partially overwrite "next" because "step" is 4 bytes in size.
	};
};

// ******************************************************************************************************

NODEI nodeCount = 0;
int threadsRunning = 0;

#ifndef SWAP
#include "nodes_flat.cpp"
#else
#include "nodes_swap.cpp"
#endif

// ******************************************************************************************************

const char* actionNames[] = {"Up", "Right", "Down", "Left", "Switch", "None"};

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

int replayState(Node* n, State* state, FRAME* frame)
{
	Step steps[MAX_STEPS+1];
	unsigned int stepNr = 0;
	Node* cur = n;

	while ((Action)cur->step.action != NONE)
	{
		steps[stepNr++] = cur->step;
		cur = getNode(cur->parent);
		if (stepNr > MAX_STEPS)
			return stepNr;
	}
	unsigned int totalSteps = 0;
	*state = initialState;
	*frame = 0;
	while (stepNr)
		totalSteps += (steps[stepNr].action<SWITCH ? 1 : 0) + replayStep(state, frame, steps[--stepNr]);
	return totalSteps;
}

void dumpChain(FILE* f, const Node* n)
{
	Step steps[MAX_STEPS+1];
	unsigned int stepNr = 0;
	const Node* cur = n;

	while ((Action)cur->step.action != NONE)
	{
		steps[stepNr++] = cur->step;
		cur = getNodeFast(cur->parent);
		//assert(stepNr <= MAX_STEPS, "Too many nodes in dumpChain");
	}
	steps[stepNr].action = NONE;
	steps[stepNr].x = initialState.players[0].x-1;
	steps[stepNr].y = initialState.players[0].y-1;
	unsigned int totalSteps = 0;
	State state = initialState;
	FRAME frame = 0;
	while (stepNr)
	{
		fprintf(f, "@%d,%d: %s\n%s", steps[stepNr].x+1, steps[stepNr].y+1, actionNames[steps[stepNr].action], state.toString());
		totalSteps += (steps[stepNr].action<SWITCH ? 1 : 0) + replayStep(&state, &frame, steps[--stepNr]);
	}
	// last one
	fprintf(f, "@%d,%d: %s\n%s", steps[0].x+1, steps[0].y+1, actionNames[steps[0].action], state.toString());
	fprintf(f, "Total steps: %d", totalSteps);
}

void dumpNodes()
{
	for (NODEI n=1; n<nodeCount; n++)
	{
		const Node* np = getNodeFast(n);
		printf("node[%d]: %d -> %s\n", n, np->parent, actionNames[np->step.action]);
	}
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

void processNodeChildren(NODEI n, FRAME frame, const State* state);
FRAME maxFrames;

#ifndef DFS
#include "search_bfs.cpp"
#else
#ifdef MULTITHREADING
#include "search_dfs_mt.cpp"
#else
#include "search_dfs_st.cpp"
#endif
#endif

// ***********************************************************************************

#define HASHSIZE 28
typedef uint32_t HASH;
NODEI lookup[1<<HASHSIZE];
#ifdef MULTITHREADING
#define PARTITIONS 1024*1024
boost::mutex lookupMutex[PARTITIONS];
#endif

void addNode(const State* state, NODEI parent, Step step, FRAME frame)
{
	#ifdef HAVE_VALIDATOR
	if (!state->validate())
		return;
	#endif

	HASH hash = SuperFastHash((const char*)state, sizeof(State)) & ((1<<HASHSIZE)-1);
	NODEI nn;
	{
#ifdef MULTITHREADING
		boost::mutex::scoped_lock lock(lookupMutex[hash % PARTITIONS]);
#endif
		NODEI old = lookup[hash];
		NODEI n = old;
		Node* prev = NULL;
		while (n)
		{
			State other;
			Node* np = getNode(n);
			FRAME otherFrame;
			replayState(np, &other, &otherFrame);
			if (*state == other)
			{
				if (otherFrame > frame) // better path found? reparent and requeue
					reparentNode(n, parent, np, frame, state, step);
				// pop node to front of hash list
				if (prev)
				{
					error("Hash collision");
					prev->next = np->next;
					markDirty(prev);
					np->next = old;
					lookup[hash] = n;
					markDirty(np);
				}
				return;
			}
			n = np->next;
			prev = np;
		}
		Node* np = newNode(&nn);
		lookup[hash] = nn;
		np->step = step;
		np->parent = parent;
		np->next = old;
	}
	queueNode(nn, frame, state);
}

void processNodeChildren(NODEI n, FRAME frame, const State* state)
{
	struct Coord { BYTE x, y; };
	const int QUEUELENGTH = X+Y;
	Coord queue[QUEUELENGTH];
	BYTE distance[Y-2][X-2];
	uint32_t queueStart=0, queueEnd=1;
	memset(distance, 0xFF, sizeof(distance));
	
	BYTE x0 = state->players[state->activePlayer].x;
	BYTE y0 = state->players[state->activePlayer].y;
	queue[0].x = x0;
	queue[0].y = y0;
	distance[y0-1][x0-1] = 0;

	State newState = *state;
	Player* np = &newState.players[newState.activePlayer];
	while(queueStart != queueEnd)
	{
		Coord c = queue[queueStart];
		queueStart = (queueStart+1) % QUEUELENGTH;
		BYTE dist = distance[c.y-1][c.x-1];
		Step step;
		step.x = c.x-1;
		step.y = c.y-1;
		step.extraSteps = dist - (abs((int)c.x - (int)x0) + abs((int)c.y - (int)y0));

		#if (PLAYERS>1)
			np->x = c.x;
			np->y = c.y;
			int res = newState.perform(SWITCH);
			assert(res == DELAY_SWITCH);
			step.action = (unsigned)SWITCH;
			addNode(&newState, n, step, frame + dist * DELAY_MOVE + DELAY_SWITCH);
			newState = state;
		#endif

		for (Action action = ACTION_FIRST; action < SWITCH; action++)
		{
			BYTE nx = c.x + DX[action];
			BYTE ny = c.y + DY[action];
			BYTE m = newState.map[ny][nx];
			if (m & OBJ_MASK)
			{
				np->x = c.x;
				np->y = c.y;
				int res = newState.perform(action);
				if (res > 0)
				{
					step.action = (unsigned)action;
					addNode(&newState, n, step, frame + dist * DELAY_MOVE + res);
				}
				if (res >= 0)
					newState = *state;
			}
			else
			if ((m & CELL_MASK) == 0)
				if (distance[ny-1][nx-1] == 0xFF)
				{
					distance[ny-1][nx-1] = dist+1;
					queue[queueEnd].x = nx;
					queue[queueEnd].y = ny;
					queueEnd = (queueEnd+1) % QUEUELENGTH;
					assert(queueEnd != queueStart, "Queue overflow");
				}
		}
	}
}

// ***********************************************************************************

int run(int argc, const char* argv[])
{
	printf("Level %d: %dx%d, %d players\n", LEVEL, X, Y, PLAYERS);
#ifdef DEBUG
	printf("Debug version\n");
#else
	printf("Optimized version\n");
#endif
#ifdef MULTITHREADING
	printf("Using %d threads\n", THREADS);
#endif

	assert(sizeof(Node) == 10, format("sizeof Node is %d", sizeof(Node)));
	assert(sizeof(Action) == 1, format("sizeof Action is %d", sizeof(Action)));
#ifdef SWAP
	assert(sizeof(CacheNode) == 24, format("sizeof CacheNode is %d", sizeof(Action)));
#endif
	
	initialState.load();

	maxFrames = MAX_FRAMES;
	if (argc>2)
		error("Too many arguments");
	if (argc==2)
		maxFrames = strtol(argv[1], NULL, 10);

	// initialize state
	memset(lookup, 0, sizeof lookup);

	reserveNode(); // node 0 is reserved
	searchInit();
	Step nullStep = { (unsigned)NONE };
	addNode(&initialState, 0, nullStep, 0);

	return search();
}

int main(int argc, const char* argv[]) { return run(argc, argv); }
//#include "test_body.cpp"
