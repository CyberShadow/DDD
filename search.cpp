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
		struct
		{
			unsigned action:3;
			unsigned dummy:15;
		};
	};
};

Node* nodes[0x10000];
NODEI nodeCount = 0;
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
		if (nodeCount==0)
			error("Node count overflow");
	}
#ifdef DEBUG
	result->action = NONE;
	result->parent = 0;
#endif
	result->next = 0;
	extern uint32_t treeNodeCount;
	// if (nodeCount > 40000) printf("\nNodes: %d/%d\n", nodeCount, treeNodeCount);
	return result;
}

INLINE Node* getNode(NODEI index)
{
	return nodes[index/0x10000] + (index&0xFFFF);
}

// ******************************************************************************************************

const char* actionNames[] = {"Up", "Right", "Down", "Left", "Switch", "None"};

int replayState(Node* n, State* state, int* frame)
{
	Action actions[MAX_STEPS+1];
	unsigned int stepNr = 0;
	Node* cur = n;

	while (cur->action != NONE)
	{
		actions[stepNr++] = (Action)cur->action;
		cur = getNode(cur->parent);
		if (stepNr > MAX_STEPS)
			return stepNr;
	}
	unsigned int stepCount = stepNr;
	*state = initialState;
	*frame = 0;
	while (stepNr)
	{
		int res = state->perform(actions[--stepNr]);
		assert(res>0, "Replay failed");
		*frame += res;
	}
	return stepCount;
}

void dumpChain(FILE* f, Node* n)
{
	Action actions[MAX_STEPS+1];
	unsigned int stepNr = 0;
	Node* cur = n;

	while ((Action)cur->action != NONE)
	{
		actions[stepNr++] = (Action)cur->action;
		cur = getNode(cur->parent);
		//assert(stepNr <= MAX_STEPS, "Too many nodes in dumpChain");
	}
	actions[stepNr] = NONE;
	unsigned int totalSteps = 0;
	State state = initialState;
	while (stepNr)
	{
		fprintf(f, "%s\n%s", actionNames[actions[stepNr]], state.toString());
		int res = state.perform(actions[--stepNr]);
		assert(res>0, "Replay failed");
		if (actions[stepNr] < SWITCH)
			totalSteps++;
	}
	// last one
	fprintf(f, "%s\n%s", actionNames[actions[0]], state.toString());
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

void addNode(State* state, NODEI parent, Action action, unsigned int frame)
{
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
					np->parent = parent;
					np->action = action;
					queueNode(n, frame);
				}
				return;
			}
			n = np->next;
		}
		Node* np = newNode(&nn);
		lookup[hash] = nn;
		np->parent = parent;
		np->action = (unsigned)action;
		np->next = old;
	}
	queueNode(nn, frame);
}

// ******************************************************************************************************

int frame;

void worker()
{
	NODEI n;
	while((n=dequeueNode(frame))!=0)
	{
		Node* np = getNode(n);
		State state;
		int stateFrame;
		int steps = replayState(np, &state, &stateFrame);
		if (stateFrame != frame)
			continue; // node was reparented and requeued
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
			if (steps < MAX_STEPS)
			{
				State newState = state;
				for (Action action = ACTION_FIRST; action <= ACTION_LAST; action++)
				{
					int result = newState.perform(action);
					if (result > 0)
						addNode(&newState, n, action, stateFrame+result);
					if (result >= 0) // the state was altered
						newState = state;
				}	
			}
	}
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
	addNode(&initialState, 0, NONE, 0);

	for (frame=0;frame<maxFrames;frame++)
	{
		if (!queue[frame])
			continue;
		
		time_t t;
		time(&t);
		char* tstr = ctime(&t);
		tstr[strlen(tstr)-1] = 0;
		printf("[%s] Frame %d/%d: %d nodes, %d total", tstr, frame, maxFrames, queueCount[frame], nodeCount); fflush(stdout);
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

		printf(", %d new nodes\n", nodeCount-oldNodes);
	}
	printf("Exit not found.\n");
	return 2;
}

int main(int argc, const char* argv[]) { return run(argc, argv); }
//#include "test_body.cpp"
