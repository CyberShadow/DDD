#include <time.h>
#include "config.h"
#ifdef MULTITHREADING
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#endif
#include "Kwirk.cpp"
#include "hsiehhash.cpp"

// ******************************************************************************************************

State initialState;

// ******************************************************************************************************

typedef uint32_t NODEI;

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
			NODEI parent;
			NODEI next;
		};
		Step step; // CAREFUL! Setting "step" directly will partially overwrite "parent" because "step" is 4 bytes in size.
	};
};

// ******************************************************************************************************

NODEI nodeCount = 0;

#ifndef SWAP

Node* nodes[0x10000];
#ifdef MULTITHREADING
boost::mutex nodeMutex;
#endif

Node* newNode(NODEI* index)
{
	Node* result;
	/* LOCK */
	{
#ifdef MULTITHREADING
		boost::mutex::scoped_lock lock(nodeMutex);
#endif
		*index = nodeCount;
		if ((nodeCount&0xFFFF) == 0)
			nodes[nodeCount/0x10000] = new Node[0x10000];
		result = nodes[nodeCount/0x10000] + (nodeCount&0xFFFF);
		nodeCount++;
		if (nodeCount == MAX_NODES)
			error("Too many nodes");
	}
	result->next = 0;
	return result;
}

INLINE Node* getNode(NODEI index)
{
	return nodes[index/0x10000] + (index&0xFFFF);
}

// ******************************************************************************************************

#else // SWAP

typedef uint32_t CACHEI;

struct CacheNode
{
	Node data;
	bool dirty;
	char _padding;
	NODEI index; // tree key, 0 - cache node free
	CACHEI left, right;
}

CacheNode cache[CACHE_SIZE];

CACHEI cacheAlloc();

CACHEI cacheSplay(NODEI i, CACHEI t)
{
	CACHEI l, r, y;
	if (t == 0) return t;
	// cache[0] works as a temporary node
	cache[0].left = cache[0].right = 0;
	l = r = 0;

	for (;;)
	{
		CacheNode* tp = &cache[t];
		if (i < tp->index)
		{
			if (tp->left == 0) break;
			if (i < cache[tp->left].index)
			{
				y = tp->left;                           /* rotate right */
				CacheNode* yp = &cache[y];
				tp->left = yp->right;
				yp->right = t;
				t = y;
				if (yp->left == 0) break;
			}
			cache[r].left = t;                               /* link right */
			r = t;
			t = t->left;
		}
		else
		if (i > tp->index)
		{
			if (tp->right == 0) break;
			if (i > cache[tp->right].index)
			{
				y = tp->right;                          /* rotate left */
				CacheNode* yp = &cache[y];
				tp->right = yp->left;
				yp->left = t;
				t = y;
				if (yp->right == 0) break;
			}
			cache[l].right = t;                              /* link left */
			l = t;
			t = t->right;
		}
		else
			break;
	}
	CacheNode* tp = &cache[t];
	cache[l].right = tp->left ;                                /* assemble */
	cache[r].left  = tp->right;
	tp->left  = cache[0].right;
	tp->right = cache[0].left;
	return t;
}

CACHEI cacheInsert(NODEI i, CACHEI t)
{
	CACHEI n = cacheAlloc();
	CacheNode* np = &cache[n];
	np->index = i;
	//if (t == 0)
	//	np->left = np->right = 0;
	t = cacheSplay(i, t);
	CacheNode* tp = &cache[t];
	if (i < tp->index)
	{
		np->left = tp->left;
		np->right = t;
		tp->left = 0;
		return n;
	}
	else 
	if (i > tp->index)
	{
		np->right = tp->right;
		np->left = t;
		tp->right = 0;
		return n;
	}
	else
	{ /* We get here if it's already in the tree */
		assert(0, "Inserted node already in tree");
		return 0;
	}
}

// ******************************************************************************************************

CACHEI cacheFreePtr=1, cacheCount=1, cacheRoot=0;
uint32_t cacheArchived[(MAX_NODES+31)/32];
#ifdef MULTITHREADING
boost::shared_mutex archiveMutex;
#endif

