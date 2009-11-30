// Windows files

HANDLE archive = NULL;

#ifdef MULTITHREADING
MUTEX archiveMutex;
#endif

INLINE void cacheArchive(NODEI index, const Node* data)
{
	/* LOCK */
	{
#ifdef MULTITHREADING
		SCOPED_LOCK lock(archiveMutex);
#endif
		if (archive == NULL)
		{
			archive = CreateFile(format("nodes-%d.bin", LEVEL), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, NULL);
			if (archive == INVALID_HANDLE_VALUE)
				error("Swap file creation failure");
		}
	}
	LARGE_INTEGER p;
	p.QuadPart = (uint64_t)index * sizeof(Node);
#ifdef MULTITHREADING
	OVERLAPPED o;
	o.Offset = p.LowPart;
	o.OffsetHigh = p.HighPart;
	o.hEvent = 0;
	WriteFile(archive, data, sizeof(Node), NULL, &o);
#else
	SetFilePointerEx(archive, p, NULL, FILE_BEGIN);
	DWORD bytes;
	WriteFile(archive, data, sizeof(Node), &bytes, NULL);
#endif
}

INLINE void cacheUnarchive(NODEI index, Node* data)
{
	assert(archive);
	LARGE_INTEGER p;
	p.QuadPart = (uint64_t)index * sizeof(Node);
#ifdef MULTITHREADING
	OVERLAPPED o;
	o.Offset = p.LowPart;
	o.OffsetHigh = p.HighPart;
	o.hEvent = 0;
	ReadFile(archive, data, sizeof(Node), NULL, &o);
#else
	SetFilePointerEx(archive, p, NULL, FILE_BEGIN);
	DWORD bytes;
	ReadFile(archive, data, sizeof(Node), &bytes, NULL);
#endif
}
