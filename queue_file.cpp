FILE* queue[MAX_FRAMES];
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
	{
		FILE* f = fopen(format("queue-%d-%d.bin", LEVEL, frame), "wb");
		if (f==NULL)
			error("Queue file error");
		queue[frame] = f;
	}
	fwrite(&node, sizeof(NODEI), 1, queue[frame]);
}

NODEI dequeueNode(FRAME frame)
{
#ifdef MULTITHREADING
	SCOPED_LOCK lock(queueMutex[frame]);
#endif
	if (queue[frame]==NULL)
		return 0;
	NODEI result = 0;
	fread(&result, sizeof(NODEI), 1, queue[frame]);
	return result;
}

void queueDestroy(FRAME frame)
{
	fclose(queue[frame]);
	remove(format("queue-%d-%d.bin", LEVEL, frame));
}

NODEI queueRewind(FRAME frame)
{
	if (!queue[frame])
		return 0;
	NODEI length = ftell(queue[frame]) / sizeof(NODEI);
	queue[frame] = freopen(format("queue-%d-%d.bin", LEVEL, frame), "rb", queue[frame]);
	return length;
}
