#pragma pack(1)
#include <time.h>
#include "kwirk.cpp"

#define DUMPSTATS

// ******************************************************************************************************

State initialState;
void dumpData(char* name);

// ******************************************************************************************************

typedef unsigned long NODEI;

struct Node
{
	NODEI parent;
	unsigned short frame;
	Action action;
	BYTE filler;
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
	result->filler = 0;
#endif
	nodeCount++;
	if (nodeCount==0)
		error("Node count overflow");
	extern unsigned long treeNodeCount;
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
		actions[stepNr++] = cur->action;
		cur = getNode(cur->parent);
		if (stepNr > MAX_STEPS)
			return stepNr;
	}
	unsigned int stepCount = stepNr;
	*state = initialState;
	while (stepNr)
	{
		state->perform(actions[--stepNr]);
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

	while (cur->action != NONE)
	{
		actions[stepNr++] = cur->action;
		cur = getNode(cur->parent);
		//assert(stepNr <= MAX_STEPS, "Too many nodes in dumpChain");
	}
	actions[stepNr] = NONE;
	unsigned int totalSteps = 0;
	State state = initialState;
	while (stepNr)
	{
		fprintf(f, "%s\n%s", actionNames[actions[stepNr]], state.toString());
		state.perform(actions[--stepNr]);
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

typedef unsigned long TREEI;
const NODEI NODEI_FILLER = 0xFFFFFFFF;

union TreeNode
{
	struct
	{
		 TREEI children[2];
	};
	struct
	{
		NODEI node;
		NODEI filler;
	};

	INLINE bool isLeaf()
	{
		return filler == NODEI_FILLER;
	}
};

TreeNode* treeNodes[0x10000];
TREEI treeNodeCount = 0;
unsigned int maxTreeDepth;

INLINE void updateDepth(unsigned int depth)
{
	assert(depth < sizeof CompressedState*8, "Depth exceeded");
	if (depth>maxTreeDepth)
		maxTreeDepth = depth; 
}

TreeNode* newTreeNode()
{
	if ((treeNodeCount&0xFFFF) == 0)
		treeNodes[treeNodeCount/0x10000] = new TreeNode[0x10000];
	TreeNode* result = treeNodes[treeNodeCount/0x10000] + (treeNodeCount&0xFFFF);
#ifdef DEBUG
	result->children[0] = 0;
	result->children[1] = 0;
#endif
	treeNodeCount++;
	if (treeNodeCount==0)
		error("TreeNode count overflow");
	// if (nodeCount > 40000) printf("\nNodes: %d/%d\n", nodeCount, treeNodeCount);
	return result;
}

INLINE TreeNode* getTreeNode(TREEI index)
{
	return treeNodes[index/0x10000] + (index&0xFFFF);
}

void dumpState(CompressedState* state)
{
	BYTE* stateBytes = (BYTE*)state;
	unsigned int bitPos = 0;
	unsigned int bytePos = 0;

	while (bytePos < sizeof CompressedState)
	{
		printf("%d", (stateBytes[bytePos] >> bitPos) & 1);
		bitPos++;
		bytePos += bitPos/8;
		bitPos %= 8;
	}
}

void addNode(State* state, NODEI parent, Action action, unsigned int frame)
{
	CompressedState compressedState;
	state->compress(&compressedState);
	BYTE* stateBytes = (BYTE*)&compressedState;
	// if (nodeCount > 40000) { printf("Adding node: %5d/%6s/%3d\n%s", parent, actionNames[action], frame, state->toString());  dumpState(&compressedState); printf("\n"); }
	unsigned int bitPos = 0;
	unsigned int bytePos = 0;
	TREEI treePos = 0;

	while(1)
	{
		TreeNode* treeNode = getTreeNode(treePos);
		if (treeNode->isLeaf())
		{
			NODEI otherIndex = treeNode->node;
			Node* other = getNode(otherIndex);

			State otherState;
			// if (nodeCount > 40000) printf(" - Dead end. Other state:\n");
			int steps = replayState(other, &otherState);
			if (steps > MAX_STEPS) { dumpChain(stderr, other); error(format("replayState of another state failed")); }
			
			CompressedState compressedOtherState;
			otherState.compress(&compressedOtherState); // OPTIMIZATION TODO?
			// if (nodeCount > 40000) { printf("%s", state->toString()); dumpState(&compressedOtherState); printf("\n"); }

			// identical states?
			if (compressedState == compressedOtherState)
			{
				if (frame < other->frame)
				{
					other->parent = parent;
					other->action = action;
					other->frame = frame;
					queueNode(otherIndex, frame); // requeue - minor performance overhead
					// if (nodeCount > 40000) { printf("\nRequeued"); }
				}
				// if (nodeCount > 40000) { printf("\nDuplicate\n"); }
				return;
			}

			BYTE* otherStateBytes = (BYTE*)&compressedOtherState;

#ifdef DEBUG
			unsigned int bit2Pos = 0;
			unsigned int byte2Pos = 0;
			while (bit2Pos!=bitPos || byte2Pos!=bytePos)
			{
				if (((stateBytes[byte2Pos] >> bit2Pos) & 1) != ((otherStateBytes[byte2Pos] >> bit2Pos) & 1))
					error(format("Computed compressed state does not match tree path (pos %d)", bit2Pos + byte2Pos*8));
				bit2Pos++;
				byte2Pos += bit2Pos/8;
				bit2Pos %= 8;
			}
#endif

			// if (nodeCount > 40000) { printf("..."); }

			// expand tree until the first difference in the compressed states
			BYTE dir;
			while ((dir = (stateBytes[bytePos] >> bitPos) & 1) == ((otherStateBytes[bytePos] >> bitPos) & 1))
			{
				assert(dir < 2);
				// if (nodeCount > 40000) printf("%d", dir);
				treeNode->children[dir  ] = treeNodeCount;
				treeNode->children[dir^1] = 0;

				bitPos++;
				bytePos += bitPos/8;
				bitPos %= 8;

				treeNode = newTreeNode();
			}

			updateDepth(bitPos + bytePos*8);

			// create a fork with two leaves
			assert(dir < 2);
			treeNode->children[dir  ] = treeNodeCount;
			treeNode->children[dir^1] = treeNodeCount+1;

			TreeNode* myTreeNode = newTreeNode();
			myTreeNode->node = nodeCount;
			myTreeNode->filler = NODEI_FILLER;

			queueNode(nodeCount, frame);
			
			Node* nNode = newNode();
			nNode->parent = parent;
			nNode->action = action;
			nNode->frame = frame;

			TreeNode* otherTreeNode = newTreeNode();
			otherTreeNode->node = otherIndex;
			otherTreeNode->filler = NODEI_FILLER;

			// if (nodeCount > 40000) { printf("\nExpanded and forked\n"); }
			return;
		}
		else
		{
			BYTE dir = (stateBytes[bytePos] >> bitPos) & 1;
			// if (nodeCount > 40000) printf("%d", dir);
			TREEI child = treeNode->children[dir];
			if (child)
			{
				bitPos++;
				bytePos += bitPos/8;
				bitPos %= 8;
				treePos = child;
			}
			else
			{
				// new leaf here
				treeNode->children[dir] = treeNodeCount;
				
				TreeNode* nTreeNode = newTreeNode();
				nTreeNode->node = nodeCount;
				nTreeNode->filler = NODEI_FILLER;
				
				queueNode(nodeCount, frame);

				Node* nNode = newNode();
				nNode->parent = parent;
				nNode->action = action;
				nNode->frame = frame;

				updateDepth(bitPos + bytePos*8);
				// if (nodeCount > 40000) { printf("\nBranched\n"); }
				return;
			}
		}
	}
}

// ******************************************************************************************************

#ifdef DUMPSTATS
unsigned int treeCounts[sizeof CompressedState*8];

void dumpStats(TREEI index, int level)
{
	treeCounts[level]++;
	TreeNode* n = getTreeNode(index);
	if (!n->isLeaf())
	{
		if (n->children[0])
			dumpStats(n->children[0], level+1);
		if (n->children[1])
			dumpStats(n->children[1], level+1);
	}
}
#endif

void finalize()
{
	#ifdef DUMPSTATS
	printf("Dumping stats...\n");
	memset(treeCounts, 0, sizeof treeCounts);
	dumpStats(0, 0);
	for (int i=0; i<sizeof CompressedState*8; i++)
		printf("%d ", treeCounts[i]);
	printf("\nDone.\n");
	#endif
}

// ******************************************************************************************************

int run(int argc, const char* argv[])
{
	printf("Level %d: %dx%d, %d players, %d blocks, %d rotators\n", LEVEL, X, Y, PLAYERS, BLOCKS, ROTATORS);
	printf("Compressed state size: %d (%d bits)\n\tFloor: %d bytes (%d bits)\n\tPlayers: %d bits\n\tBlocks: %d bits\n\tRotators: %d bits\n", 
		sizeof CompressedState, sizeof CompressedState*8, 
		HOLEBYTES, HOLEBYTES*8, 
		PLAYERS*(XBITS+YBITS+1), 
		BLOCKS*(XBITS+YBITS+BLOCKXBITS+BLOCKYBITS),
		ROTATORS*4);

	assert(sizeof TreeNode == 8);
	assert(sizeof Node == 8, format("sizeof Node is %d", sizeof Node));
	
	initialState.load();

	int maxFrames = MAX_FRAMES;
	if (argc>2)
		error("Too many arguments");
	if (argc==2)
		maxFrames = strtol(argv[1], NULL, 10);

	// initialize state
	Node* firstNode = newNode();
	firstNode->action = NONE;
	firstNode->frame = 0;

	memset(queue, 0, sizeof queue);
	queueNode(0, 0);

	TreeNode* treeHead = newTreeNode();
	treeHead->node = 0;
	treeHead->filler = NODEI_FILLER;

	for (int frame=0;frame<maxFrames;frame++)
	{
		QueueNode* q = queue[frame];
		if (!q)
			continue;
		time_t t;
		time(&t);
		char* tstr = ctime(&t);
		tstr[strlen(tstr)-1] = 0;
		printf("[%s] Frame %d/%d: %d tree nodes, %d state nodes, max tree depth=%d/%d", tstr, frame, maxFrames, treeNodeCount, nodeCount, maxTreeDepth, sizeof CompressedState*8);
		unsigned int queueCount = 0;
		NODEI oldNodes = nodeCount;
		TREEI oldTreeNodes = treeNodeCount;
		
		while (q)
		{
			queueCount++;
			NODEI n = q->node;
			Node* np = getNode(n);
			State state;
			int steps = replayState(np, &state);
			if (state.playersLeft()==0)
			{
				printf("Exit found, writing path...\n");
				FILE* f = fopen(BOOST_PP_STRINGIZE(LEVEL) ".txt", "wt");
				dumpChain(f, np);
				fclose(f);
				//File.set(LEVEL ~ ".txt", chainToString(state));
				printf("Done.\n");
				finalize();
				return 0;
			}
			else
				if (steps < MAX_STEPS)
				{
					State newState = state;
					for (Action action = ACTION_FIRST; action <= ACTION_LAST; action++)
					{
						int result = newState.perform(action);
						if (result >= 0)
						{
							addNode(&newState, n, action, frame+result);
							newState = state;
						}
						else
						{
							// preserve newState which hasn't changed
						}
					}	
				}

			QueueNode* oldq = q;
			q = q->next;
			delete oldq;
		}
		printf(", %d nodes processed, %d new tree nodes, %d new state nodes\n", queueCount, treeNodeCount-oldTreeNodes, nodeCount-oldNodes);
	}
	printf("Exit not found.\n");
	finalize();
	return 2;
}

int main(int argc, const char* argv[]) { return run(argc, argv); }
//#include "test_body.cpp"

void dumpData(char* name)
{
	FILE* f = fopen(format("nodes-%s.bin", name), "wb");
	for (NODEI n=0; n<nodeCount; n++)
		fwrite(getNode(n), sizeof Node, 1, f);
	fclose(f);
	f = fopen(format("treenodes-%s.bin", name), "wb");
	for (TREEI n=0; n<treeNodeCount; n++)
		fwrite(getTreeNode(n), sizeof TreeNode, 1, f);
	fclose(f);
}
