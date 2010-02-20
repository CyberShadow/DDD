// Windows files

void windowsError(const char* where = NULL)
{
	LPVOID lpMsgBuf;
	FormatMessage( 
		FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		FORMAT_MESSAGE_FROM_SYSTEM | 
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
		(LPTSTR) &lpMsgBuf,
		0,
		NULL 
	);
	if (where)
		error(format("%s: %s", where, lpMsgBuf));
	else
		error((LPCSTR)lpMsgBuf);
}

template<class NODE>
class Stream
{
protected:
	HANDLE archive;

public:
	Stream() : archive(0) {}

	bool isOpen() const { return archive != 0; }

	uint64_t size()
	{
		ULARGE_INTEGER li;
		li.LowPart = GetFileSize(archive, &li.HighPart);
		assert(li.QuadPart % sizeof(NODE) == 0, "Unaligned EOF");
		return li.QuadPart / sizeof(NODE);
	}

	uint64_t position()
	{
		LARGE_INTEGER n;
		DWORD error;
		n.QuadPart = 0;
		n.LowPart = SetFilePointer(archive, n.LowPart, &n.HighPart, FILE_CURRENT);
		if (n.LowPart == INVALID_SET_FILE_POINTER && (error=GetLastError()) != NO_ERROR)
			windowsError("Seek error");
		return n.QuadPart / sizeof(NODE);
	}

	void seek(uint64_t pos)
	{
		LARGE_INTEGER li;
		DWORD error;
		li.QuadPart = pos * sizeof(NODE);
		li.LowPart = SetFilePointer(archive, li.LowPart, &li.HighPart, FILE_BEGIN);
		if (li.LowPart == INVALID_SET_FILE_POINTER && (error=GetLastError()) != NO_ERROR)
			windowsError("Seek error");
	}

	void close()
	{
		if (archive)
		{
			CloseHandle(archive);
			archive = 0;
		}
	}

	~Stream()
	{
		close();
	}
};

#ifdef USE_UNBUFFERED_DISK_IO

template<class NODE>
class OutputStream : virtual public Stream<NODE>
{
private:
	BYTE sectorBuffer[512]; // currently assumes 512 byte sectors; maybe use GetDiskFreeSpace() in the future for increased portability
	WORD sectorBufferUse;
	WORD sectorBufferFlushed;
	char filenameOpened[MAX_PATH];

public:
	OutputStream() : sectorBufferUse(0), sectorBufferFlushed(0) {}

	OutputStream(const char* filename, bool resume=false) : sectorBufferUse(0), sectorBufferFlushed(0)
	{
		open(filename, resume);
	}

	~OutputStream()
	{
		close();
	}

	void close()
	{
		flush(false);
	}

	void flush()
	{
		flush(true);
	}

	__declspec(noinline)
	void seek(uint64_t pos)
	{
		if (sectorBufferFlushed < sectorBufferUse)
		{
			DWORD w;
			BOOL b = WriteFile(archive, sectorBuffer, sizeof(sectorBuffer), &w, NULL);
			if (!b)
				windowsError("Write error");
			if (w != sizeof(sectorBuffer))
				windowsError("Out of disk space?");
			sectorBufferUse = 0;
			sectorBufferFlushed = 0;
		}

		pos *= sizeof(NODE);
		
		LARGE_INTEGER li;
		DWORD error;
		li.QuadPart = pos & -(int64_t)sizeof(sectorBuffer);
		li.LowPart = SetFilePointer(archive, li.LowPart, &li.HighPart, FILE_BEGIN);
		if (li.LowPart == INVALID_SET_FILE_POINTER && (error=GetLastError()) != NO_ERROR)
			windowsError(format("Seek error (%s)", filenameOpened));

		sectorBufferUse = (WORD)pos % (WORD)sizeof(sectorBuffer);

		if (sectorBufferUse)
		{
			memset(sectorBuffer, 0, sizeof(sectorBuffer));
			DWORD r;
			BOOL b = ReadFile(archive, sectorBuffer, sizeof(sectorBuffer), &r, NULL);
			if (b && r!=sectorBufferUse)
				windowsError(format("Read alignment error in write alignment (%s)", filenameOpened));
			if (!b || r==0)
				windowsError(format("Read error in write alignment (%s)", filenameOpened));

			li.QuadPart = pos & -(int64_t)sizeof(sectorBuffer);
			li.LowPart = SetFilePointer(archive, li.LowPart, &li.HighPart, FILE_BEGIN);
			if (li.LowPart == INVALID_SET_FILE_POINTER && (error=GetLastError()) != NO_ERROR)
				windowsError(format("Seek error after write alignment read (%s)", filenameOpened));
		}
	}

