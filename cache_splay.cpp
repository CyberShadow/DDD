// Splay trees backed by a flat array (cache). Memory mapped files (archive) are used for swap.

struct CacheNode
{
	Node data;
	bool dirty;
	bool allocated;
	NODEI index; // tree key, 0 - cache node free
	CACHEI left, right;
};

//CacheNode cache[CACHE_SIZE];
//CacheNode *cache = new CacheNode[CACHE_SIZE];
CacheNode* cache = (CacheNode*)calloc(CACHE_SIZE, sizeof(CacheNode));

CACHEI cacheAlloc();
void dumpCache();

CACHEI cacheSplay(NODEI index, CACHEI t)
{
	CACHEI l, r, y;
	if (t == 0) return t;
	// cache[0] works as a temporary node
	cache[0].left = cache[0].right = 0;
	l = r = 0;

	/*for (;;)
	{
		CacheNode* tp = &cache[t];
		if (i < tp->index)
		{
			if (tp->left == 0) break;
			if (i < cache[tp->left].index)
			{
				y = tp->left;                           // rotate right
				CacheNode* yp = &cache[y];
				tp->left = yp->right;
				yp->right = t;
				t = y;
				if (yp->left == 0) break;
			}
			cache[r].left = t;                          // link right
			r = t;
			t = tp->left;
		}
		else
		if (i > tp->index)
		{
			if (tp->right == 0) break;
			if (i > cache[tp->right].index)
			{
				y = tp->right;                          // rotate left
				CacheNode* yp = &cache[y];
				tp->right = yp->left;
				yp->left = t;
				t = y;
				if (yp->right == 0) break;
			}
			cache[l].right = t;                         // link left
			l = t;
			t = tp->right;
		}
		else
			break;
	}
	CacheNode* tp = &cache[t];
	cache[l].right = tp->left ;                         // assemble
	cache[r].left  = tp->right;
	tp->left  = cache[0].right;
	tp->right = cache[0].left;*/

	// TODO: optimize

	for (;;) {
		if (index < cache[t].index) {
			if (cache[t].left == 0) break;
			if (index < cache[cache[t].left].index) {
				y = cache[t].left;                           /* rotate right */
				cache[t].left = cache[y].right;
				cache[y].right = t;
				t = y;
				if (cache[t].left == 0) break;
			}
			cache[r].left = t;                               /* link right */
			r = t;
			t = cache[t].left;
		} else if (index > cache[t].index) {
			if (cache[t].right == 0) break;
			if (index > cache[cache[t].right].index) {
				y = cache[t].right;                          /* rotate left */
				cache[t].right = cache[y].left;
				cache[y].left = t;
				t = y;
				if (cache[t].right == 0) break;
			}
			cache[l].right = t;                              /* link left */
			l = t;
			t = cache[t].right;
		} else {
			break;
		}
	}
	cache[l].right = cache[t].left;                                /* assemble */
	cache[r].left = cache[t].right;
	cache[t].left = cache[0].right;
	cache[t].right = cache[0].left;
	return t;
}

CACHEI cacheFind(NODEI index, CACHEI t)
{
	while (t)
	{
		if (index < cache[t].index)
			t = cache[t].left;
		else
		if (index > cache[t].index)
			t = cache[t].right;
		else
			break;
	}
	return t;
}

CACHEI cacheInsert(NODEI index, CACHEI t, bool dirty)
{
	CACHEI n = cacheAlloc();
	CacheNode* np = &cache[n];
	np->index = index;
	np->dirty = dirty;
	if (t == 0)
	{
		np->left = np->right = 0;
		return n;
	}
	t = cacheSplay(index, t);
	CacheNode* tp = &cache[t];
	if (index < tp->index)
	{
		np->left = tp->left;
		np->right = t;
		tp->left = 0;
		return n;
	}
	else 
	if (index > tp->index)
	{
		np->right = tp->right;
		np->left = t;
		tp->right = 0;
		return n;
	}
	else
		error("Inserted node already in tree");
	throw "Unreachable";
}

