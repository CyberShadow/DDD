#ifdef SPLAY
#include "nodes_swap_splay.cpp"
#else
#include "nodes_swap_hash.cpp"
#endif

// ******************************************************************************************************

#define ARCHIVE_CLUSTER_SIZE 0x4000000
#define ARCHIVE_CLUSTERS ((MAX_NODES + (ARCHIVE_CLUSTER_SIZE-1)) / ARCHIVE_CLUSTER_SIZE)
Node* archive[ARCHIVE_CLUSTERS];

//#define ARCHIVE_STATS

#ifdef ARCHIVE_STATS
unsigned long archived, unarchived;
#endif

INLINE void cacheArchive(CACHEI c)
{
#ifdef ARCHIVE_STATS
	archived++;
#endif
	NODEI index = cache[c].index;
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
	archive[cindex][index % ARCHIVE_CLUSTER_SIZE] = cache[c].data;
}

INLINE void cacheUnarchive(CACHEI c)
{
#ifdef ARCHIVE_STATS
	unarchived++;
#endif
	NODEI index = cache[c].index;
	cache[c].data = archive[index / ARCHIVE_CLUSTER_SIZE][index % ARCHIVE_CLUSTER_SIZE];
}
