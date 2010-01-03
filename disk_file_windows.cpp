// Windows files

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
		return li.QuadPart / sizeof(Node);
	}

	uint64_t position()
	{
		LARGE_INTEGER n, o;
		n.QuadPart = 0;
		BOOL b = SetFilePointerEx(archive, n, &o, FILE_CURRENT);
		if (!b)
			error("Seek error");
		return o.QuadPart / sizeof(Node);
	}

	void seek(uint64_t pos)
	{
		LARGE_INTEGER li;
		li.QuadPart = pos * sizeof(Node);
		BOOL b = SetFilePointerEx(archive, li, NULL, FILE_BEGIN);
		if (!b)
			error("Seek error");
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

class OutputStream : virtual public Stream
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
			error("File creation failure");
		if (resume)
		{
			LARGE_INTEGER li;
			li.QuadPart = 0;
			BOOL b = SetFilePointerEx(archive, li, NULL, FILE_END);
			if (!b)
				error("Append error");
		}
	}

	void write(const Node* p, size_t n)
	{
		assert(archive, "File not open");
		size_t total = n * sizeof(Node);
		size_t bytes = 0;
		const char* data = (const char*)p;
		while (bytes < total) // write in 1GB chunks
		{
			size_t left = total-bytes;
			DWORD chunk = left > 0x40000000 ? 0x40000000 : left;
			DWORD r;
			BOOL b = WriteFile(archive, data + bytes, chunk, &r, NULL);
			if (!b)
				error("Write error");
			if (r == 0)
				error("Out of disk space?");
			bytes += r;
		}
	}
																
	void flush()
	{
		FlushFileBuffers(archive);
	}
};

class InputStream : virtual public Stream
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
			error("File open failure");
	}

	size_t read(Node* p, size_t n)
	{
		assert(archive, "File not open");
		size_t total = n * sizeof(Node);
		size_t bytes = 0;
		char* data = (char*)p;
		while (bytes < total) // read in 1GB chunks
		{
			size_t left = total-bytes;
			DWORD chunk = left > 0x40000000 ? 0x40000000 : left;
			DWORD r = 0;
			BOOL b = ReadFile(archive, data + bytes, chunk, &r, NULL);
			if ((!b && GetLastError() == ERROR_HANDLE_EOF) || (b && r==0))
			{
				assert(bytes % sizeof(Node) == 0, "Unaligned EOF");
				return bytes / sizeof(Node);
			}
			if (!b || r==0)
				error(format("Read error %d", GetLastError()));
			bytes += r;
		}
		return n;
	}
};

// For in-place filtering. Written nodes must be <= read nodes.
class RewriteStream : public InputStream, public OutputStream
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
			error("File creation failure");
		readpos = writepos = 0;
	}

	size_t read(Node* p, size_t n)
	{
		assert(readpos >= writepos, "Write position overwritten");
		seek(readpos);
		size_t r = InputStream::read(p, n);
		readpos += r;
		return r;
	}

	void write(const Node* p, size_t n)
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
		error("Error deleting file");
}

void renameFile(const char* from, const char* to)
{
	DeleteFile(to); // ignore error
	BOOL b = MoveFile(from, to);
	if (!b)
		error("Error moving file");
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
		error("GetDiskFreeSpaceEx error");
	return li.QuadPart;
}
