// POSIX files

#include <stdint.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <stdio.h> // perror

uint64_t getFileSize(const char* filename)
{
	struct stat st;
	if (stat(filename, &st) == 0)
		return st.st_size;
	perror(format("stat (%s)", filename));
	return 0;
}

template<class NODE>
class Stream
{
protected:
	int archive; // file descriptor

public:
	Stream() : archive(-1) {}

	bool isOpen() const { return archive >= 0; }

	uint64_t size()
	{
		struct stat st;
		if (fstat(archive, &st) == 0)
		{
			assert(st.st_size % sizeof(NODE) == 0, "Unaligned EOF");
			return st.st_size / sizeof(NODE);
		}
		perror("fstat");
		return 0;
	}

	uint64_t position()
	{
		return lseek(archive, 0, SEEK_CUR) / sizeof(NODE);
	}

	void seek(uint64_t pos)
	{
		off_t res = lseek(archive, pos * sizeof(NODE), SEEK_SET);
		if (res == -1)
			perror("lseek");
	}

	void close()
	{
		if (isOpen())
		{
#if defined(PREALLOCATE_EXPANDED) || defined(PREALLOCATE_COMBINING)
			ftruncate(archive, position());
#endif
			::close(archive);
			archive = -1;
		}
	}

	~Stream()
	{
		close();
	}
};

#ifdef USE_UNBUFFERED_DISK_IO

#error Unfinished, TODO

template<class NODE>
class OutputStream : virtual public Stream<NODE>
{
	using Stream<NODE>::archive;
	using Stream<NODE>::isOpen;
	using Stream<NODE>::size;

private:
	uint8_t sectorBuffer[512]; // currently assumes 512 byte sectors; maybe use GetDiskFreeSpace() in the future for increased portability
	uint16_t sectorBufferUse;
	uint16_t sectorBufferFlushed;
	char filenameOpened[PATH_MAX];

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

	NOINLINE(void) seek(uint64_t pos)
	{
		if (sectorBufferFlushed < sectorBufferUse)
		{
			ssize_t w = ::write(archive, sectorBuffer, sizeof(sectorBuffer));
			if (w < 0)
				perror("write");
			if (w != sizeof(sectorBuffer))
				perror("write (incomplete) - out of disk space?");
			sectorBufferUse = 0;
			sectorBufferFlushed = 0;
		}

		pos *= sizeof(NODE);

		Stream<NODE>::seek(pos & -(int64_t)sizeof(sectorBuffer));

		sectorBufferUse = (uint16_t)pos % (uint16_t)sizeof(sectorBuffer);

		if (sectorBufferUse)
		{
			memset(sectorBuffer, 0, sizeof(sectorBuffer));
			ssize_t r = ::read(archive, sectorBuffer, sizeof(sectorBuffer));
			if (r < 0)
				perror("read");
			if (r>=0 && r!=sectorBufferUse)
				perror(format("Read alignment error in write alignment (%s)", filenameOpened));
			if (r==0)
				perror(format("Read error in write alignment (%s)", filenameOpened));

			Stream<NODE>::seek(pos & -(int64_t)sizeof(sectorBuffer));
		}
	}

	NOINLINE(void) open(const char* filename, bool resume=false)
	{
		assert(!isOpen());
		sectorBufferUse = 0;
		sectorBufferFlushed = 0;
		strcpy(filenameOpened, filename);
		archive = ::open(filename, O_RDWR | (resume ? 0 : O_CREAT | O_TRUNC) | O_DIRECT, 0666);
		if (archive < 0)
			perror(format("File creation failure (%s)", filename));
		if (resume)
			seek(size());
	}

#if defined(PREALLOCATE_EXPANDED) || defined(PREALLOCATE_COMBINING)
	NOINLINE(void) preallocate(uint64_t size)
	{
		assert(isOpen());
		int res = posix_fallocate(archive, 0, size);
		if (res != 0)
			perror("fallocate");
		seek(0);
	}
#endif