// ******************************************************************************************************

CACHEI cacheFreePtr=1, cacheRoot=0;
// this is technically not required, but provides an optimization (we don't need to check if a possibly-archived node is in the cache)
uint32_t cacheArchived[(MAX_NODES+31)/32];

#define MAX_CACHE_TREE_DEPTH 64
#define CACHE_TRIM 4

CACHEI cacheDepthCounts[MAX_CACHE_TREE_DEPTH];
int cacheTrimLevel;

void cacheCount(CACHEI n, int level)
{
	cacheDepthCounts[level >= MAX_CACHE_TREE_DEPTH ? MAX_CACHE_TREE_DEPTH-1 : level]++;
	CacheNode* np = &cache[n];
	CACHEI l = np->left;
	if (l) cacheCount(l, level+1);
	CACHEI r = np->right;
	if (r) cacheCount(r, level+1);
}

void cacheDoTrim(CACHEI n, int level)
{
	CacheNode* np = &cache[n];
	CACHEI l = np->left;
	if (l) cacheDoTrim(l, level+1);
	CACHEI r = np->right;
	if (r) cacheDoTrim(r, level+1);
	if (level > cacheTrimLevel)
	{
		if (np->dirty)
		{
			cacheArchive(cache[n].index, &cache[n].data);
			onCacheWrite();
		}
		NODEI index = np->index;
		assert((cacheArchived[index/32] & (1<<(index%32))) == 0, "Attempting to re-archive node");
		cacheArchived[index/32] |= 1<<(index%32);
		//np->index = 0;
		np->allocated = false;
		cacheSize--;
	}
	else
	if (level == cacheTrimLevel)
		np->left = np->right = 0;
}

void cacheTrim()
{
	onCacheTrim();
	for (int i=0; i<MAX_CACHE_TREE_DEPTH; i++)
		cacheDepthCounts[i] = 0;
	cacheCount(cacheRoot, 0);
#ifdef DEBUG
	CACHEI total = 1;
	for (int i=0; i<MAX_CACHE_TREE_DEPTH; i++)
		total += cacheDepthCounts[i];
	assert(total == cacheSize);
#endif
	CACHEI nodes = 0;
	const int threshold = CACHE_SIZE / CACHE_TRIM;
	int level;
	for (level=MAX_CACHE_TREE_DEPTH-1; level>=0; level--)
	{
		nodes += cacheDepthCounts[level];
		if (nodes > threshold)
			break;
	}
	assert(level>0);
	cacheTrimLevel = level-1;
	cacheDoTrim(cacheRoot, 0);
}

CACHEI cacheAlloc()
{
	if (cacheSize == CACHE_SIZE)
		error("Cache overflow"); // How could we have let this HAPPEN?!?
	do
		cacheFreePtr = cacheFreePtr==(CACHE_SIZE-1) ? 1 : cacheFreePtr+1;
	//while (cache[cacheFreePtr].index);
	while (cache[cacheFreePtr].allocated);
	cache[cacheFreePtr].allocated = true;
	cacheSize++;
	return cacheFreePtr;
}

// ******************************************************************************************************

Node* newNode(NODEI* index)
{
	CACHEI c;
	/* LOCK */
	{
#ifdef MULTITHREADING
		SCOPED_LOCK lock(cacheMutex);
#endif
		*index = nodeCount;
		c = cacheRoot = cacheInsert(nodeCount, cacheRoot, true);
		nodeCount++;
		if (nodeCount == MAX_NODES)
			error("Too many nodes");
	}
	Node* result = &cache[c].data;
	result->next = 0;
	return result;
}

void reserveNode() { nodeCount++; }

