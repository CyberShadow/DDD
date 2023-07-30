// POSIX files

#warning "This file is hopelessly out of date. Update it using disk_file_windows.cpp as reference."

class OutputStream
{
	int archive;
public:
	OutputStream(const char* filename, bool resume=false)
	{
		archive = _open(filename, _O_BINARY | _O_WRONLY | _O_SEQUENTIAL | (resume ? _O_APPEND : _O_CREAT | _O_EXCL), _S_IREAD);
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

	void flush()
	{
		// TODO
		#error "Not implemented"
	}

	~OutputStream()
	{
		_close(archive);
	}
};

template<class NODE>
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

	int read(NODE* p, int n)
	{
		size_t total = n * sizeof(NODE);
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
				assert(bytes % sizeof(NODE) == 0, "Unaligned EOF");
				return bytes / sizeof(NODE);
			}
			bytes += r;
		}
		return n;
	}

	uint64_t size()
	{
		// TODO
		#error "Not implemented"
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

void renameFile(const char* from, const char* to)
{
	// TODO
	#error "Not implemented"
}

bool fileExists(const char* filename)
{
	// TODO
	#error "Not implemented"
}

#ifdef PREALLOCATE_EXPANDED
void preparePreallocation()
{
	// TODO
}
#endif
