// POSIX files

class OutputStream
{
	int archive;
public:
	OutputStream(const char* filename)
	{
		archive = _open(filename, _O_BINARY | _O_WRONLY | _O_TRUNC | _O_CREAT | _O_SEQUENTIAL, _S_IREAD);
		if (archive == -1)
			error(_strerror(NULL));
	}

	void write(const Node* p, int n)
	{
		size_t total = n * sizeof(Node);
		size_t bytes = 0;
		const char* data = (const char*)p;
		while (bytes < total) // write in 1GB chunks
		{
			size_t left = total-bytes;
			size_t chunk = left > 0x40000000 ? 0x40000000 : left;
			int r = _write(archive, data + bytes, chunk);
			if (r < 0)
				error(_strerror(NULL));
			if (r == 0)
				error("Out of disk space?");
			bytes += r;
		}
	}

	~OutputStream()
	{
		_close(archive);
	}
};

class InputStream
{
	int archive;
public:
	InputStream(const char* filename)
	{
		archive = _open(filename, _O_BINARY | _O_RDONLY | _O_SEQUENTIAL, _S_IREAD);
		if (archive == -1)
			error(_strerror(NULL));
	}

	int read(Node* p, int n)
	{
		size_t total = n * sizeof(Node);
		size_t bytes = 0;
		char* data = (char*)p;
		while (bytes < total) // read in 1GB chunks
		{
			size_t left = total-bytes;
			size_t chunk = left > 0x40000000 ? 0x40000000 : left;
			int r = _read(archive, data + bytes, chunk);
			if (r < 0)
				error(_strerror(NULL));
			if (r == 0)
			{
				assert(bytes % sizeof(Node) == 0, "Unaligned EOF");
				return bytes / sizeof(Node);
			}
			bytes += r;
		}
		return n;
	}

	int size()
	{
		// TODO
		error("Not implemented");
	}

	~InputStream()
	{
		_close(archive);
	}
};

void deleteFile(const char* filename)
{
	int r = _unlink(filename);
	if (r)
		error("Error deleting file");
}
