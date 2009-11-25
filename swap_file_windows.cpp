// Windows files

HANDLE archive = NULL;

INLINE void cacheArchive(CACHEI c)
{
	if (archive == NULL)
	{
		archive = CreateFile(format("nodes-%d.bin", LEVEL), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, NULL);
		if (archive == INVALID_HANDLE_VALUE)
			error("Swap file creation failure");
	}
	NODEI index = cache[c].index;
	LARGE_INTEGER p;
	p.QuadPart = (uint64_t)index * sizeof(Node);
#ifdef MULTITHREADING
	OVERLAPPED o;
	o.Offset = p.LowPart;
	o.OffsetHigh = p.HighPart;
	o.hEvent = 0;
	WriteFile(archive, &cache[c].data, sizeof(Node), NULL, &o);
#else
	SetFilePointerEx(archive, p, NULL, FILE_BEGIN);
	DWORD bytes;
	WriteFile(archive, &cache[c].data, sizeof(Node), &bytes, NULL);
#endif
}

INLINE void cacheUnarchive(CACHEI c)
{
	assert(archive);
	NODEI index = cache[c].index;
	LARGE_INTEGER p;
	p.QuadPart = (uint64_t)index * sizeof(Node);
#ifdef MULTITHREADING
	OVERLAPPED o;
	o.Offset = p.LowPart;
	o.OffsetHigh = p.HighPart;
	o.hEvent = 0;
	ReadFile(archive, &cache[c].data, sizeof(Node), NULL, &o);
#else
	SetFilePointerEx(archive, p, NULL, FILE_BEGIN);
	DWORD bytes;
	ReadFile(archive, &cache[c].data, sizeof(Node), &bytes, NULL);
#endif
}
