// Boost memory-mapped files

#define ARCHIVE_CLUSTER_SIZE 0x4000000
#define ARCHIVE_CLUSTERS ((MAX_NODES + (ARCHIVE_CLUSTER_SIZE-1)) / ARCHIVE_CLUSTER_SIZE)
Node* archive[ARCHIVE_CLUSTERS];

INLINE void cacheArchive(NODEI index, const Node* data)
{
	NODEI cindex = index / ARCHIVE_CLUSTER_SIZE;
	assert(cindex < ARCHIVE_CLUSTERS);
	if (archive[cindex]==NULL)
	{
		boost::iostreams::mapped_file_params params;
		params.path = format("nodes-%d-%d.bin", LEVEL, cindex);
		params.mode = std::ios_base::in | std::ios_base::out;
		params.length = params.new_file_size = ARCHIVE_CLUSTER_SIZE * sizeof(Node);
		boost::iostreams::mapped_file* m = new boost::iostreams::mapped_file(params);
		archive[cindex] = (Node*)m->data();
	}
	archive[cindex][index % ARCHIVE_CLUSTER_SIZE] = *data;
}

INLINE void cacheUnarchive(NODEI index, Node* data)
{
	*data = archive[index / ARCHIVE_CLUSTER_SIZE][index % ARCHIVE_CLUSTER_SIZE];
}