#define MAX_CACHE_TREE_DEPTH 64
#define CACHE_TRIM 4

CACHEI cacheDepthCounts[MAX_CACHE_TREE_DEPTH];
int cacheTrimLevel;

void cacheCount(CACHEI n, int level)
{
	cacheDepthCounts[level >= MAX_CACHE_TREE_DEPTH ? MAX_CACHE_TREE_DEPTH-1 : level]++;
	CacheNode* np = &cache[n];
	CACHEI l = np->left;
	if (l) cacheCount(l, level+1);
	CACHEI r = np->right;
	if (r) cacheCount(r, level+1);
}

void cacheDoTrim(CACHEI n, int level)
{
	CacheNode* np = &cache[n];
	CACHEI l = np->left;
	if (l) cacheDoTrim(l, level+1);
	CACHEI r = np->right;
	if (r) cacheDoTrim(r, level+1);
	if (level > cacheTrimLevel)
	{
		// TODO: save node
		// TODO: mark node as archived
		np->index = 0;
		cacheCount--;
	}
	else
	if (level == cacheTrimLevel)
		np->left = np->right = 0;
}

CACHEI cacheTrim()
{
	for (int i=0; i<MAX_CACHE_TREE_DEPTH; i++)
		cacheDepthCounts[i] = 0;
	cacheCount(cacheRoot, 0);
#ifdef DEBUG
	CACHEI total = 1;
	for (int i=0; i<MAX_CACHE_TREE_DEPTH; i++)
		total += cacheDepthCounts[i];
	assert(total == cacheCount);
#endif
	CACHEI nodes = 0;
	const threshold = CACHE_SIZE / CACHE_TRIM;
	for (int i=MAX_CACHE_TREE_DEPTH-1; i>=0; i--)
	{
		nodes += cacheDepthCounts[i];
		if (nodes > threshold)
			break;
	}
	assert(i>0);
	cacheTrimLevel = i-1;
	cacheDoTrim(cacheRoot, 0);
}

CACHEI cacheAlloc()
{
	if (cacheCount == CACHE_SIZE)
		error("Cache overflow"); // How could we have let this HAPPEN?!?
	while (cache[cacheFreePtr].index)
		cacheFreePtr = cacheFreePtr==(CACHE_SIZE-1) ? 1 : cacheFreePtr+1;
	cacheCount++;
}

// ******************************************************************************************************

// this is technically not required, but provides an optimization:
#ifdef MULTITHREADING
boost::mutex cacheMutex;
#endif

Node* newNode(NODEI* index)
{
	CACHEI c;
	/* LOCK */
	{
#ifdef MULTITHREADING
		boost::mutex::scoped_lock lock(cacheMutex);
#endif
		*index = nodeCount;
		c = cacheRoot = cacheInsert(nodeCount, cacheRoot);
		nodeCount++;
		if (nodeCount == MAX_NODES)
			error("Too many nodes");
	}
	Node* result = &cache[c].data;
	result->next = 0;
	return result;
}

