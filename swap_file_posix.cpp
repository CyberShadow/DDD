// POSIX files

#ifdef MULTITHREADING
boost::mutex swapMutex;
#endif

int archive = 0;

INLINE void cacheArchive(CACHEI c)
{
	NODEI index = cache[c].index;
#ifdef MULTITHREADING
	boost::mutex::scoped_lock lock(swapMutex);
#endif
	if (archive == 0)
	{
		archive = _open(format("nodes-%d.bin", LEVEL), _O_BINARY | _O_RDWR | _O_TRUNC | _O_CREAT, _S_IREAD | _S_IWRITE);
		if (archive == -1)
			error(_strerror(NULL));
	}
	_lseeki64(archive, (uint64_t)index * sizeof(Node), SEEK_SET);
	_write(archive, &cache[c].data, sizeof(Node));
}

INLINE void cacheUnarchive(CACHEI c)
{
	assert(archive);
	NODEI index = cache[c].index;
#ifdef MULTITHREADING
	boost::mutex::scoped_lock lock(swapMutex);
#endif
	_lseeki64(archive, (uint64_t)index * sizeof(Node), SEEK_SET);
	_read(archive, &cache[c].data, sizeof(Node));
}
