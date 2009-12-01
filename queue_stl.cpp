#include <queue>

std::vector<NODEI>* queue[MAX_FRAMES];
#ifdef MULTITHREADING
MUTEX queueMutex[MAX_FRAMES];
#endif

void queueNode(NODEI node, FRAME frame)
{
#ifdef MULTITHREADING
	SCOPED_LOCK lock(queueMutex[frame]);
#endif
	if (frame >= MAX_FRAMES)
		return;
	if (queue[frame]==NULL)
		queue[frame] = new std::vector<NODEI>();
	queue[frame]->push_back(node);
}

NODEI dequeueNode(FRAME frame)
{
#ifdef MULTITHREADING
	SCOPED_LOCK lock(queueMutex[frame]);
#endif
	if (queue[frame]==NULL)
		return 0;
	std::vector<NODEI>* q = queue[frame];
	NODEI result = q->back();
	q->pop_back();
	if (q->empty())
	{
		delete q;
		queue[frame] = NULL;
	}
	return result;
}
