struct QueueNode
{
	NODEI node;
	QueueNode* next;
};

QueueNode* queue[MAX_FRAMES];
NODEI queueCount[MAX_FRAMES];
#ifdef MULTITHREADING
boost::mutex queueMutex[MAX_FRAMES];
#endif

void queueNode(NODEI node, FRAME frame, const State* state)
{
#ifdef MULTITHREADING
	boost::mutex::scoped_lock lock(queueMutex[frame]);
#endif
	if (frame >= MAX_FRAMES)
		return;
	QueueNode* q = new QueueNode;
	q->node = node;
	q->next = queue[frame];
	// discard state, recalculate on dequeue
	queue[frame] = q;
	queueCount[frame]++;
}

NODEI dequeueNode(FRAME frame)
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

unsigned int currentFrame;

void processNode(NODEI n)
{
	Node* np = getNode(n);
	State state;
	FRAME stateFrame;
	int steps = replayState(np, &state, &stateFrame);
	if (stateFrame != currentFrame)
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
		processNodeChildren(n, stateFrame, &state);
}

INLINE void reparentNode(NODEI n, NODEI parent, Node* np, FRAME frame, const State* state, Step step)
{
	// this node will always be still queued, so we don't need to worry about its children
	np->step = step;
	np->parent = parent;
	markDirty(np);
	queueNode(n, frame, state);
}

void searchInit() {}

void worker()
{
	threadsRunning++;
	NODEI n;
	while((n=dequeueNode(currentFrame))!=0)
	{
		processNode(n);
		postNode();
	}
	threadsRunning--;
}

int search()
{
	for (currentFrame=0;currentFrame<maxFrames;currentFrame++)
	{
		if (!queue[currentFrame])
			continue;
		
		time_t t;
		time(&t);
		char* tstr = ctime(&t);
		tstr[strlen(tstr)-1] = 0;
		printf("[%s] Frame %d/%d: %d/%d nodes", tstr, currentFrame, maxFrames, queueCount[currentFrame], nodeCount-1); fflush(stdout);
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
		assert(queueCount[currentFrame]==0);

		printf(", %d new\n", nodeCount-oldNodes);
	}
	printf("Exit not found.\n");
	//dumpCache(); dumpNodes();
	return 2;
}