	NOINLINE(void) write(const NODE* p, size_t n)
	{
		assert(isOpen(), "File not open");
		size_t total = n * sizeof(NODE);
		size_t bytes = 0;
		const uint8_t* data = (const uint8_t*)p;
		while (bytes < total)
		{
			size_t left = total-bytes;
			uint32_t chunk = left > DISK_IO_CHUNK_SIZE ? DISK_IO_CHUNK_SIZE : (uint32_t)left;
			if (sectorBufferUse)
			{
				if (chunk > (uint32_t)sizeof(sectorBuffer) - (uint32_t)sectorBufferUse)
					chunk = (uint32_t)sizeof(sectorBuffer) - (uint32_t)sectorBufferUse;
				memcpy(sectorBuffer + sectorBufferUse, data + bytes, chunk);
				sectorBufferUse += (uint16_t)chunk;
				if (sectorBufferUse == sizeof(sectorBuffer))
				{
					ssize_t w = ::write(archive, sectorBuffer, sizeof(sectorBuffer));
					if (w < 0)
						perror("write");
					if (w != sizeof(sectorBuffer))
						perror("write (incomplete) - out of disk space?");
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
					sectorBufferUse = (uint16_t)chunk;
					return;
				}
			}
			ssize_t w = ::write(archive, data + bytes, chunk);
			if (w < 0)
				perror("write");
			if (w != sizeof(sectorBuffer))
				perror("write (incomplete) - out of disk space?");
			bytes += w;
		}
	}

private:
	NOINLINE(void) flush(bool reopen)
	{
		if (!archive)
			return;

		if (sectorBufferFlushed == sectorBufferUse) // nothing to flush?
		{
			if (reopen)
				FlushFileBuffers(archive);
			else
			{
#if defined(PREALLOCATE_EXPANDED) || defined(PREALLOCATE_COMBINING)
				SetEndOfFile(archive);
#endif
				CloseHandle(archive);
				archive = 0;
			}
			return;
		}
		assert(sectorBufferFlushed < sectorBufferUse);

		BOOL b;
		uint32_t w;
		ULARGE_INTEGER size;
#if defined(PREALLOCATE_EXPANDED) || defined(PREALLOCATE_COMBINING)
		{
			LARGE_INTEGER size0;
			size0.QuadPart = 0;
			if (!SetFilePointerEx(archive, size0, &(LARGE_INTEGER&)size, FILE_CURRENT))
				perror("Seek error");
		}
#else
		size.LowPart = GetFileSize(archive, &size.HighPart);
#endif
		size.QuadPart += sectorBufferUse;

		b = WriteFile(archive, sectorBuffer, sizeof(sectorBuffer), &w, NULL);
		if (!b)
			perror("Write error");
		if (w != sizeof(sectorBuffer))
			perror("Out of disk space?");
		CloseHandle(archive);

		archive = CreateFile(filenameOpened, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_WRITE_THROUGH, NULL);
		if (archive == INVALID_HANDLE_VALUE)
			perror(format("File creation failure upon reopening (%s) buffered", filenameOpened));
		
		ULARGE_INTEGER seekPos = size;
		uint32_t seek_error;
		seekPos.LowPart = SetFilePointer(archive, seekPos.LowPart, (LONG*)&seekPos.HighPart, FILE_BEGIN);
		if (seekPos.LowPart == INVALID_SET_FILE_POINTER && (seek_error=GetLastError()) != NO_ERROR)
			perror("SetFilePointer() #1 error");
		if (seekPos.QuadPart != size.QuadPart)
			error("Error setting file size");
		b = SetEndOfFile(archive);
		if (!b)
			perror("SetEndOfFile() error");
		CloseHandle(archive);
		sectorBufferFlushed = sectorBufferUse;
		if (!reopen)
		{
			archive = 0;
			return;
		}

		archive = CreateFile(filenameOpened, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_NO_BUFFERING, NULL);
		if (archive == INVALID_HANDLE_VALUE)
			perror(format("File creation failure upon reopening (%s) unbuffered", filenameOpened));
		size.QuadPart -= sectorBufferUse;
		seekPos.QuadPart = size.QuadPart;
		seekPos.LowPart = SetFilePointer(archive, seekPos.LowPart, (LONG*)&seekPos.HighPart, FILE_BEGIN);
		if (seekPos.QuadPart != size.QuadPart)
			perror("SetFilePointer() #2 error");
	}
};

template<class NODE>
class InputStream : virtual public Stream<NODE>
{
private:
	uint8_t sectorBuffer[512]; // currently assumes 512 byte sectors; maybe use GetDiskFreeSpace() in the future for increased portability
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

