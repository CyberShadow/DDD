// RAM (for testing)

#define ARCHIVE_CLUSTER_SIZE 0x10000
#define ARCHIVE_CLUSTERS ((MAX_NODES + (ARCHIVE_CLUSTER_SIZE-1)) / ARCHIVE_CLUSTER_SIZE)
Node* archive[ARCHIVE_CLUSTERS];

INLINE void cacheArchive(NODEI index, const Node* data)
{
	testNode(data, index, "Archiving");
	NODEI cindex = index / ARCHIVE_CLUSTER_SIZE;
	assert(cindex < ARCHIVE_CLUSTERS);
	if (archive[cindex]==NULL)
		archive[cindex] = new Node[ARCHIVE_CLUSTER_SIZE];
	archive[cindex][index % ARCHIVE_CLUSTER_SIZE] = *data;
}

INLINE void cacheUnarchive(NODEI index, Node* data)
{
	assert(index < nodeCount);
	*data = archive[index / ARCHIVE_CLUSTER_SIZE][index % ARCHIVE_CLUSTER_SIZE];
	testNode(data, index, "Unarchiving");
}

#define HAVE_CACHE_PEEK
INLINE const Node* cachePeek(NODEI index)
{
	assert(index < nodeCount);
	return &archive[index / ARCHIVE_CLUSTER_SIZE][index % ARCHIVE_CLUSTER_SIZE];
}