Node* getNode(NODEI index)
{
	assert(index, "Trying to get node 0");
	assert(index < nodeCount, "Trying to get inexistent node");

	NODEI archiveIndex = index/32;
	uint32_t archiveMask = 1<<(index%32);

	bool archived;
	CACHEI c;

	/* LOCK */
	{
#ifdef MULTITHREADING
		SCOPED_LOCK lock(cacheMutex);
#endif
		uint32_t a = cacheArchived[archiveIndex];
		archived = (a & archiveMask) != 0;
		if (archived)
		{
			a &= ~archiveMask;
			cacheArchived[archiveIndex] = a;
			c = cacheRoot = cacheInsert(index, cacheRoot, false);
		}
		else
		{
			c = cacheRoot = cacheSplay(index, cacheRoot);
			assert(cache[c].index == index, "Splayed wrong node"); 
			onCacheHit();
		}
	}
	if (archived)
	{
		cacheUnarchive(cache[c].index, &cache[c].data);
		onCacheMiss();
	}

	Node* result = &cache[c].data;
	return result;
}

INLINE const Node* getNodeFast(NODEI index)
{
	assert(index, "Trying to get node 0");
	assert(index < nodeCount, "Trying to get inexistent node");

	NODEI archiveIndex = index/32;
	uint32_t archiveMask = 1<<(index%32);

#ifdef MULTITHREADING
	SCOPED_LOCK lock(cacheMutex);
#endif
	if ((cacheArchived[archiveIndex] & archiveMask) != 0)
	{
		return cachePeek(index);
	}
	else
	{
		CACHEI c = cacheFind(index, cacheRoot);
		assert(cache[c].index == index, "Found wrong node"); 
		return &cache[c].data;
	}
}

INLINE Node* refreshNode(NODEI index, Node* old)
{
	if (((CacheNode*)old)->index == index && ((CacheNode*)old)->allocated)
		return old;
	else
		return getNode(index);
}

/*
#define CACHE_DUMP_DEPTH 25
std::string cacheTreeLines[CACHE_DUMP_DEPTH];

int dumpCacheSubtree(CACHEI t, int depth, int x)
{
	if (t==0 || depth>=CACHE_DUMP_DEPTH) return 0;
	std::ostringstream ss; 
	ss << cache[t].index;
	std::string s = ss.str();
	int myLength = s.length();
	int left = dumpCacheSubtree(cache[t].left, depth+1, x);
	cacheTreeLines[depth].resize(x + left, ' ');
	cacheTreeLines[depth] += s;
	int right = dumpCacheSubtree(cache[t].right, depth+1, x + left + myLength);
	return left + myLength + right;
}

void dumpCacheTree()
{
	for (int i=0; i<CACHE_DUMP_DEPTH; i++)
		cacheTreeLines[i] = std::string();
	dumpCacheSubtree(cacheRoot, 0, 0);
	for (int i=0; i<CACHE_DUMP_DEPTH; i++)
		printf("%s\n", cacheTreeLines[i].c_str());
}

void dumpCache()
{
	dumpCacheTree();
	printf("root=%d\n", cacheRoot);
	for (CACHEI i=1; i<CACHE_SIZE; i++)
		if (cache[i].index)
			printf("%ccache[%d]=%d -> (%d,%d)\n", cache[i].dirty ? '*' : ' ', i, cache[i].index, cache[i].left, cache[i].right);
	printf("---\n");
}
*/

// ******************************************************************************************************

bool* nodePresent;

void cacheTestNode(CACHEI c)
{
	assert(cache[c].allocated);
	NODEI n = cache[c].index;
	assert(n);
	assert(n < nodeCount);
	assert(!nodePresent[n]);
	testNode(getNodeFast(n), n, "Testing");
	nodePresent[n] = true;
	if (cache[c].left)
		cacheTestNode(cache[c].left);
	if (cache[c].right)
		cacheTestNode(cache[c].right);
}

void cacheTest()
{
	nodePresent = new bool[nodeCount];
	memset(nodePresent, 0, nodeCount);
	cacheTestNode(cacheRoot);
	for (NODEI n = 1; n < nodeCount; n++)
		if ((cacheArchived[n/32] & (1<<(n%32))) == 0)
		{
			assert(nodePresent[n]);
		}
		else
		{
			assert(!nodePresent[n]);
		}
	delete[] nodePresent;
}
