#include <vector>

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
	std::vector<NODEI>* q = queue[frame];
#ifdef MULTITHREADING
	SCOPED_LOCK lock(queueMutex[frame]);
#endif
	if (q->empty())
		return 0;
	NODEI result = q->back();
	q->pop_back();
	return result;
}

void queueDestroy(FRAME frame)
{
	delete queue[frame];
	queue[frame] = NULL;
}

NODEI queueRewind(FRAME frame)
{
	return queue[frame] ? queue[frame]->size() : 0;
}