	__declspec(noinline)
	void open(const char* filename, bool resume=false)
	{
		assert(archive==0);
		sectorBufferUse = 0;
		sectorBufferFlushed = 0;
		strcpy(filenameOpened, filename);
		archive = CreateFile(filename, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, resume ? OPEN_EXISTING : CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_NO_BUFFERING, NULL);
		if (archive == INVALID_HANDLE_VALUE)
			windowsError(format("File creation failure (%s)", filename));
		if (resume)
			seek(size());
	}

	__declspec(noinline)
	void write(const NODE* p, size_t n)
	{
		assert(archive, "File not open");
		size_t total = n * sizeof(NODE);
		size_t bytes = 0;
		const BYTE* data = (const BYTE*)p;
		while (bytes < total)
		{
			size_t left = total-bytes;
			DWORD chunk = left > DISK_IO_CHUNK_SIZE ? DISK_IO_CHUNK_SIZE : (DWORD)left;
			DWORD w;
			BOOL b;
			if (sectorBufferUse)
			{
				if (chunk > (WORD)sizeof(sectorBuffer) - (WORD)sectorBufferUse)
					chunk = (WORD)sizeof(sectorBuffer) - (WORD)sectorBufferUse;
				memcpy(sectorBuffer + sectorBufferUse, data + bytes, chunk);
				sectorBufferUse += (WORD)chunk;
				if (sectorBufferUse == sizeof(sectorBuffer))
				{
					b = WriteFile(archive, sectorBuffer, sizeof(sectorBuffer), &w, NULL);
					if (!b)
						windowsError("Write error");
					if (w != sizeof(sectorBuffer))
						windowsError("Out of disk space?");
					sectorBufferUse = 0;
					sectorBufferFlushed = 0;
				}
				bytes += chunk;
				continue;
			}
			if (chunk % sizeof(sectorBuffer) != 0)
			{
				if (chunk > sizeof(sectorBuffer))
					chunk &= -(int)sizeof(sectorBuffer);
				else
				{
					memset(sectorBuffer, 0, sizeof(sectorBuffer));
					memcpy(sectorBuffer, data + bytes, chunk);
					sectorBufferUse = (WORD)chunk;
					return;
				}
			}
			b = WriteFile(archive, data + bytes, chunk, &w, NULL);
			if (!b)
				windowsError("Write error");
			if (w == 0)
				windowsError("Out of disk space?");
			bytes += w;
		}
	}

private:
	__declspec(noinline)
	void flush(bool reopen)
	{
		if (!archive)
			return;

		if (sectorBufferFlushed == sectorBufferUse) // nothing to flush?
		{
			if (reopen)
				FlushFileBuffers(archive);
			else
			{
				CloseHandle(archive);
				archive = 0;
			}
			return;
		}
		assert(sectorBufferFlushed < sectorBufferUse);

		BOOL b;
		DWORD w;
		ULARGE_INTEGER size;
		size.LowPart = GetFileSize(archive, &size.HighPart);
		size.QuadPart += sectorBufferUse;

		b = WriteFile(archive, sectorBuffer, sizeof(sectorBuffer), &w, NULL);
		if (!b)
			windowsError("Write error");
		if (w != sizeof(sectorBuffer))
			windowsError("Out of disk space?");
		CloseHandle(archive);

		archive = CreateFile(filenameOpened, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_WRITE_THROUGH, NULL);
		if (archive == INVALID_HANDLE_VALUE)
			windowsError(format("File creation failure upon reopening (%s) buffered", filenameOpened));
		
		ULARGE_INTEGER seekPos = size;
		DWORD seek_error;
		seekPos.LowPart = SetFilePointer(archive, seekPos.LowPart, (LONG*)&seekPos.HighPart, FILE_BEGIN);
		if (seekPos.LowPart == INVALID_SET_FILE_POINTER && (seek_error=GetLastError()) != NO_ERROR)
			windowsError("SetFilePointer() #1 error");
		if (seekPos.QuadPart != size.QuadPart)
			error("Error setting file size");
		b = SetEndOfFile(archive);
		if (!b)
			windowsError("SetEndOfFile() error");
		CloseHandle(archive);
		sectorBufferFlushed = sectorBufferUse;
		if (!reopen)
		{
			archive = 0;
			return;
		}

		archive = CreateFile(filenameOpened, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_NO_BUFFERING, NULL);
		if (archive == INVALID_HANDLE_VALUE)
			windowsError(format("File creation failure upon reopening (%s) unbuffered", filenameOpened));
		size.QuadPart -= sectorBufferUse;
		seekPos.QuadPart = size.QuadPart;
		seekPos.LowPart = SetFilePointer(archive, seekPos.LowPart, (LONG*)&seekPos.HighPart, FILE_BEGIN);
		if (seekPos.QuadPart != size.QuadPart)
			windowsError("SetFilePointer() #2 error");
	}
};

