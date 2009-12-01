#define QUEUE_BUF_SIZE (64*1024)

struct Queue
{
	FILE* f;
	NODEI buf[QUEUE_BUF_SIZE];
	int pos;
};

Queue* queue[MAX_FRAMES];
#ifdef MULTITHREADING
MUTEX queueMutex[MAX_FRAMES];
#endif

void queueFlush(Queue* q, FRAME frame)
{
	if (q->f==NULL)
	{
		FILE* f = fopen(format("queue-%d-%d.bin", LEVEL, frame), "wb");
		if (f==NULL)
			error("Queue file error");
		q->f = f;
	}
	fwrite(&q->buf, sizeof(NODEI), q->pos, q->f);
	q->pos = 0;
}

void queueBuffer(Queue* q)
{
	int read = fread(&q->buf, sizeof(NODEI), QUEUE_BUF_SIZE, q->f);
	if (read < QUEUE_BUF_SIZE)
		memset(&q->buf[read], 0, sizeof(NODEI) * (QUEUE_BUF_SIZE-read));
	q->pos = 0;
};

void queueNode(NODEI node, FRAME frame)
{
	if (frame >= MAX_FRAMES)
		return;
#ifdef MULTITHREADING
	SCOPED_LOCK lock(queueMutex[frame]);
#endif
	Queue* q = queue[frame];
	if (q == NULL)
	{
		q = queue[frame] = new Queue;
		memset(q, 0, sizeof(Queue));
	}
	q->buf[q->pos++] = node;
	if (q->pos == QUEUE_BUF_SIZE)
		queueFlush(q, frame);
}

NODEI dequeueNode(FRAME frame)
{
#ifdef MULTITHREADING
	SCOPED_LOCK lock(queueMutex[frame]);
#endif
	Queue* q = queue[frame];
	//if (q->f == NULL)
	//	return 0;
	if (q->pos==QUEUE_BUF_SIZE)
		queueBuffer(q);
	return q->buf[q->pos++];
}

void queueDestroy(FRAME frame)
{
	if (queue[frame]->f)
		fclose(queue[frame]->f);
	delete queue[frame];
	remove(format("queue-%d-%d.bin", LEVEL, frame));
}

NODEI queueRewind(FRAME frame)
{
	Queue* q = queue[frame];
	if (q==NULL)
		return 0;
	else
	if (!q->f) // no file? just terminate + rewind buffer
	{
		q->buf[q->pos] = 0;
		NODEI length = q->pos;
		q->pos = 0;
		return length;
	}
	else
	{
		if (q->pos)
			queueFlush(queue[frame], frame);
		NODEI length = ftell(q->f) / sizeof(NODEI);
		q->f = freopen(format("queue-%d-%d.bin", LEVEL, frame), "rb", q->f);
		q->pos = QUEUE_BUF_SIZE; // buffer on first read
		return length;
	}
}
