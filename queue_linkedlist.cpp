struct QueueNode
{
	NODEI node;
	QueueNode* next;
};

QueueNode* queue[MAX_FRAMES];
NODEI queueCount[MAX_FRAMES];
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
	QueueNode* q = new QueueNode;
	q->node = node;
	q->next = queue[frame];
	queue[frame] = q;
	queueCount[frame]++;
}

NODEI dequeueNode(FRAME frame)
{
	QueueNode* q;
	/* LOCK */
	{
#ifdef MULTITHREADING
		SCOPED_LOCK lock(queueMutex[frame]);
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

void queueDestroy(FRAME frame) {}

NODEI queueRewind(FRAME frame)
{
	return queueCount[frame];
}
