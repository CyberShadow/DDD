Node* nodes[0x10000];
#ifdef MULTITHREADING
boost::mutex nodeMutex;
#endif

Node* newNode(NODEI* index)
{
	Node* result;
	/* LOCK */
	{
#ifdef MULTITHREADING
		boost::mutex::scoped_lock lock(nodeMutex);
#endif
		*index = nodeCount;
		if ((nodeCount&0xFFFF) == 0)
			nodes[nodeCount/0x10000] = new Node[0x10000];
		result = nodes[nodeCount/0x10000] + (nodeCount&0xFFFF);
		nodeCount++;
		if (nodeCount == MAX_NODES)
			error("Too many nodes");
	}
	result->next = 0;
	return result;
}

void reserveNode() { NODEI dummy; newNode(&dummy); }

INLINE Node* getNode(NODEI index)
{
	return nodes[index/0x10000] + (index&0xFFFF);
}

INLINE void postNode() {}
INLINE void markDirty(Node* np) {}
INLINE const Node* getNodeFast(NODEI index) { return getNode(index); }