Node* getNode(NODEI index)
{
	CACHEI archiveIndex = index/32;
	uint32_t archiveMask = 1<<(index%32);

	bool archived;
    /* LOCK */
    {
#ifdef MULTITHREADING
		boost::shared_lock lock(archiveMutex);
#endif
		archived = cacheArchived[archiveIndex] & archiveMask;
    }

    // possible sync gap for cacheArchived here

    CACHEI c;
    if (archived)
    {

#ifdef MULTITHREADING
		boost::upgrade_lock alock(archiveMutex);
		boost::upgrade_to_unique_lock ulock(alock);
#endif
		uint32_t a = cacheArchived[archiveIndex];
		if ((a & archiveMask)==0) // it was unarchived by another thread
		{
			archived = false;
		}
		else
		{
			a &= ~archiveMask;
			cacheArchived[archiveIndex] = a;
		
			/* LOCK */
			{
#ifdef MULTITHREADING
			boost::mutex::scoped_lock lock(cacheMutex);
#endif
			c = cacheRoot = cacheInsert(index, cacheRoot);
			// TODO: unarchive node
		}
    }
    
    if (!archived)
    {
#ifdef MULTITHREADING
		boost::mutex::scoped_lock lock(cacheMutex);
#endif
		c = cacheRoot = cacheSplay(index, cacheRoot);
    }


	CACHEI c;
	/* LOCK */
	{
#ifdef MULTITHREADING
		boost::mutex::scoped_lock lock(cacheMutex);
#endif
		if (
		*index = nodeCount;
		c = cacheRoot = cacheInsert(nodeCount, cacheRoot);
		nodeCount++;
		if (nodeCount == MAX_NODES)
			error("Too many nodes");
	}
	Node* result = &cache[c].data;
	result->next = 0;
	return result;
}

void postNode()
{
	if (cacheCount > CACHE_TRIM_THRESHOLD)
	{
		barrier;
		mutex
		{
			if (cacheCount > CACHE_TRIM_THRESHOLD)
			{
				cacheTrim();
				assert(cacheCount < CACHE_TRIM_THRESHOLD);
			}
			else
			{
				// another thread took care of it
			}
		}
	}
}

#endif // SWAP

// ******************************************************************************************************

const char* actionNames[] = {"Up", "Right", "Down", "Left", "Switch", "None"};

int replayStep(State* state, int* frame, Step step)
{
	Player* p = &state->players[state->activePlayer];
	int nx = step.x+1;
	int ny = step.y+1;
	int steps = abs((int)p->x - nx) + abs((int)p->y - ny) + step.extraSteps;
	p->x = nx;
	p->y = ny;
	int res = state->perform((Action)step.action);
	assert(res>0, "Replay failed");
    *frame += steps * DELAY_MOVE + res;
	return steps; // not counting actual action
}

int replayState(Node* n, State* state, int* frame)
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

void dumpChain(FILE* f, Node* n)
{
	Step steps[MAX_STEPS+1];
	unsigned int stepNr = 0;
	Node* cur = n;

	while ((Action)cur->step.action != NONE)
	{
		steps[stepNr++] = cur->step;
		cur = getNode(cur->parent);
		//assert(stepNr <= MAX_STEPS, "Too many nodes in dumpChain");
	}
	steps[stepNr].action = NONE;
	steps[stepNr].x = initialState.players[0].x-1;
	steps[stepNr].y = initialState.players[0].y-1;
	unsigned int totalSteps = 0;
	State state = initialState;
	int frame = 0;
	while (stepNr)
	{
		fprintf(f, "@%d,%d: %s\n%s", steps[stepNr].x+1, steps[stepNr].y+1, actionNames[steps[stepNr].action], state.toString());
		totalSteps += (steps[stepNr].action<SWITCH ? 1 : 0) + replayStep(&state, &frame, steps[--stepNr]);
	}
	// last one
	fprintf(f, "@%d,%d: %s\n%s", steps[0].x+1, steps[0].y+1, actionNames[steps[0].action], state.toString());
	fprintf(f, "Total steps: %d", totalSteps);
}

// ******************************************************************************************************

struct QueueNode
{
	NODEI node;
	QueueNode* next;
};

QueueNode* queue[MAX_FRAMES];
int queueCount[MAX_FRAMES];
#ifdef MULTITHREADING
boost::mutex queueMutex[MAX_FRAMES];
#endif

void queueNode(NODEI node, int frame)
{
#ifdef MULTITHREADING
	boost::mutex::scoped_lock lock(queueMutex[frame]);
#endif
	if (frame >= MAX_FRAMES)
		return;
	QueueNode* q = new QueueNode;
	q->node = node;
	q->next = queue[frame];
	queue[frame] = q;
	queueCount[frame]++;
}

NODEI dequeueNode(int frame)
{
	QueueNode* q;
	/* LOCK */
	{
#ifdef MULTITHREADING
		boost::mutex::scoped_lock lock(queueMutex[frame]);
#endif
		q = queue[frame];
		if (q == NULL)
			return 0;
		queue[frame] = q->next;
		queueCount[frame]--;
	}
	NODEI result = q->node;
	delete q;
	return result;
}

// ******************************************************************************************************

#define HASHSIZE 28
typedef uint32_t HASH;
NODEI lookup[1<<HASHSIZE];
#ifdef MULTITHREADING
#define PARTITIONS 1024*1024
boost::mutex lookupMutex[PARTITIONS];
#endif

void addNode(State* state, NODEI parent, Step step, unsigned int frame)
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
		while (n)
		{
			State other;
			Node* np = getNode(n);
			int otherFrame;
			replayState(np, &other, &otherFrame);
			if (*state == other)
			{
				if (otherFrame > frame) // better path found? reparent and requeue
				{
					// this node will always be still queued, so we don't need to worry about its children
					np->step = step;
					np->parent = parent;
					queueNode(n, frame);
				}
				return;
			}
			n = np->next;
		}
		Node* np = newNode(&nn);
		lookup[hash] = nn;
		np->step = step;
		np->parent = parent;
		np->next = old;
	}
	queueNode(nn, frame);
}

