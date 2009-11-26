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
		static bool trimPending = false;

		/* LOCK */
		{
			// (re)create barrier, set trimPending
			boost::mutex::scoped_lock lock(cacheMutex);
			if (cacheSize >= CACHE_TRIM_THRESHOLD)
			{
				if (!trimPending)
				{
					trimPending = true;
					if (barrier)
						delete barrier;
					assert(threadsRunning <= THREADS);
					barrier = new boost::barrier(threadsRunning);
				}
			}
		}

		if (trimPending)
		{
			assert(barrier != NULL);
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
			trimPending = false;
		}
#else
		cacheTrim();
		assert(cacheSize < CACHE_TRIM_THRESHOLD, "Trim failed");
#endif
	}
}

INLINE void markDirty(Node* np)
{
	((CacheNode*)np)->dirty = true;
}
