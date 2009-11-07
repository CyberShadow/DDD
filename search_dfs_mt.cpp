#include "search_dfs.cpp"

struct QueueNode
{
	NODEI node;
	QueueNode* next;
};

QueueNode* stack;
NODEI stackCount;
int threadsWaiting; // for new nodes
bool noMoreNodes;
#ifdef MULTITHREADING
boost::mutex stackMutex;
boost::condition stackCondition;
#endif

void queueNode(NODEI node, FRAME frame, const State* state)
{
#ifdef MULTITHREADING
	boost::mutex::scoped_lock lock(stackMutex);
#endif
	QueueNode* q = new QueueNode;
	q->node = node;
	// discard state, recalculate on dequeue
	q->next = stack;
	stack = q;
	stackCount++;
#ifdef MULTITHREADING
	stackCondition.notify_one();
#endif
}

NODEI dequeueNode()
{
	QueueNode* q;
	/* LOCK */
	{
#ifdef MULTITHREADING
		boost::mutex::scoped_lock lock(stackMutex);
		while (stack == NULL)
		{
			if (threadsRunning - threadsWaiting < 1) // don't block the last thread
			{
				noMoreNodes = true;
				stackCondition.notify_all();
				return 0;
			}
			stackCondition.wait(lock);
			if (noMoreNodes)
				return 0;
		}
#endif
		q = stack;
		if (q == NULL)
			return 0;
		stack = q->next;
		stackCount--;
	}
	NODEI result = q->node;
	delete q;
	return result;
}

void worker()
{
	threadsRunning++;
	NODEI n;
	while((n=dequeueNode())!=0)
	{
		processNode(n);
		postNode();
	}
	threadsRunning--;
}

void runSearch()
{
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
}
