// Windows files

class OutputStream
{
	HANDLE archive;
public:
	OutputStream(const char* filename)
	{
		archive = CreateFile(filename, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (archive == INVALID_HANDLE_VALUE)
			error("File creation failure");
	}

	void write(const Node* p, int n)
	{
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

	~OutputStream()
	{
		CloseHandle(archive);
	}
};

class InputStream
{
	HANDLE archive;
public:
	InputStream(const char* filename)
	{
		archive = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (archive == INVALID_HANDLE_VALUE)
			error("File open failure");
	}

	int read(Node* p, int n)
	{
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

	size_t size()
	{
		ULARGE_INTEGER li;
		li.LowPart = GetFileSize(archive, &li.HighPart);
		return li.QuadPart / sizeof(Node);
	}

	~InputStream()
	{
		CloseHandle(archive);
	}
};

void deleteFile(const char* filename)
{
	BOOL b = DeleteFile(filename);
	if (!b)
		error("Error deleting file");
}
