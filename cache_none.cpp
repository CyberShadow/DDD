#define NODE_CLUSTER_SIZE 0x100000
#define NODE_CLUSTERS ((MAX_NODES+1) / NODE_CLUSTER_SIZE)

Node* nodes[NODE_CLUSTERS];
#ifdef MULTITHREADING
boost::mutex cacheMutex;
#endif

Node* newNode(NODEI* index)
{
	Node* result;
	/* LOCK */
	{
#ifdef MULTITHREADING
		boost::mutex::scoped_lock lock(cacheMutex);
#endif
		*index = nodeCount;
		if ((nodeCount%NODE_CLUSTER_SIZE) == 0)
			nodes[nodeCount/NODE_CLUSTER_SIZE] = new Node[NODE_CLUSTER_SIZE];
		result = nodes[nodeCount/NODE_CLUSTER_SIZE] + (nodeCount%NODE_CLUSTER_SIZE);
		nodeCount++;
		//if ((nodeCount & 0x3) == 0) Sleep(1);
		if (nodeCount == MAX_NODES)
			error("Too many nodes");
	}
	result->next = 0;
	return result;
}

void reserveNode() { NODEI dummy; newNode(&dummy); }

INLINE Node* getNode(NODEI index)
{
	assert(index, "Trying to get null node");
	assert(index < nodeCount, "Trying to get invalid node")
	return nodes[index/NODE_CLUSTER_SIZE] + (index%NODE_CLUSTER_SIZE);
}

INLINE Node* refreshNode(NODEI index, Node* old) { return old; }

INLINE void postNode() {}
INLINE void markDirty(Node* np) {}
INLINE const Node* getNodeFast(NODEI index) { return getNode(index); }

void cacheTest() 
{
	for (NODEI n = 1; n < nodeCount; n++)
		testNode(getNodeFast(n), n, "Testing");
}
