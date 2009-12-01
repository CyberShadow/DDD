// Simple Dijkstra BFS.

#if defined(QUEUE_STL)
#include "queue_stl.cpp"
#elif defined(QUEUE_FILE)
#include "queue_file.cpp"
#else
#error Queue plugin not set
#endif

// ******************************************************************************************************

void processNodeChildren(NODEI n, FRAME frame, const State* state);

// ******************************************************************************************************

#ifdef MULTITHREADING
#define NODE_PARTITIONS 0x100000
MUTEX nodeMutex[NODE_PARTITIONS];
#endif

int replayState(NODEI n, const Node* np, State* state, FRAME* frame)
{
	Step steps[MAX_STEPS+1];
	unsigned int stepNr = 0;
	const Node* cur = np;

	while (1)
	{
		/* LOCK */
		{
#ifdef MULTITHREADING
			SCOPED_LOCK lock(nodeMutex[n % NODE_PARTITIONS]);
#endif
			if ((Action)cur->step.action == NONE)
				break;
			
			steps[stepNr++] = cur->step;
			n = cur->parent;
		}

		cur = getNode(n);
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

void dumpChain(FILE* f, NODEI n)
{
	Step steps[MAX_STEPS+1];
	NODEI nodes[MAX_STEPS+1];
	unsigned int stepNr = 0;
	const Node* cur = getNode(n);

	while ((Action)cur->step.action != NONE)
	{
		steps[stepNr++] = cur->step;
		nodes[stepNr  ] = cur->parent;
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
		fprintf(f, "[%d] @%d,%d: %s\n%s", nodes[stepNr], steps[stepNr].x+1, steps[stepNr].y+1, actionNames[steps[stepNr].action], state.toString());
		totalSteps += (steps[stepNr].action<SWITCH ? 1 : 0) + replayStep(&state, &frame, steps[--stepNr]);
	}
	// last one
	fprintf(f, "[%d], @%d,%d: %s\n%s", n, steps[0].x+1, steps[0].y+1, actionNames[steps[0].action], state.toString());
	fprintf(f, "Total steps: %d", totalSteps);
}

FRAME getFrames(NODEI n)
{
	Node* np = getNode(n);
	State state;
	FRAME stateFrame;
	replayState(n, np, &state, &stateFrame);
	return stateFrame;
}

#ifdef DEBUG_VERBOSE

void log(const char* s)
{
#ifdef MULTITHREADING
	static boost::mutex m;
	SCOPED_LOCK lock(m);
#endif
	static FILE* f = NULL;
	if (f==NULL)
	{
		f = fopen("log.txt", "wt");
		assert(f != NULL);
	}
	static int counter = 0;
	fprintf(f, "%10d. %s", counter++, s);
	fflush(f);
}

int replayStateFast(const Node* n, NODEI index, State* state, FRAME* frame, const char* comment)
{
	Step steps[MAX_STEPS+1];
	unsigned int stepNr = 0;
	const Node* cur = n;

	char buf[10240];
	sprintf(buf, "%12s node %d ", comment, index);

	while ((Action)cur->step.action != NONE)
	{
		steps[stepNr++] = cur->step;
		sprintf(buf+strlen(buf), " <- %d [%s@(%d,%d)+%d]", cur->parent, actionNames[cur->step.action], cur->step.x, cur->step.y, cur->step.extraSteps);
		cur = getNodeFast(cur->parent);
		if (stepNr > MAX_STEPS)
			return stepNr;
	}
	sprintf(buf+strlen(buf), "\n");
	log(buf);
	unsigned int totalSteps = 0;
	*state = initialState;
	*frame = 0;
	while (stepNr)
		totalSteps += (steps[stepNr].action<SWITCH ? 1 : 0) + replayStep(state, frame, steps[--stepNr]);
	return totalSteps;
}

void testNode(const Node* np, NODEI index, const char* comment)
{
	State state;
	FRAME stateFrame;
	replayStateFast(np, index, &state, &stateFrame, comment);
}

#endif

void dumpNodes()
{
	for (NODEI n=1; n<nodeCount; n++)
	{
		const Node* np = getNodeFast(n);
		printf("node[%d] <- %d: @%d,%d: %s (%d)\n", n, np->parent, np->step.x+1, np->step.y+1, actionNames[np->step.action], getFrames(n));
	}
}

// ******************************************************************************************************

INLINE void reparentNode(NODEI n, NODEI parent, Node* np, FRAME frame, Step step)
{
	// this node will always be still queued, so we don't need to worry about its children
	testNode(np, n, "Reparenting");
	/* LOCK */
	{
#ifdef MULTITHREADING
		SCOPED_LOCK lock(nodeMutex[n % NODE_PARTITIONS]);
#endif
		np->step = step;
		np->parent = parent;
		markDirty(np);
	}
	queueNode(n, frame);
	testNode(np, n, "Reparented");
}

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
		SCOPED_LOCK lock(lookupMutex[hash % PARTITIONS]);
#endif
		NODEI old = lookup[hash];
		NODEI n = old;
		Node* prev = NULL;
		while (n)
		{
			State other;
			Node* np = getNode(n);
			FRAME otherFrame;
			replayState(n, np, &other, &otherFrame);
			//np = refreshNode(n, np);
			if (*state == other)
			{
				if (otherFrame > frame) // better path found? reparent and requeue
				{
					//printf("node[%2d] << %2d: @%2d,%2d: %6s (%3d)\n", n, parent, step.x+1, step.y+1, actionNames[step.action], frame);
					reparentNode(n, parent, np, frame, step);
				}
				else
				{
					//printf("node[%2d] .. %2d: @%2d,%2d: %6s (%3d)\n", n, parent, step.x+1, step.y+1, actionNames[step.action], frame);
				}
				// pop node to front of hash list
				if (prev)
				{
					//assert(0, "Hash collision");
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
		np->step = step;
		np->parent = parent;
		np->next = old;
		lookup[hash] = nn;
		//printf("node[%2d] <- %2d: @%2d,%2d: %6s (%3d)\n", nn, parent, step.x+1, step.y+1, actionNames[step.action], frame);
	}
	queueNode(nn, frame);
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
			newState = *state;
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

// ******************************************************************************************************

unsigned int currentFrame;

void processNode(NODEI n)
{
	Node* np = getNode(n);
	State state;
	FRAME stateFrame;
	testNode(np, n, "Processing");
	int steps = replayState(n, np, &state, &stateFrame);
	if (stateFrame != currentFrame)
		return; // node was reparented and requeued
	if (state.playersLeft()==0)
	{
		printf("\nExit found, writing path...\n");
		FILE* f = fopen(BOOST_PP_STRINGIZE(LEVEL) ".txt", "wt");
		dumpChain(f, n);
		fclose(f);
		//File.set(LEVEL ~ ".txt", chainToString(state));
		printf("Done.\n");
		exit(0);
	}
	else
		processNodeChildren(n, stateFrame, &state);
}

void worker()
{
	NODEI n;
	while((n=dequeueNode(currentFrame))!=0)
	{
		processNode(n);
		postNode();
	}
	onThreadExit();
}

void searchInit()
{
	// initialize state
	memset(lookup, 0, sizeof lookup);
	
	reserveNode(); // node 0 is reserved

	Step nullStep = { (unsigned)NONE };
	addNode(&initialState, 0, nullStep, 0);
}

int search()
{
	searchInit();
	
	for (currentFrame=0;currentFrame<maxFrames;currentFrame++)
	{
		if (!queue[currentFrame])
			continue;
		
		printTime();
		printf("Frame %d/%d: %d/%d nodes", currentFrame, maxFrames, queue[currentFrame]->size(), nodeCount-1); fflush(stdout);
		NODEI oldNodes = nodeCount;
		
#ifdef MULTITHREADING
		THREAD threads[THREADS];
		threadsRunning = THREADS;
		for (int i=0; i<THREADS; i++)
			threads[i] = THREAD_CREATE(&worker);
		for (int i=0; i<THREADS; i++)
		{
			THREAD_JOIN(threads[i]);
			THREAD_DESTROY(threads[i]);
		}
#else
		worker();
#endif
		assert(queue[currentFrame]==NULL);
		//assert(queueCount[currentFrame]-queuePos[currentFrame]==0);

		printf(", %d new\n", nodeCount-oldNodes);
#ifdef DEBUG
		cacheTest();
#endif
		printCacheStatsDelta();
	}
	printf("Exit not found.\n");
	//dumpCache(); dumpNodes();
	return 2;
}

