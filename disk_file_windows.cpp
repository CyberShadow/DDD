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
		return li.QuadPart / sizeof(NODE);
	}

	uint64_t position()
	{
		LARGE_INTEGER n, o;
		n.QuadPart = 0;
		BOOL b = SetFilePointerEx(archive, n, &o, FILE_CURRENT);
		if (!b)
			windowsError("Seek error");
		return o.QuadPart / sizeof(NODE);
	}

	void seek(uint64_t pos)
	{
		LARGE_INTEGER li;
		li.QuadPart = pos * sizeof(NODE);
		BOOL b = SetFilePointerEx(archive, li, NULL, FILE_BEGIN);
		if (!b)
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

template<class NODE>
class OutputStream : virtual public Stream<NODE>
{
private:
	HANDLE archive_buffered;
	BYTE sectorBuffer[512]; // currently assumes 512 byte sectors; maybe use GetDiskFreeSpace() in the future for increased portability
	WORD sectorBufferUse;
	WORD sectorBufferFlushed;

public:
	OutputStream() : archive_buffered(0), sectorBufferUse(0), sectorBufferFlushed(0) {}

	OutputStream(const char* filename, bool resume=false) : archive_buffered(0), sectorBufferUse(0), sectorBufferFlushed(0)
	{
		open(filename, resume);
	}

	~OutputStream()
	{
		close();
	}

	void close()
	{
		flush();
		Stream::close();
		if (archive_buffered)
		{
			CloseHandle(archive_buffered);
			archive_buffered = 0;
		}
	}

	void open(const char* filename, bool resume=false)
	{
		archive          = CreateFile(filename, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, resume ? OPEN_EXISTING : CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_NO_BUFFERING, NULL);
		archive_buffered = CreateFile(filename, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,          OPEN_ALWAYS               , FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN                         , NULL);
		if (archive == INVALID_HANDLE_VALUE || archive_buffered == INVALID_HANDLE_VALUE)
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
		const BYTE* data = (const BYTE*)p;
		while (bytes < total) // write in 256 KB chunks
		{
			size_t left = total-bytes;
			DWORD chunk = left > 256*1024 ? 256*1024 : (DWORD)left;
			DWORD r;
			BOOL b;
			if (sectorBufferUse)
			{
				if (chunk > sizeof(sectorBuffer) - sectorBufferUse)
					chunk = sizeof(sectorBuffer) - sectorBufferUse;
				memcpy(sectorBuffer + sectorBufferUse, data, chunk);
				sectorBufferUse += (WORD)chunk;
				if (sectorBufferUse == sizeof(sectorBuffer))
				{
					b = WriteFile(archive, sectorBuffer, sizeof(sectorBuffer), &r, NULL);
					if (!b)
						windowsError("Write error");
					if (r != sizeof(sectorBuffer))
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
			b = WriteFile(archive, data + bytes, chunk, &r, NULL);
			if (!b)
				windowsError("Write error");
			if (r == 0)
				windowsError("Out of disk space?");
			bytes += r;
		}
	}

	void flush()
	{
		if (archive_buffered && sectorBufferFlushed < sectorBufferUse)
		{
			DWORD r;
			BOOL b;
			b = WriteFile(archive_buffered, sectorBuffer, sectorBufferUse, &r, NULL);
			if (!b)
				windowsError("Write error");
			if (r != sectorBufferUse)
				windowsError("Out of disk space?");
			FlushFileBuffers(archive_buffered);
			sectorBufferFlushed = sectorBufferUse;
			//b = SetFilePointer(archive_buffered, -sectorBufferUse, NULL, FILE_END);
			/*b = SetFilePointer(archive_buffered, 0, NULL, FILE_BEGIN);
			if (!b)
				windowsError("SetFilePointer() error");*/
		}
		//FlushFileBuffers(archive);
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
		archive = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_NO_BUFFERING, NULL);
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

void renameFile(const char* from, const char* to)
{
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