// ******************************************************************************************************

int frame;

void processNode(NODEI n)
{
	Node* np = getNode(n);
	State state;
	int stateFrame;
	int steps = replayState(np, &state, &stateFrame);
	if (stateFrame != frame)
		return; // node was reparented and requeued
	if (state.playersLeft()==0)
	{
		printf("\nExit found, writing path...\n");
		FILE* f = fopen(BOOST_PP_STRINGIZE(LEVEL) ".txt", "wt");
		dumpChain(f, np);
		fclose(f);
		//File.set(LEVEL ~ ".txt", chainToString(state));
		printf("Done.\n");
		exit(0);
	}
	else
	{
		struct Coord { BYTE x, y; };
		const int QUEUELENGTH = X+Y;
		Coord queue[QUEUELENGTH];
		BYTE distance[Y-2][X-2];
		uint32_t queueStart=0, queueEnd=1;
		memset(distance, 0xFF, sizeof(distance));
		
		BYTE x0 = state.players[state.activePlayer].x;
		BYTE y0 = state.players[state.activePlayer].y;
		queue[0].x = x0;
		queue[0].y = y0;
		distance[y0-1][x0-1] = 0;

		State newState = state;
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
				BYTE m = state.map[ny][nx];
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
						newState = state;
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
}

void worker()
{
	NODEI n;
	while((n=dequeueNode(frame))!=0)
		processNode(n);
}

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

	int maxFrames = MAX_FRAMES;
	if (argc>2)
		error("Too many arguments");
	if (argc==2)
		maxFrames = strtol(argv[1], NULL, 10);

	// initialize state
	memset(lookup, 0, sizeof lookup);
	memset(queue, 0, sizeof queue);

	NODEI dummy;
	newNode(&dummy); // node 0 is reserved
	Step nullStep = { (unsigned)NONE };
	addNode(&initialState, 0, nullStep, 0);

	for (frame=0;frame<maxFrames;frame++)
	{
		if (!queue[frame])
			continue;
		
		time_t t;
		time(&t);
		char* tstr = ctime(&t);
		tstr[strlen(tstr)-1] = 0;
		printf("[%s] Frame %d/%d: %d/%d nodes", tstr, frame, maxFrames, queueCount[frame], nodeCount-1); fflush(stdout);
		NODEI oldNodes = nodeCount;
		
#ifdef MULTITHREADING
		boost::thread* threads[THREADS];
		for (int i=0; i<THREADS; i++)
			threads[i] = new boost::thread(&worker);
		for (int i=0; i<THREADS; i++)
		{
			threads[i]->join();
			delete threads[i];
		}
#else
		worker();
#endif
		assert(queueCount[frame]==0);

		printf(", %d new\n", nodeCount-oldNodes);
	}
	printf("Exit not found.\n");
	return 2;
}

int main(int argc, const char* argv[]) { return run(argc, argv); }
//#include "test_body.cpp"
