// Portable plain C files

#include <stdint.h>
#include <stdio.h>

#if defined(PREALLOCATE_EXPANDED) || defined(PREALLOCATE_COMBINING)
#error PREALLOCATE_* are not supported in DISK_FILE_C
#endif

uint64_t getFileSize(const char* filename)
{
	FILE* fp = fopen(filename, "rb");
	if (!fp)
	{
		fprintf(stderr, "fopen(%s) failed (%d)\n", filename, errno);
		return 0;
	}
	fseek(fp, 0, SEEK_END);
	uint64_t size = ftell(fp);
	fclose(fp);
	return size;
}

template<class NODE>
class Stream
{
protected:
	FILE* archive;

public:
	Stream() : archive(NULL) {}

	bool isOpen() const { return archive != NULL; }

	uint64_t size()
	{
		long pos = ftell(archive);
		fseek(archive, 0, SEEK_END);
		long size = ftell(archive);
		fseek(archive, pos, SEEK_SET);
		assert(size % sizeof(NODE) == 0, "Unaligned EOF");
		return size / sizeof(NODE);
	}

	uint64_t position()
	{
		return ftell(archive) / sizeof(NODE);
	}

	void seek(uint64_t pos)
	{
		long long_pos = (long)(pos * sizeof(NODE));
		assert(long_pos / sizeof(NODE) == pos, "Overflow");
		fseek(archive, long_pos, SEEK_SET);
	}

	void close()
	{
		if (isOpen())
		{
// #if defined(PREALLOCATE_EXPANDED) || defined(PREALLOCATE_COMBINING)
// 			ftruncate(archive, position());
// #endif
			fclose(archive);
			archive = NULL;
		}
	}

	~Stream()
	{
		close();
	}
};

#ifdef USE_UNBUFFERED_DISK_IO

#error USE_UNBUFFERED_DISK_IO is not supported in DISK_FILE_C

#else // !defined(USE_UNBUFFERED_DISK_IO):

template<class NODE>
class OutputStream : virtual public Stream<NODE>
{
	using Stream<NODE>::archive;

public:
	using Stream<NODE>::isOpen;

	OutputStream(){}

	OutputStream(const char* filename, bool resume=false)
	{
		open(filename, resume);
	}

	void open(const char* filename, bool resume=false)
	{
		archive = fopen(filename, resume ? "ab" : "wb");
		if (archive == NULL)
			fprintf(stderr, "File creation failure (%s)\n", filename);
	}

	void write(const NODE* p, size_t n)
	{
		assert(isOpen(), "File not open");
		size_t total = n * sizeof(NODE);
		size_t bytes = 0;
		const char* data = (const char*)p;
		while (bytes < total) // write in 256 KB chunks
		{
			size_t left = total-bytes;
			uint32_t chunk = left > 256*1024 ? 256*1024 : (uint32_t)left;
			size_t w = fwrite(data + bytes, 1, chunk, archive);
			if (w < chunk)
				fprintf(stderr, "Write error (%d)\n", errno);
			bytes += w;
		}
	}

	void flush()
	{
		fflush(archive);
	}
};

template<class NODE>
class InputStream : virtual public Stream<NODE>
{
	using Stream<NODE>::archive;

public:
	using Stream<NODE>::isOpen;

	InputStream(){}

	InputStream(const char* filename)
	{
		open(filename);
	}

	void open(const char* filename)
	{
		archive = fopen(filename, "rb");
		if (archive == NULL)
			fprintf(stderr, "File open failure (%s) (%d)\n", filename, errno);
	}

	size_t read(NODE* p, size_t n)
	{
		assert(isOpen(), "File not open");
		size_t total = n * sizeof(NODE);
		size_t bytes = 0;
		char* data = (char*)p;
		while (bytes < total) // read in 256 KB chunks
		{
			size_t left = total-bytes;
			uint32_t chunk = left > 256*1024 ? 256*1024 : (uint32_t)left;
			size_t r = fread(data + bytes, 1, chunk, archive);
			if (r < chunk)
			{
				if (feof(archive))
				{
					assert(bytes % sizeof(NODE) == 0, "Unaligned EOF");
					return bytes / sizeof(NODE);
				}
				fprintf(stderr, "Read error (%d)\n", errno);
			}
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
	using Stream<NODE>::archive;
	using Stream<NODE>::seek;

	uint64_t readpos, writepos;
public:
	RewriteStream(){}

	RewriteStream(const char* filename)
	{
		open(filename);
	}

	void open(const char* filename)
	{
		archive = fopen(filename, "r+b");
		if (archive < 0)
			fprintf(stderr, "File creation failure (%s) (%d\n", filename, errno);
		readpos = writepos = 0;
	}

	size_t read(NODE* p, size_t n)
	{
		assert(readpos >= writepos, "Write position overwritten");
		seek(readpos);
		size_t r = InputStream<NODE>::read(p, n);
		readpos += r;
		return r;
	}

	void write(const NODE* p, size_t n)
	{
		seek(writepos);
		OutputStream<NODE>::write(p, n);
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
	int ret = remove(filename);
	if (ret < 0)
		fprintf(stderr, "File deletion failure (%s) (%d)\n", filename, errno);
}

void renameFile(const char* from, const char* to, bool replaceExisting=false)
{
	int ret = rename(from, to);
	if (ret < 0)
		fprintf(stderr, "File rename failure (%s -> %s) (%d)\n", from, to, errno);
}

bool fileExists(const char* filename)
{
	FILE* fp = fopen(filename, "rb");
	if (fp == NULL)
		return false;
	fclose(fp);
	return true;
}

uint64_t getFreeSpace()
{
	return (uint64_t)-1; // stub
}