template<class NODE>
class InputStream : virtual public Stream<NODE>
{
private:
	BYTE sectorBuffer[512]; // currently assumes 512 byte sectors; maybe use GetDiskFreeSpace() in the future for increased portability
	unsigned sectorBufferPos;
	unsigned sectorBufferEnd;
	uint64_t filePosition;
#ifdef DEBUG
	char filenameOpened[MAX_PATH];
#endif

public:
	InputStream() : sectorBufferPos(0), filePosition(0) {}

	InputStream(const char* filename) : sectorBufferPos(0), filePosition(0)
	{
		open(filename);
	}

	__declspec(noinline)
	void open(const char* filename)
	{
#ifdef DEBUG
		strcpy(filenameOpened, filename);
#endif
		assert(archive==0);
		sectorBufferPos = 0;
		filePosition = 0;
		archive = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_NO_BUFFERING, NULL);
		if (archive == INVALID_HANDLE_VALUE)
			windowsError(format("File open failure (%s)", filename));
	}

	uint64_t position()
	{
		return filePosition / sizeof(NODE);
	}

	__declspec(noinline)
	void seek(uint64_t pos)
	{
		filePosition = pos * sizeof(NODE);
		sectorBufferPos = (unsigned)filePosition % (unsigned)sizeof(sectorBuffer);

		LARGE_INTEGER li;
		DWORD error;
		li.QuadPart = filePosition & -(int64_t)sizeof(sectorBuffer);
		li.LowPart = SetFilePointer(archive, li.LowPart, &li.HighPart, FILE_BEGIN);
		if (li.LowPart == INVALID_SET_FILE_POINTER && (error=GetLastError()) != NO_ERROR)
			windowsError("Seek error");

		if (sectorBufferPos)
		{
			DWORD r;
			BOOL b = ReadFile(archive, sectorBuffer, sizeof(sectorBuffer), &r, NULL);
			if (b)
				sectorBufferEnd = r;
			if (!b || r==0)
				windowsError(format("Read error %d", GetLastError()));
		}
	}

	__declspec(noinline)
	size_t read(NODE* p, size_t n)
	{
		assert(archive, "File not open");
		size_t total = n * sizeof(NODE);
		size_t bytes = 0;
		char* data = (char*)p;
		while (bytes < total)
		{
			size_t left = total-bytes;
			DWORD chunk = left > DISK_IO_CHUNK_SIZE ? DISK_IO_CHUNK_SIZE : (DWORD)left;
			DWORD r = 0;
			BOOL b;
			if (sectorBufferPos)
			{
				if (chunk > sectorBufferEnd - sectorBufferPos)
				{
					chunk = sectorBufferEnd - sectorBufferPos;
					if (chunk == 0)
					{
						assert(bytes % sizeof(NODE) == 0, "Unaligned EOF");
						return bytes / sizeof(NODE);
					}
				}
				memcpy(data + bytes, sectorBuffer + sectorBufferPos, chunk);
				sectorBufferPos += chunk;
				if (sectorBufferPos == sizeof(sectorBuffer))
					sectorBufferPos = 0;
				filePosition += chunk;
				bytes += chunk;
				continue;
			}
			if (chunk % sizeof(sectorBuffer) != 0)
			{
				if (chunk > sizeof(sectorBuffer))
					chunk &= -(int)sizeof(sectorBuffer);
				else
				{
					b = ReadFile(archive, sectorBuffer, sizeof(sectorBuffer), &r, NULL);
					sectorBufferEnd = r;
					if (b && r!=sizeof(sectorBuffer))
					{
						if (r<chunk)
						{
							sectorBufferPos = r;
							filePosition += r;
							memcpy(data + bytes, sectorBuffer, r);
							bytes += r;
							assert(bytes % sizeof(NODE) == 0, "Unaligned EOF");
							return bytes / sizeof(NODE);
						}
					}
					if (!b || r==0)
						windowsError(format("Read error %d", GetLastError()));
					memcpy(data + bytes, sectorBuffer, chunk);
					sectorBufferPos = chunk;
					filePosition += chunk;
					return n;
				}
			}
			b = ReadFile(archive, data + bytes, chunk, &r, NULL);
			if (b && r<chunk)
			{
				sectorBufferEnd = r % (DWORD)sizeof(sectorBuffer);
				sectorBufferPos = sectorBufferEnd;
				memcpy(sectorBuffer, data + bytes + r - sectorBufferEnd, sectorBufferEnd);
				filePosition += r;
				bytes += r;
				assert(bytes % sizeof(NODE) == 0, "Unaligned EOF");
				return bytes / sizeof(NODE);
			}
			if (!b || r==0)
				windowsError(format("Read error %d", GetLastError()));
			filePosition += r;
			bytes += r;
		}
		return n;
	}
};