	NOINLINE(void) open(const char* filename)
	{
#ifdef DEBUG
		strcpy(filenameOpened, filename);
#endif
		assert(archive==0);
		sectorBufferPos = 0;
		filePosition = 0;
		archive = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_NO_BUFFERING, NULL);
		if (archive == INVALID_HANDLE_VALUE)
			perror(format("File open failure (%s)", filename));
	}

	uint64_t position()
	{
		return filePosition / sizeof(NODE);
	}

	NOINLINE(void) seek(uint64_t pos)
	{
		filePosition = pos * sizeof(NODE);
		sectorBufferPos = (unsigned)filePosition % (unsigned)sizeof(sectorBuffer);

		LARGE_INTEGER li;
		uint32_t error;
		li.QuadPart = filePosition & -(int64_t)sizeof(sectorBuffer);
		li.LowPart = SetFilePointer(archive, li.LowPart, &li.HighPart, FILE_BEGIN);
		if (li.LowPart == INVALID_SET_FILE_POINTER && (error=GetLastError()) != NO_ERROR)
			perror("Seek error");

		if (sectorBufferPos)
		{
			uint32_t r;
			BOOL b = ReadFile(archive, sectorBuffer, sizeof(sectorBuffer), &r, NULL);
			if (b)
				sectorBufferEnd = r;
			if (!b || r==0)
				perror(format("Read error %d", GetLastError()));
		}
	}

	NOINLINE(size_t) read(NODE* p, size_t n)
	{
		assert(archive, "File not open");
		size_t total = n * sizeof(NODE);
		size_t bytes = 0;
		char* data = (char*)p;
		while (bytes < total)
		{
			size_t left = total-bytes;
			uint32_t chunk = left > DISK_IO_CHUNK_SIZE ? DISK_IO_CHUNK_SIZE : (uint32_t)left;
			uint32_t r = 0;
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
						perror(format("Read error %d", GetLastError()));
					memcpy(data + bytes, sectorBuffer, chunk);
					sectorBufferPos = chunk;
					filePosition += chunk;
					return n;
				}
			}
			b = ReadFile(archive, data + bytes, chunk, &r, NULL);
			if (b && r<chunk)
			{
				sectorBufferEnd = r % (uint32_t)sizeof(sectorBuffer);
				sectorBufferPos = sectorBufferEnd;
				memcpy(sectorBuffer, data + bytes + r - sectorBufferEnd, sectorBufferEnd);
				filePosition += r;
				bytes += r;
				assert(bytes % sizeof(NODE) == 0, "Unaligned EOF");
				return bytes / sizeof(NODE);
			}
			if (!b || r==0)
				perror(format("Read error %d", GetLastError()));
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
	using Stream<NODE>::archive;

public:
	using Stream<NODE>::seek;
	using Stream<NODE>::size;
	using Stream<NODE>::isOpen;

	OutputStream(){}

	OutputStream(const char* filename, bool resume=false)
	{
		open(filename, resume);
	}

	void open(const char* filename, bool resume=false)
	{
		archive = ::open(filename, O_WRONLY | (resume ? 0 : O_CREAT | O_TRUNC), 0666);
		if (archive < 0)
			perror(format("File creation failure (%s)", filename));
		if (resume)
			seek(size());
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
			ssize_t w = ::write(archive, data + bytes, chunk);
			if (w < 0)
				perror("Write error");
			if (w == 0)
				perror("Out of disk space?");
			bytes += w;
		}
	}

#if defined(PREALLOCATE_EXPANDED) || defined(PREALLOCATE_COMBINING)
	void preallocate(uint64_t size)
	{
		assert(isOpen());
		int res = posix_fallocate(archive, 0, size);
		if (res != 0)
			perror("fallocate");
		seek(0);
	}
#endif

	void flush()
	{
		fsync(archive);
	}
};

template<class NODE>
class InputStream : virtual public Stream<NODE>
{
	using Stream<NODE>::archive;

public:
	using Stream<NODE>::size;
	using Stream<NODE>::isOpen;

	InputStream(){}

	InputStream(const char* filename)
	{
		open(filename);
	}

	void open(const char* filename)
	{
		archive = ::open(filename, O_RDONLY);
		if (archive < 0)
			perror(format("File open failure (%s)", filename));
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
			ssize_t r = ::read(archive, data + bytes, chunk);
			if (r < 0)
				perror("read");
			if (r == 0)
			{
				assert(bytes % sizeof(NODE) == 0, "Unaligned EOF");
				return bytes / sizeof(NODE);
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
	using Stream<NODE>::size;
	using Stream<NODE>::seek;
	using Stream<NODE>::isOpen;

	uint64_t readpos, writepos;
public:
	RewriteStream(){}

	RewriteStream(const char* filename)
	{
		open(filename);
	}

	void open(const char* filename)
	{
		archive = open(filename, O_RDWR);
		if (archive < 0)
			perror("File creation failure");
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
	int ret = unlink(filename);
	if (ret < 0)
		perror("unlink");
}

void renameFile(const char* from, const char* to, bool replaceExisting=false)
{
	int ret = rename(from, to);
	if (ret < 0)
		perror("rename");
}

bool fileExists(const char* filename)
{
	struct stat st;
	return stat(filename, &st) == 0;
}

uint64_t getFreeSpace()
{
	struct statvfs stat;
	if (statvfs("." , &stat) != 0)
		perror("statvfs");
	return stat.f_bavail * stat.f_frsize;
}

#if defined(PREALLOCATE_EXPANDED) || defined(PREALLOCATE_COMBINING)
void preparePreallocation()
{
}
#endif
