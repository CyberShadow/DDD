#pragma pack(1)
#include <time.h>
#include "Kwirk.cpp"
#include "hsiehhash.cpp"

// ******************************************************************************************************

State initialState;

// ******************************************************************************************************

typedef uint32_t NODEI;

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
			unsigned frame:13;
		};
	};
};

Node* nodes[0x10000];
NODEI nodeCount = 0;

Node* newNode()
{
	if ((nodeCount&0xFFFF) == 0)
		nodes[nodeCount/0x10000] = new Node[0x10000];
	Node* result = nodes[nodeCount/0x10000] + (nodeCount&0xFFFF);
#ifdef DEBUG
	result->action = NONE;
	result->parent = 0;
	result->frame = 0;
#endif
	result->next = 0;
	nodeCount++;
	if (nodeCount==0)
		error("Node count overflow");
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

int replayState(Node* n, State* state)
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
	while (stepNr)
	{
		int res = state->perform(actions[--stepNr]);
		assert(res>0, "Replay failed");
		// if (nodeCount > 40000) printf("%c", actionNames[actions[stepNr]][0]);
	}
	// if (nodeCount > 40000) printf("\n");
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

void queueNode(NODEI node, int frame)
{
	if (frame >= MAX_FRAMES)
		return;
	QueueNode* q = new QueueNode;
	q->node = node;
	q->next = queue[frame];
	queue[frame] = q;
}

// ******************************************************************************************************

#define HASHSIZE 28
typedef uint32_t HASH;
NODEI lookup[1<<HASHSIZE];

void addNode(State* state, NODEI parent, Action action, unsigned int frame)
{
	HASH hash = SuperFastHash((const char*)state, sizeof(State)) & ((1<<HASHSIZE)-1);
	NODEI old = lookup[hash];
	NODEI n = old;
	while (n)
	{
		State other;
		Node* np = getNode(n);
		replayState(np, &other);
		if (*state == other)
			return;
		n = np->next;
	}
	lookup[hash] = nodeCount;
	queueNode(nodeCount, frame);
	Node* np = newNode();
	np->parent = parent;
	np->frame = frame;
	np->action = (unsigned)action;
	np->next = old;
}

// ******************************************************************************************************

int run(int argc, const char* argv[])
{
	printf("Level %d: %dx%d, %d players\n", LEVEL, X, Y, PLAYERS);
#ifdef DEBUG
	printf("Debug version\n");
#else
	printf("Optimized version\n");
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

	newNode(); // node 0 is reserved
	addNode(&initialState, 0, NONE, 0);

	for (int frame=0;frame<maxFrames;frame++)
	{
		QueueNode* q = queue[frame];
		if (!q)
			continue;
		time_t t;
		time(&t);
		char* tstr = ctime(&t);
		tstr[strlen(tstr)-1] = 0;
		printf("[%s] Frame %d/%d: %d nodes", tstr, frame, maxFrames, nodeCount); fflush(stdout);
		unsigned int queueCount = 0;
		NODEI oldNodes = nodeCount;
		
		while (q)
		{
			queueCount++;
			NODEI n = q->node;
			Node* np = getNode(n);
			State state;
			int steps = replayState(np, &state);
			if (state.playersLeft()==0)
			{
				printf("\nExit found, writing path...\n");
				FILE* f = fopen(BOOST_PP_STRINGIZE(LEVEL) ".txt", "wt");
				dumpChain(f, np);
				fclose(f);
				//File.set(LEVEL ~ ".txt", chainToString(state));
				printf("Done.\n");
				return 0;
			}
			else
				if (steps < MAX_STEPS)
				{
					State newState = state;
					for (Action action = ACTION_FIRST; action <= ACTION_LAST; action++)
					{
						int result = newState.perform(action);
						if (result > 0)
							addNode(&newState, n, action, frame+result);
						if (result >= 0) // the state was altered
							newState = state;
					}	
				}

			QueueNode* oldq = q;
			q = q->next;
			delete oldq;
		}
		printf(", %d nodes processed, %d new nodes\n", queueCount, nodeCount-oldNodes);
	}
	printf("Exit not found.\n");
	return 2;
}

int main(int argc, const char* argv[]) { return run(argc, argv); }
//#include "test_body.cpp"
