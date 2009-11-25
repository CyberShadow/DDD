// Linked lists with heads in a hashtable, backed by a flat array. Memory mapped files (archive) are used for swap.

struct CacheNode
{
	Node data;
	bool dirty;
	char allocState;
	NODEI index;
	CACHEI next;
};

//CacheNode cache[CACHE_SIZE];
//CacheNode *cache = new CacheNode[CACHE_SIZE];
CacheNode* cache = (CacheNode*)calloc(CACHE_SIZE, sizeof(CacheNode));
CACHEI cacheFreeSearchPtr=1, cacheFreeFirstPtr=0, cacheSize=1;

void cacheTrim();

INLINE CACHEI cacheAlloc()
{
	if (cacheSize == CACHE_SIZE)
	{
#ifdef MULTITHREADING
		error("Cache overflow");
#else
		cacheTrim();
#endif
	}
	assert(cacheSize < CACHE_SIZE, "Trim failed");
	cacheSize++;
	if (cacheFreeFirstPtr)
	{
		CACHEI n = cacheFreeFirstPtr;
		assert(cache[n].allocState == 2);
		cacheFreeFirstPtr = cache[n].next;
		cache[n].allocState = 1;
		return n;
	}
	else
	{
		do
			cacheFreeSearchPtr = cacheFreeSearchPtr==(CACHE_SIZE-1) ? 1 : cacheFreeSearchPtr+1;
		//while (KEY_TO_INDEX(cache[cacheFreePtr].key));
		while (cache[cacheFreeSearchPtr].allocState);
		cache[cacheFreeSearchPtr].allocState = 1;
		return cacheFreeSearchPtr;
	}
}

// ******************************************************************************************************

//#define CACHE_HASHSIZE 24
//#define CACHE_HASHSIZE 12
//#define CACHE_LOOKUPSIZE (1<<CACHE_HASHSIZE)
#define CACHE_LOOKUPSIZE (CACHE_SIZE>>2)
//#define CACHE_LOOKUPSIZE 0x1000000
typedef uint32_t CACHEHASH;
CACHEI cacheLookup[CACHE_LOOKUPSIZE];
#ifdef MULTITHREADING
boost::mutex cacheMutex;
#endif

INLINE CACHEHASH cacheHash(NODEI n)
{
	return n % CACHE_LOOKUPSIZE;
}

CACHEI cacheNew(NODEI index)
{
    CACHEI c = cacheAlloc();
    cache[c].index = index;

    CACHEHASH h = cacheHash(index);
	cache[c].next = cacheLookup[h];
	//printf("Allocated %d for %d -> h=%d -> %d\n", c, index, h, cache[c].next);
	cacheLookup[h] = c;
	return c;
}

const int cacheTrimThreshold = (CACHE_SIZE / CACHE_LOOKUPSIZE / 2) - 1;

void cacheTrim()
{
	printf("*");
	for (CACHEHASH h=0; h<CACHE_LOOKUPSIZE; h++)
	{
		CACHEI c = cacheLookup[h];
		unsigned n = 0;
		while (c)
		{
			//printf("%d. %d -> c=%d n=%d next=%d\n", n, h, c, cache[c].index, cache[c].next);
			assert(cache[c].allocState == 1);
			CACHEI next = cache[c].next;
			if (n > cacheTrimThreshold)
			{
				if (cache[c].dirty)
				{
					cacheArchive(c);
					onCacheWrite();
				}
				cache[c].allocState = 2;
				cache[c].next = cacheFreeFirstPtr;
				cacheFreeFirstPtr = c;
				cacheSize--;
			}
			else
			if (n == cacheTrimThreshold)
				cache[c].next = 0;
			c = next;
			n++;
		}
	}
}

INLINE void markDirty(Node* np)
{
	((CacheNode*)np)->dirty = true;
}

// ******************************************************************************************************

Node* newNode(NODEI* index)
{
	/* LOCK */
	CACHEI c;
	{
#ifdef MULTITHREADING
		boost::mutex::scoped_lock lock(cacheMutex);
#endif
		*index = nodeCount;
		c = cacheNew(nodeCount);
		cache[c].dirty = true;
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
    CACHEHASH hash = cacheHash(index);

    /* LOCK */
    CACHEI c;
    {
#ifdef MULTITHREADING
		boost::mutex::scoped_lock lock(cacheMutex);
#endif
		CACHEI first = cacheLookup[hash];
		c = first;
		CACHEI prev = 0;
		while (c)
		{
			if (cache[c].index == index)
			{
				// pop node to front of hash list
				if (prev)
				{
					//assert(0, "Hash collision");
					cache[prev].next = cache[c].next;
					cache[c].next = first;
					cacheLookup[hash] = c;
				}
				onCacheHit();
				return &cache[c].data;
			}
			prev = c;
			c = cache[c].next;
		}
		
		// unarchive
		c = cacheNew(index);
		cache[c].dirty = false;
		cache[c].next = first;
		cacheLookup[hash] = c;
		onCacheMiss();
	}
	
	cacheUnarchive(c);
	return &cache[c].data;
}

Node* getNodeFast(NODEI index)
{
    CACHEHASH hash = cacheHash(index);

	CACHEI first = cacheLookup[hash];
	CACHEI c = first;
	CACHEI prev = 0;
	while (c)
	{
		if (cache[c].index == index)
			return &cache[c].data;
		prev = c;
		c = cache[c].next;
	}
	
	// unarchive
	c = cacheNew(index);
	cache[c].dirty = false;
	cache[c].next = first;
	cacheLookup[hash] = c;
	cacheUnarchive(c);
	return &cache[c].data;
}

INLINE Node* refreshNode(NODEI index, Node* old)
{
	if (((CacheNode*)old)->index == index && ((CacheNode*)old)->allocState == 1)
		return old;
	else
		return getNode(index);
}

#ifndef MULTITHREADING
#define THREADS 1
#endif
#define CACHE_TRIM_THRESHOLD (CACHE_SIZE-(X*Y*2*THREADS))

void postNode()
{
	if (cacheSize >= CACHE_TRIM_THRESHOLD)
	{
#ifdef MULTITHREADING
		assert(threadsRunning > 0);
		static boost::barrier* barrier;
		/* LOCK */
		{
			boost::mutex::scoped_lock lock(cacheMutex);
			if (barrier == NULL)
				barrier = new boost::barrier(threadsRunning);
		}
		barrier->wait();
		/* LOCK */
		{
			boost::mutex::scoped_lock lock(cacheMutex);
			if (cacheSize >= CACHE_TRIM_THRESHOLD)
			{
				cacheTrim();
				assert(cacheSize < CACHE_TRIM_THRESHOLD, "Trim failed");
			}
			else
			{
				// another thread took care of it
			}
		}
		delete barrier;
		barrier = NULL;
#else
		cacheTrim();
		assert(cacheSize < CACHE_TRIM_THRESHOLD, "Trim failed");
#endif
	}
}