#else // !defined(USE_UNBUFFERED_DISK_IO):

template<class NODE>
class OutputStream : virtual public Stream<NODE>
{
public:
	OutputStream(){}

	OutputStream(const char* filename, bool resume=false)
	{
		open(filename, resume);
	}

	void open(const char* filename, bool resume=false)
	{
		archive = CreateFile(filename, GENERIC_WRITE, FILE_SHARE_READ, NULL, resume ? OPEN_EXISTING : CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (archive == INVALID_HANDLE_VALUE)
			windowsError(format("File creation failure (%s)", filename));
		if (resume)
		{
			LARGE_INTEGER li;
			li.QuadPart = 0;
			BOOL b = SetFilePointerEx(archive, li, NULL, FILE_END);
			if (!b)
				windowsError("Append error");
		}
	}

	void write(const NODE* p, size_t n)
	{
		assert(archive, "File not open");
		size_t total = n * sizeof(NODE);
		size_t bytes = 0;
		const char* data = (const char*)p;
		while (bytes < total) // write in 256 KB chunks
		{
			size_t left = total-bytes;
			DWORD chunk = left > 256*1024 ? 256*1024 : (DWORD)left;
			DWORD r;
			BOOL b = WriteFile(archive, data + bytes, chunk, &r, NULL);
			if (!b)
				windowsError("Write error");
			if (r == 0)
				windowsError("Out of disk space?");
			bytes += r;
		}
	}
																
	void flush()
	{
		FlushFileBuffers(archive);
	}
};

template<class NODE>
class InputStream : virtual public Stream<NODE>
{
public:
	InputStream(){}

	InputStream(const char* filename)
	{
		open(filename);
	}

	void open(const char* filename)
	{
		archive = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (archive == INVALID_HANDLE_VALUE)
			windowsError(format("File open failure (%s)", filename));
	}

	size_t read(NODE* p, size_t n)
	{
		assert(archive, "File not open");
		size_t total = n * sizeof(NODE);
		size_t bytes = 0;
		char* data = (char*)p;
		while (bytes < total) // read in 256 KB chunks
		{
			size_t left = total-bytes;
			DWORD chunk = left > 256*1024 ? 256*1024 : (DWORD)left;
			DWORD r = 0;
			BOOL b = ReadFile(archive, data + bytes, chunk, &r, NULL);
			if ((!b && GetLastError() == ERROR_HANDLE_EOF) || (b && r==0))
			{
				assert(bytes % sizeof(NODE) == 0, "Unaligned EOF");
				return bytes / sizeof(NODE);
			}
			if (!b || r==0)
				windowsError(format("Read error %d", GetLastError()));
			bytes += r;
		}
		return n;
	}
};

#endif

// For in-place filtering. Written nodes must be <= read nodes.
template<class NODE>
class RewriteStream : public InputStream<NODE>, public OutputStream<NODE>
{
	uint64_t readpos, writepos;
public:
	RewriteStream(){}

	RewriteStream(const char* filename)
	{
		open(filename);
	}
	
	void open(const char* filename)
	{
		archive = CreateFile(filename, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (archive == INVALID_HANDLE_VALUE)
			windowsError("File creation failure");
		readpos = writepos = 0;
	}

	size_t read(NODE* p, size_t n)
	{
		assert(readpos >= writepos, "Write position overwritten");
		seek(readpos);
		size_t r = InputStream::read(p, n);
		readpos += r;
		return r;
	}

	void write(const NODE* p, size_t n)
	{
		seek(writepos);
		OutputStream::write(p, n);
		writepos += n;
	}

	void truncate()
	{
		seek(writepos);
		SetEndOfFile(archive);
	}
};

void deleteFile(const char* filename)
{
	BOOL b = DeleteFile(filename);
	if (!b)
		windowsError(format("Error deleting file %s", filename));
}

void renameFile(const char* from, const char* to, bool replaceExisting=false)
{
	if (replaceExisting)
		DeleteFile(to); // ignore error
	BOOL b = MoveFile(from, to);
	if (!b)
		windowsError(format("Error moving file from %s to %s", from, to));
}

bool fileExists(const char* filename)
{
	return GetFileAttributes(filename) != INVALID_FILE_ATTRIBUTES;
}

uint64_t getFreeSpace()
{
	char dir[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, dir);
	ULARGE_INTEGER li;
	if (!GetDiskFreeSpaceEx(dir, &li, NULL, NULL))
		windowsError("GetDiskFreeSpaceEx error");
	return li.QuadPart;
}
