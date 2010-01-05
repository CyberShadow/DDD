#undef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include "config.h"

#include <time.h>
#include <sys/timeb.h>
//#include <fstream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __GNUC__
#include <stdint.h>
#else
#include "pstdint.h"
#endif

// ******************************************************************************************************

#ifdef MULTITHREADING
#if defined(THREAD_BOOST)
#include "thread_boost.cpp"
#elif defined(THREAD_WINAPI)
#include "thread_winapi.cpp"
#else
#error Thread plugin not set
#endif

#if defined(SYNC_BOOST)
#include "sync_boost.cpp"
#elif defined(SYNC_WINAPI)
#include "sync_winapi.cpp"
#elif defined(SYNC_WINAPI_SPIN)
#include "sync_winapi_spin.cpp"
#elif defined(SYNC_INTEL_SPIN)
#include "sync_intel_spin.cpp"
#else
#error Sync plugin not set
#endif
// TODO: look into user-mode scheduling
#endif

// ******************************************************************************************************

void error(const char* message = NULL)
{
	if (message)
		throw message;
	else
		throw "Unspecified error";
}

const char* format(const char *fmt, ...)
{    
	va_list argptr;
	va_start(argptr,fmt);
	//static char buf[1024];
	//char* buf = (char*)malloc(1024);
	static char buffers[16][1024];
	static int bufIndex = 0;
	char* buf = buffers[bufIndex++ % 16];
	vsprintf(buf, fmt, argptr);
	va_end(argptr);
	return buf;
}

const char* defaultstr(const char* a, const char* b = NULL) { return b ? b : a; }

// enforce - check condition in both DEBUG/RELEASE, error() on fail
// assert - check condition in DEBUG builds, try to instruct compiler to assume the condition is true in RELEASE builds
// debug_assert - check condition in DEBUG builds, do nothing in RELEASE builds (classic ASSERT)

#define enforce(expr,...) while(!(expr)){error(defaultstr(format("Check failed at %s:%d", __FILE__,  __LINE__), __VA_ARGS__));throw "Unreachable";}

#undef assert
#ifdef DEBUG
#define assert enforce
#define debug_assert enforce
#define INLINE
#else
#if defined(_MSC_VER)
#define assert(expr,...) __assume((expr)!=0)
#define INLINE __forceinline
#elif defined(__GNUC__)
#define assert(expr,...) __builtin_expect(!(expr),0)
#define INLINE inline
#else
#error Unknown compiler
#endif
#define debug_assert(...) do{}while(0)
#endif

const char* hexDump(const void* data, size_t size)
{
	static char buf[1024];
	enforce(size*3+1 < 1024);
	const uint8_t* s = (const uint8_t*)data;
	for (size_t x=0; x<size; x++)
		sprintf(buf+x*3, "%02X ", s[x]);
	return buf;
}

#define DO_STRINGIZE(x) #x
#define STRINGIZE(x) DO_STRINGIZE(x)

// ******************************************************************************************************

typedef int32_t FRAME;
typedef int32_t FRAME_GROUP;
#if (MAX_FRAMES<65536)
typedef int16_t PACKED_FRAME;
#else
typedef int32_t PACKED_FRAME;
#endif

// ******************************************************************************************************

#include STRINGIZE(PROBLEM/PROBLEM.cpp)

// ******************************************************************************************************

#ifndef COMPRESSED_BYTES
#define COMPRESSED_BYTES ((COMPRESSED_BITS + 7) / 8)
#endif

// It is very important that these comparison operators are as fast as possible.
// TODO: relax ranges for !GROUP_FRAMES

#if   (COMPRESSED_BITS >  24 && COMPRESSED_BITS <=  32) // 4 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return ((const uint32_t*)&a)[0] == ((const uint32_t*)&b)[0]; }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return ((const uint32_t*)&a)[0] != ((const uint32_t*)&b)[0]; }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return ((const uint32_t*)&a)[0] <  ((const uint32_t*)&b)[0]; }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return ((const uint32_t*)&a)[0] <= ((const uint32_t*)&b)[0]; }
#elif (COMPRESSED_BITS >  56 && COMPRESSED_BITS <=  64) // 8 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] == ((const uint64_t*)&b)[0]; }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] != ((const uint64_t*)&b)[0]; }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] <  ((const uint64_t*)&b)[0]; }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] <= ((const uint64_t*)&b)[0]; }
#elif (COMPRESSED_BITS >  96 && COMPRESSED_BITS <= 112) // 13-14 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] == ((const uint64_t*)&b)[0] && (((const uint64_t*)&a)[1]&0x0000FFFFFFFFFFFFLL) == (((const uint64_t*)&b)[1]&0x0000FFFFFFFFFFFFLL); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] != ((const uint64_t*)&b)[0] || (((const uint64_t*)&a)[1]&0x0000FFFFFFFFFFFFLL) != (((const uint64_t*)&b)[1]&0x0000FFFFFFFFFFFFLL); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] <  ((const uint64_t*)&b)[0] || 
                                                                                   (((const uint64_t*)&a)[0] == ((const uint64_t*)&b)[0] && (((const uint64_t*)&a)[1]&0x0000FFFFFFFFFFFFLL) <  (((const uint64_t*)&b)[1]&0x0000FFFFFFFFFFFFLL)); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] <  ((const uint64_t*)&b)[0] || 
                                                                                   (((const uint64_t*)&a)[0] == ((const uint64_t*)&b)[0] && (((const uint64_t*)&a)[1]&0x0000FFFFFFFFFFFFLL) <= (((const uint64_t*)&b)[1]&0x0000FFFFFFFFFFFFLL)); }
#elif (COMPRESSED_BITS > 120 && COMPRESSED_BITS <= 128) // 16 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] == ((const uint64_t*)&b)[0] && ((const uint64_t*)&a)[1] == ((const uint64_t*)&b)[1]; }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] != ((const uint64_t*)&b)[0] || ((const uint64_t*)&a)[1] != ((const uint64_t*)&b)[1]; }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] <  ((const uint64_t*)&b)[0] || 
                                                                                   (((const uint64_t*)&a)[0] == ((const uint64_t*)&b)[0] && ((const uint64_t*)&a)[1] <  ((const uint64_t*)&b)[1]); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] <  ((const uint64_t*)&b)[0] || 
                                                                                   (((const uint64_t*)&a)[0] == ((const uint64_t*)&b)[0] && ((const uint64_t*)&a)[1] <= ((const uint64_t*)&b)[1]); }
#else
#pragma message("Performance warning: using memcmp for CompressedState comparison")
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return memcmp(&a, &b, COMPRESSED_BYTES)==0; }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return memcmp(&a, &b, COMPRESSED_BYTES)!=0; }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return memcmp(&a, &b, COMPRESSED_BYTES)< 0; }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return memcmp(&a, &b, COMPRESSED_BYTES)<=0; }
#endif

INLINE bool operator> (const CompressedState& a, const CompressedState& b) { return b< a; }
INLINE bool operator>=(const CompressedState& a, const CompressedState& b) { return b<=a; }

// ******************************************************************************************************

#ifdef GROUP_FRAMES

#define GET_FRAME(frameGroup, cs) ((frameGroup) * FRAMES_PER_GROUP + (cs).subframe)
#define SET_SUBFRAME(cs, frame) (cs).subframe = (frame) % FRAMES_PER_GROUP

#define GROUP_STR "-group"
#if (FRAMES_PER_GROUP == 10)
#define GROUP_FORMAT "%ux"
#else
#define GROUP_FORMAT "g%u"
#endif

#else // GROUP_FRAMES

#define FRAMES_PER_GROUP 1
#define GET_FRAME(frameGroup, cs) (frameGroup)
#define SET_SUBFRAME(cs, frame)
#define GROUP_STR ""
#define GROUP_FORMAT "%u"

#endif

// ******************************************************************************************************

typedef CompressedState Node;

#if defined(DISK_WINFILES)
#include "disk_file_windows.cpp"
#elif defined(DISK_POSIX)
#include "disk_file_posix.cpp"
#else
#error Disk plugin not set
#endif

// ******************************************************************************************************

// Allocate RAM at start, use it for different purposes depending on what we're doing
// Even if we won't use all of it, most OSes shouldn't reserve physical RAM for the entire amount
void* ram = malloc(RAM_SIZE);

struct CacheNode
{
	CompressedState state;
	PACKED_FRAME frame;
};

const size_t CACHE_HASH_SIZE = RAM_SIZE / sizeof(CacheNode) / NODES_PER_HASH;
CacheNode (*cache)[NODES_PER_HASH] = (CacheNode (*)[NODES_PER_HASH]) ram;

const size_t BUFFER_SIZE = RAM_SIZE / sizeof(Node);
Node* buffer = (Node*) ram;

// ******************************************************************************************************

// 1 MB
#define STREAM_BUFFER_SIZE (1024*1024 / sizeof(Node))

class Buffer
{
protected:
	Node* buf;
	int pos;

	Buffer() : pos(0), buf(NULL)
	{
	}

	void allocate()
	{
		if (!buf)
			buf = new Node[STREAM_BUFFER_SIZE];
	}

	~Buffer()
	{
		delete[] buf;
	}
};

template<class STREAM>
class BufferedStreamBase
{
protected:
	STREAM s;

public:
	uint64_t size() { return s.size(); }
	bool isOpen() { return s.isOpen(); }
	void close() { return s.close(); }
};

template<class STREAM>
class WriteBuffer : protected Buffer, virtual public BufferedStreamBase<STREAM>
{
public:
	void write(const Node* p, bool verify=false)
	{
		buf[pos++] = *p;
#ifdef DEBUG
		if (verify && pos > 1)
			assert(buf[pos-1] > buf[pos-2], "Output is not sorted");
#endif
		if (pos == STREAM_BUFFER_SIZE)
			flushBuffer();
	}

	uint64_t size()
	{
		return s.size() + pos;
	}

	void flushBuffer()
	{
		if (pos)
		{
			s.write(buf, pos);
			pos = 0;
		}
	}

	void flush()
	{
		flushBuffer();
#ifndef NO_DISK_FLUSH
		s.flush();
#endif
	}

	void close()
	{
		flushBuffer();
		BufferedStreamBase<STREAM>::close();
	}

	~WriteBuffer()
	{
		flushBuffer();
	}
};

template<class STREAM>
class ReadBuffer : protected Buffer, virtual public BufferedStreamBase<STREAM>
{
	int end;
public:
	ReadBuffer() : end(0) {}

	const Node* read()
	{
		if (pos == end)
		{
			buffer();
			if (pos == end)
				return NULL;
		}
#ifdef DEBUG
		if (pos > 0) 
			assert(buf[pos-1] < buf[pos], "Input is not sorted");
#endif
		return &buf[pos++];
	}

	void buffer()
	{
		pos = 0;
		uint64_t left = s.size() - s.position();
		end = (int)s.read(buf, (size_t)(left < STREAM_BUFFER_SIZE ? left : STREAM_BUFFER_SIZE));
	}
};

class BufferedInputStream : public ReadBuffer<InputStream>
{
public:
	BufferedInputStream() {}
	BufferedInputStream(const char* filename) { open(filename); }
	void open(const char* filename) { s.open(filename); allocate(); }
};

class BufferedOutputStream : public WriteBuffer<OutputStream>
{
public:
	BufferedOutputStream() {}
	BufferedOutputStream(const char* filename, bool resume=false) { open(filename, resume); }
	void open(const char* filename, bool resume=false) { s.open(filename, resume); allocate(); }
};

class BufferedRewriteStream : public ReadBuffer<RewriteStream>, public WriteBuffer<RewriteStream>
{
public:
	BufferedRewriteStream() {}
	BufferedRewriteStream(const char* filename) { open(filename); }
	void open(const char* filename) { s.open(filename); ReadBuffer<RewriteStream>::allocate(); WriteBuffer<RewriteStream>::allocate(); }
	void truncate() { s.truncate(); }
};

// ******************************************************************************************************

void printTime()
{
	time_t t;
	time(&t);
	char* tstr = ctime(&t);
	tstr[strlen(tstr)-1] = 0;
	printf("[%s] ", tstr);
}

void copyFile(const char* from, const char* to)
{
	InputStream input(from);
	OutputStream output(to);
	uint64_t amount = input.size();
	if (amount > BUFFER_SIZE)
		amount = BUFFER_SIZE;
	size_t records;
	while (records = input.read(buffer, (size_t)amount))
		output.write(buffer, records);
	output.flush(); // force disk flush
}

// ******************************************************************************************************

template<class INPUT>
class InputHeap
{
	struct HeapNode
	{
		const CompressedState* state;
		INPUT* input;
		INLINE bool operator<(const HeapNode& b) const { return *this->state < *b.state; }
	};

	HeapNode *heap, *head;
	int size;

public:
	InputHeap(INPUT inputs[], int count)
	{
		if (count==0)
			error("No inputs");
		heap = new HeapNode[count];
		size = 0;
		for (int i=0; i<count; i++)
		{
			if (inputs[i].isOpen())
			{
				heap[size].input = &inputs[i];
				heap[size].state = inputs[i].read();
				if (heap[size].state)
					size++;
			}
		}
		std::sort(heap, heap+size);
		head = heap;
		heap--; // heap[0] is now invalid, use heap[1] to heap[size] inclusively; head == heap[1]
		if (size==0 && count>0)
			head->state = NULL;
		test();
	}

	~InputHeap()
	{
		heap++;
		delete[] heap;
	}

	const CompressedState* getHead() const { return head->state; }
	INPUT* getHeadInput() const { return head->input; }

	bool next()
	{
		test();
		if (size == 0)
			return false;
		head->state = head->input->read();
		if (head->state == NULL)
		{
			*head = heap[size];
			size--;
			if (size==0)
				return false;
		}
		bubbleDown();
		test();
		return true;
	}

	bool scanTo(const CompressedState& target)
	{
		test();
		if (size == 0)
			return false;
		if (*head->state >= target) // TODO: this check is not always needed
			return true;
		if (size>1)
		{
			do
			{
				CompressedState readUntil = target;
				const CompressedState* minChild = heap[2].state;
				if (size>2 && *minChild > *heap[3].state)
					minChild = heap[3].state;
				if (readUntil > *minChild)
					readUntil = *minChild;
				
				do
					head->state = head->input->read();
				while (head->state && *head->state < readUntil);

				if (head->state == NULL)
				{
					*head = heap[size];
					size--;
				}
				else
					if (*head->state <= *minChild)
						continue;
				bubbleDown();
				test();
				if (size==1)
					if (*head->state < target)
						goto size1;
					else
						return true;
			} while (*head->state < target);
		}
		else
		{
		size1:
			do
				head->state = head->input->read();
			while (head->state && *head->state < target);
			if (head->state == NULL)
			{
				size = 0;
				return false;
			}
		}
		test();
		return true;
	}

	void bubbleDown()
	{
		// Force local variables
		intptr_t c = 1;
		intptr_t size = this->size;
		HeapNode* heap = this->heap;
		HeapNode* pp = head; // pointer to parent
		while (1)
		{
			c = c*2;
			if (c > size)
				return;
			HeapNode* pc = &heap[c];
			if (c < size) // if (c+1 <= size)
			{
				HeapNode* pc2 = pc+1;
				if (*pc2->state < *pc->state)
				{
					pc = pc2;
					c++;
				}
			}
			if (*pp->state <= *pc->state)
				return;
			HeapNode t = *pp;
			*pp = *pc;
			*pc = t;
			pp = pc;
		}
	}

	void test() const
	{
#ifdef DEBUG
		for (int p=1; p<size; p++)
		{
			assert(p*2   > size || *heap[p].state <= *heap[p*2  ].state);
			assert(p*2+1 > size || *heap[p].state <= *heap[p*2+1].state);
		}
#endif
	}
};

void mergeStreams(BufferedInputStream inputs[], int inputCount, BufferedOutputStream* output)
{
	InputHeap<BufferedInputStream> heap(inputs, inputCount);

	const CompressedState* first = heap.getHead();
	if (!first)
		return;
	CompressedState cs = *first;
	
	while (heap.next())
	{
		CompressedState cs2 = *heap.getHead();
		debug_assert(cs2 >= cs);
		if (cs == cs2) // CompressedState::operator== does not compare subframe
		{
#ifdef GROUP_FRAMES
			if (cs.subframe > cs2.subframe) // in case of duplicate frames, pick the one from the smallest frame
				cs.subframe = cs2.subframe;
#endif
		}
		else
		{
			output->write(&cs, true);
			cs = cs2;
		}
	}
	output->write(&cs, true);
}

template <class FILTERED_NODE_HANDLER>
void filterStream(BufferedInputStream* source, BufferedInputStream inputs[], int inputCount, BufferedOutputStream* output)
{
	const CompressedState* sourceState = source->read();
	if (inputCount == 0)
	{
		while (sourceState)
		{
			output->write(sourceState, true);
			FILTERED_NODE_HANDLER::handle(sourceState);
			sourceState = source->read();
		}
		return;
	}
	
	InputHeap<BufferedInputStream> heap(inputs, inputCount);

	while (sourceState)
	{
		bool b = heap.scanTo(*sourceState);
		if (!b) // EOF of heap sources
		{
			do {
				output->write(sourceState, true);
				FILTERED_NODE_HANDLER::handle(sourceState);
				sourceState = source->read();
			} while (sourceState);
			return;
		}
		const CompressedState* head = heap.getHead();
		assert(sourceState);
		while (sourceState && *sourceState < *head)
		{
			output->write(sourceState, true);
			FILTERED_NODE_HANDLER::handle(sourceState);
			sourceState = source->read();
		}
		while (sourceState && *sourceState == *head)
			sourceState = source->read();
	}
}

template <class FILTERED_NODE_HANDLER>
void mergeTwoStreams(BufferedInputStream* input1, BufferedInputStream* input2, BufferedOutputStream* output, BufferedOutputStream* output1)
{
	// output <= merged
	// output1 <= only in input1
	
	BufferedInputStream *inputs[2];
	const CompressedState* states[2];
	inputs[0] = input1;
	inputs[1] = input2;
	states[0] = inputs[0]->read();
	states[1] = inputs[1]->read();
	
	int c;
	while (*states[0] == *states[1])
	{
		output->write(states[0]);
		states[0] = inputs[0]->read();
		states[1] = inputs[1]->read();
		if (states[0] == NULL) { c = 0; goto eof; }
		if (states[1] == NULL) { c = 1; goto eof; }
	}

	c = *states[0] < *states[1] ? 0 : 1;
	while (true)
	{
		const CompressedState* cc = states[c];
		const CompressedState* co = states[c^1];
		debug_assert(*cc < *co);
		BufferedInputStream *ci = inputs[c];
		do
		{
			output->write(cc, true);
			if (c==0)
			{
				output1->write(cc, true);
				FILTERED_NODE_HANDLER::handle(cc);
			}
			cc = ci->read();
			if (cc==NULL)
				goto eof;
		} while (*cc < *co);
		if (*cc == *co)
		{
			states[0] = cc;
			do 
			{
				output->write(states[0]);
				states[0] = inputs[0]->read();
				states[1] = inputs[1]->read();
				if (states[0] == NULL) { c = 0; goto eof; }
				if (states[1] == NULL) { c = 1; goto eof; }
			} while (*states[0] == *states[1]);
			c = *states[0] < *states[1] ? 0 : 1;
		}
		else
		{
			states[c] = cc;
			c ^= 1;
			states[c] = co;
		}
	}
eof:
	c ^= 1;
	const CompressedState* cc = states[c];
	BufferedInputStream* ci = inputs[c];
	while (cc)
	{
		output->write(cc, true);
		if (c==0)
		{
			output1->write(cc, true);
			FILTERED_NODE_HANDLER::handle(cc);
		}
		cc = ci->read();
	}
}

// In-place deduplicate sorted nodes in memory. Return new number of nodes.
size_t deduplicate(CompressedState* start, size_t records)
{
	if (records==0)
		return 0;
	CompressedState *read=start+1, *write=start+1;
	CompressedState *end = start+records;
	while (read < end)
	{
		debug_assert(*read >= *(read-1));
		if (*read == *(write-1)) // CompressedState::operator== does not compare subframe
		{
#ifdef GROUP_FRAMES
			if ((write-1)->subframe > read->subframe)
				(write-1)->subframe = read->subframe;
#endif
		}
		else
		{
			*write = *read;
            write++;
		}
        read++;
	}
	return write-start;
}

// ******************************************************************************************************

const char* formatFileName(const char* name)
{
	return formatProblemFileName(name, NULL, "bin");
}

const char* formatFileName(const char* name, FRAME_GROUP g)
{
	return formatProblemFileName(name, format(GROUP_FORMAT, g), "bin");
}

const char* formatFileName(const char* name, FRAME_GROUP g, unsigned chunk)
{
	return formatProblemFileName(name, format(GROUP_FORMAT "-%u", g, chunk), "bin");
}

// ******************************************************************************************************

#define MAX_FRAME_GROUPS ((MAX_FRAMES+(FRAMES_PER_GROUP-1))/FRAMES_PER_GROUP)

BufferedOutputStream* queue[MAX_FRAME_GROUPS];
bool noQueue[MAX_FRAME_GROUPS];
#ifdef MULTITHREADING
MUTEX queueMutex[MAX_FRAME_GROUPS];
#endif

#ifdef MULTITHREADING
#define PARTITIONS (CACHE_HASH_SIZE/256)
MUTEX cacheMutex[PARTITIONS];
#endif

void queueState(CompressedState* state, FRAME frame)
{
	FRAME_GROUP group = frame/FRAMES_PER_GROUP;
	if (group >= MAX_FRAME_GROUPS)
		return;
	if (noQueue[group])
		return;
	SET_SUBFRAME(*state, frame);
#ifdef MULTITHREADING
	SCOPED_LOCK lock(queueMutex[group]);
#endif
	if (!queue[group])
		queue[group] = new BufferedOutputStream(formatFileName("open", group));
	queue[group]->write(state);
}

INLINE uint32_t hashState(const CompressedState* state)
{
	// Based on MurmurHash ( http://murmurhash.googlepages.com/MurmurHash2.cpp )
	
	const unsigned int m = 0x5bd1e995;
	const int r = 24;

	unsigned int h = sizeof(CompressedState);

	const unsigned char* data = (const unsigned char *)state;

	for (int i=0; i<sizeof(CompressedState)/4; i++) // should unroll
	{
		unsigned int k = *(unsigned int *)data;

		k *= m; 
		k ^= k >> r; 
		k *= m; 
		
		h *= m; 
		h ^= k;

		data += 4;
	}
	
	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
}

void addState(const State* state, FRAME frame)
{
	CompressedState cs;
	state->compress(&cs);
#ifdef DEBUG
	State test;
	test.decompress(&cs);
	if (!(test == *state))
	{
		puts("");
		puts(hexDump(state, sizeof(State)));
		puts(state->toString());
		puts(hexDump(&cs, sizeof(CompressedState)));
		puts(hexDump(&test, sizeof(State)));
		puts(test.toString());
		error("Compression/decompression failed");
	}
#endif
	uint32_t hash = hashState(&cs) % CACHE_HASH_SIZE;
	
	{
#ifdef MULTITHREADING
		SCOPED_LOCK lock(cacheMutex[hash % PARTITIONS]);
#endif
		CacheNode* nodes = cache[hash];
		for (int i=0; i<NODES_PER_HASH; i++)
			if (nodes[i].state == cs)
			{
				if (nodes[i].frame > frame)
					queueState(&cs, frame);
				// pop to front
				if (i>0)
				{
					memmove(nodes+1, nodes, i * sizeof(CacheNode));
					nodes[0].state = cs;
				}
				nodes[0].frame = (PACKED_FRAME)frame;
				return;
			}
		
		// new node
		memmove(nodes+1, nodes, (NODES_PER_HASH-1) * sizeof(CacheNode));
		nodes[0].frame = (PACKED_FRAME)frame;
		nodes[0].state = cs;
	}
	queueState(&cs, frame);
}

void flushQueue()
{
	for (FRAME_GROUP g=0; g<MAX_FRAME_GROUPS; g++)
		if (queue[g])
			queue[g]->flush();
}

// ******************************************************************************************************

FRAME_GROUP firstFrameGroup, maxFrameGroups;
FRAME_GROUP currentFrameGroup;

// ******************************************************************************************************

bool exitFound;
FRAME exitFrame;
State exitState;

void processState(const CompressedState* cs)
{
	State s;
	s.decompress(cs);
#ifdef DEBUG
	CompressedState test;
	s.compress(&test);
	assert(test == *cs, "Compression/decompression failed");
#endif
	FRAME currentFrame = GET_FRAME(currentFrameGroup, *cs);
	if (s.isFinish())
	{
		if (exitFound)
		{
			if (exitFrame > currentFrame)
			{
				exitFrame = currentFrame;
				exitState = s;
			}
		}
		else
		{
			exitFound = true;
			exitFrame = currentFrame;
			exitState = s;
		}
		return;
	}

	class AddStateChildHandler
	{
	public:
		static INLINE void handleChild(const State* state, Step step, FRAME frame)
		{
			addState(state, frame);
		}
	};

	expandChildren<AddStateChildHandler>(currentFrame, &s);
}

#ifdef MULTITHREADING

#define PROCESS_QUEUE_SIZE 0x100000
CompressedState processQueue[PROCESS_QUEUE_SIZE]; // circular buffer
size_t processQueueHead=0, processQueueTail=0;
MUTEX processQueueMutex; // for head/tail
CONDITION processQueueReadCondition, processQueueWriteCondition;

void processFilteredState(const CompressedState* state)
{
	// queue state
	SCOPED_LOCK lock(processQueueMutex);

	while (processQueueHead == processQueueTail+PROCESS_QUEUE_SIZE) // while full
		CONDITION_WAIT(processQueueReadCondition, lock);
	processQueue[processQueueHead++ % PROCESS_QUEUE_SIZE] = *state;
	CONDITION_NOTIFY(processQueueWriteCondition, lock);
}

void dequeueState(CompressedState* state)
{
	SCOPED_LOCK lock(processQueueMutex);
		
	while (processQueueHead == processQueueTail) // while empty
		CONDITION_WAIT(processQueueWriteCondition, lock);
	*state = processQueue[processQueueTail++ % PROCESS_QUEUE_SIZE];
	CONDITION_NOTIFY(processQueueReadCondition, lock);
}

void worker()
{
	CompressedState cs;
	while (true)
	{
		dequeueState(&cs);
		processState(&cs);
	}
}

void flushProcessQueue()
{
	/*
	SCOPED_LOCK lock(processQueueMutex);

	while (processQueueHead != processQueueTail) // while not empty
		CONDITION_WAIT(processQueueReadCondition, lock);
    /*/
    while (true)
    {
    	{
	    	SCOPED_LOCK lock(processQueueMutex);
    		if (processQueueHead == processQueueTail)
    			return;
    	}
    	SLEEP(1);
    }//*/
}

#else

void processFilteredState(const CompressedState* state)
{
	processState(state);
}

#endif

class ProcessStateHandler
{
public:
	INLINE static void handle(const CompressedState* state)
	{
		processFilteredState(state);
	}
};

// ******************************************************************************************************

State exitSearchState;
FRAME exitSearchStateFrame;
Step exitSearchStateStep;
bool exitSearchStateFound = false;

class FinishCheckChildHandler
{
public:
	static INLINE void handleChild(const State* state, Step step, FRAME frame)
	{
		if (*state==exitSearchState && frame==exitSearchStateFrame)
		{
			exitSearchStateFound = true;
			exitSearchStateStep = step;
		}
	}
};

void traceExit()
{
	Step steps[MAX_STEPS];
	int stepNr = 0;
	{
		exitSearchState      = exitState;
		exitSearchStateFrame = exitFrame;
		int frameGroup = currentFrameGroup;
		while (frameGroup >= 0)
		{
	nextStep:
			exitSearchStateFound = false;
			frameGroup--;
			if (fileExists(formatFileName("closed", frameGroup)))
			{
				printf("Frame" GROUP_STR " " GROUP_FORMAT "... \r", frameGroup);
				// TODO: parallelize?
				InputStream input(formatFileName("closed", frameGroup));
				CompressedState cs;
				while (input.read(&cs, 1))
				{
					State state;
					state.decompress(&cs);
					FRAME frame = GET_FRAME(frameGroup, cs);
					expandChildren<FinishCheckChildHandler>(frame, &state);
					if (exitSearchStateFound)
					{
						printf("Found (at %d)!          \n", frame);
						steps[stepNr++] = exitSearchStateStep;
						exitSearchState      = state;
						exitSearchStateFrame = frame;
						if (frame == 0)
							goto found;
						goto nextStep;
					}
				}
			}
		}
	}
	error("Lost parent node!");
found:

	writeSolution(&exitSearchState, steps, stepNr);
}

// ******************************************************************************************************

size_t ramUsed;

void sortAndMerge(FRAME_GROUP g)
{
	// Step 1: read chunks of BUFFER_SIZE nodes, sort+dedup them in RAM and write them to disk
	int chunks = 0;
	ramUsed = 0;
	printf("Sorting... "); fflush(stdout);
	{
		InputStream input(formatFileName("open", g));
		uint64_t amount = input.size();
		if (amount > BUFFER_SIZE)
			amount = BUFFER_SIZE;
		size_t records;
		while (records = input.read(buffer, (size_t)amount))
		{
			if (ramUsed < records * sizeof(Node))
				ramUsed = records * sizeof(Node);
			std::sort(buffer, buffer + records);
			records = deduplicate(buffer, records);
			OutputStream output(formatFileName("chunk", g, chunks));
			output.write(buffer, records);
			chunks++;
		}
	}

	// Step 2: merge + dedup chunks
	printf("Merging... "); fflush(stdout);
	if (chunks>1)
	{
		BufferedInputStream* chunkInput = new BufferedInputStream[chunks];
		for (int i=0; i<chunks; i++)
			chunkInput[i].open(formatFileName("chunk", g, i));
		BufferedOutputStream* output = new BufferedOutputStream(formatFileName("merging", g));
		mergeStreams(chunkInput, chunks, output);
		delete[] chunkInput;
		output->flush();
		delete output;
		renameFile(formatFileName("merging", g), formatFileName("merged", g));
		for (int i=0; i<chunks; i++)
			deleteFile(formatFileName("chunk", g, i));
	}
	else
	{
		renameFile(formatFileName("chunk", g, 0), formatFileName("merged", g));
	}
}

bool checkStop()
{
	if (fileExists(formatProblemFileName("stop", NULL, "txt")))
	{
		deleteFile(formatProblemFileName("stop", NULL, "txt"));
		printf("Stop file found.\n");
		return true;
	}
	return false;
}

// ******************************************************************************************************

int search()
{
	firstFrameGroup = 0;

	for (FRAME_GROUP g=MAX_FRAME_GROUPS; g>0; g--)
		if (fileExists(formatFileName("closed", g)))
		{
			printf("Resuming from frame" GROUP_STR " " GROUP_FORMAT "\n", g+1);
			firstFrameGroup = g+1;
			break;
	    }

	for (FRAME_GROUP g=firstFrameGroup; g<MAX_FRAME_GROUPS; g++)
		if (fileExists(formatFileName("open", g)))
		{
			printTime(); printf("Reopening queue for frame" GROUP_STR " " GROUP_FORMAT "\n", g);
			queue[g] = new BufferedOutputStream(formatFileName("open", g), true);
		}

	if (firstFrameGroup==0 && !queue[0])
	{
		for (int i=0; i<initialStateCount; i++)
		{
			State s = initialStates[i];
			CompressedState c;
			s.compress(&c);
			queueState(&c, 0);
		}
	}

#ifdef MULTITHREADING
	for (int i=0; i<THREADS-1; i++)
		THREAD_CREATE(&worker);
#endif

	for (currentFrameGroup=firstFrameGroup; currentFrameGroup<maxFrameGroups; currentFrameGroup++)
	{
		if (!queue[currentFrameGroup])
			continue;
		delete queue[currentFrameGroup];
		queue[currentFrameGroup] = NULL;

		printTime(); printf("Frame" GROUP_STR " " GROUP_FORMAT "/" GROUP_FORMAT ": ", currentFrameGroup, maxFrameGroups); fflush(stdout);

		if (fileExists(formatFileName("merged", currentFrameGroup)))
		{
			printf("(reopening merged)    ");
		}
		else
		{
			// Step 1 & 2: sort and merge open nodes
			sortAndMerge(currentFrameGroup);
		}

		printf("Clearing... "); fflush(stdout);
		memset(ram, 0, ramUsed); // clear cache

		// Step 3: dedup against previous frames, while simultaneously processing filtered nodes
		printf("Processing... "); fflush(stdout);
#ifdef USE_ALL
		if (currentFrameGroup==0)
		{
			copyFile(formatFileName("merged", currentFrameGroup), formatFileName("closing", currentFrameGroup));
			renameFile(formatFileName("merged", currentFrameGroup), formatFileName("all"));
			
			BufferedInputStream input(formatFileName("closing", currentFrameGroup));
			const CompressedState* cs;
			while (cs = input.read())
				processFilteredState(cs);
		}
		else
		{
			BufferedInputStream* source = new BufferedInputStream(formatFileName("merged", currentFrameGroup));
			BufferedInputStream* all = new BufferedInputStream(formatFileName("all"));
			BufferedOutputStream* allnew = new BufferedOutputStream(formatFileName("allnew"));
			BufferedOutputStream* closing = new BufferedOutputStream(formatFileName("closing", currentFrameGroup));
			mergeTwoStreams<ProcessStateHandler>(source, all, allnew, closing);
			allnew->flush();
			closing->flush();
			delete all;
			delete source;
			delete allnew;
			delete closing;
			deleteFile(formatFileName("all"));
			renameFile(formatFileName("allnew"), formatFileName("all"));
			deleteFile(formatFileName("merged", currentFrameGroup));
		}
#else
		{
			BufferedInputStream* source = new BufferedInputStream(formatFileName("merged", currentFrameGroup));
			BufferedInputStream* inputs = new BufferedInputStream[MAX_FRAME_GROUPS];
			int inputCount = 0;
			for (FRAME_GROUP g=0; g<currentFrameGroup; g++)
				if (fileExists(formatFileName("closed", g)))
				{
					inputs[inputCount].open(formatFileName("closed", g));
					if (inputs[inputCount].size())
						inputCount++;
					else
						inputs[inputCount].close();
				}
			if (fileExists(formatFileName("closing", currentFrameGroup)))
			{
				//printf("Overwriting %s\n", formatFileName("closing", currentFrameGroup));
				deleteFile(formatFileName("closing", currentFrameGroup));
			}
			BufferedOutputStream* output = new BufferedOutputStream(formatFileName("closing", currentFrameGroup));
			filterStream<ProcessStateHandler>(source, inputs, inputCount, output);
			delete source;
			output->flush(); // force disk flush
			delete output;
			delete[] inputs;
			deleteFile(formatFileName("merged", currentFrameGroup));
		}
#endif

#ifdef MULTITHREADING
		flushProcessQueue();
#endif

		printf("Flushing... "); fflush(stdout);
		flushQueue();

		deleteFile(formatFileName("open", currentFrameGroup));
		renameFile(formatFileName("closing", currentFrameGroup), formatFileName("closed", currentFrameGroup));
		
		printf("Done.\n");

		if (exitFound)
		{
			printf("Exit found (at frame %u), tracing path...\n", exitFrame);
			traceExit();
			return 0;
		}

		if (checkStop())
			return 3;

#ifdef FREE_SPACE_THRESHOLD
		if (getFreeSpace() < FREE_SPACE_THRESHOLD)
		{
			int filterOpen();
			int sortOpen();
			printf("Low disk space detected. Sorting open nodes...\n");
			sortOpen();
			printf("Done. Filtering open nodes...\n");
			filterOpen();
			if (getFreeSpace() < FREE_SPACE_THRESHOLD)
				error("Open node filter failed to produce sufficient free space");
			printf("Done, resuming search...\n");

		}
#endif
	}
	
	printf("Exit not found.\n");
	return 2;
}

// ******************************************************************************************************

int packOpen()
{
	for (FRAME_GROUP g=firstFrameGroup; g<maxFrameGroups; g++)
    	if (fileExists(formatFileName("open", g)))
    	{
			printTime(); printf("Frame" GROUP_STR " " GROUP_FORMAT ": ", g);

			{
				InputStream input(formatFileName("open", g));
				OutputStream output(formatFileName("openpacked", g));
				uint64_t amount = input.size();
				if (amount > BUFFER_SIZE)
					amount = BUFFER_SIZE;
				size_t records;
				uint64_t read=0, written=0;
				while (records = input.read(buffer, (size_t)amount))
				{
					read += records;
					std::sort(buffer, buffer + records);
					records = deduplicate(buffer, records);
					written += records;
					output.write(buffer, records);
				}
				output.flush();

				if (read == written)
					printf("No improvement.\n");
				else
					printf("%llu -> %llu.\n", read, written);
			}
			deleteFile(formatFileName("open", g));
			renameFile(formatFileName("openpacked", g), formatFileName("open", g));
    	}
	return 0;
}

// ******************************************************************************************************

int dump(FRAME_GROUP g)
{
	printf("Dumping frame" GROUP_STR " " GROUP_FORMAT ":\n", g);
	const char* fn = formatFileName("closed", g);
	if (!fileExists(fn))
		fn = formatFileName("open", g);
	if (!fileExists(fn))
		error(format("Can't find neither open nor closed node file for frame" GROUP_STR " " GROUP_FORMAT, g));
	
	BufferedInputStream in(fn);
	const CompressedState* cs;
	while (cs = in.read())
	{
#ifdef GROUP_FRAMES
		printf("Frame %u:\n", GET_FRAME(g, *cs));
#endif
		State s;
		s.decompress(cs);
		puts(s.toString());
	}
	return 0;
}

// ******************************************************************************************************

int sample(FRAME_GROUP g)
{
	printf("Sampling frame" GROUP_STR " " GROUP_FORMAT ":\n", g);
	const char* fn = formatFileName("closed", g);
	if (!fileExists(fn))
		fn = formatFileName("open", g);
	if (!fileExists(fn))
		error(format("Can't find neither open nor closed node file for frame" GROUP_STR " " GROUP_FORMAT, g));
	
	InputStream in(fn);
	srand((unsigned)time(NULL));
	in.seek(((uint64_t)rand() + ((uint64_t)rand()<<32)) % in.size());
	CompressedState cs;
	in.read(&cs, 1);
#ifdef GROUP_FRAMES
	printf("Frame %u:\n", GET_FRAME(g, cs));
#endif
	State s;
	s.decompress(&cs);
	puts(s.toString());
	return 0;
}

// ******************************************************************************************************

int compare(const char* fn1, const char* fn2)
{
	BufferedInputStream i1(fn1), i2(fn2);
	printf("%s: %llu states\n%s: %llu states\n", fn1, i1.size(), fn2, i2.size());
	const CompressedState *cs1, *cs2;
	cs1 = i1.read();
	cs2 = i2.read();
	uint64_t dups = 0;
	uint64_t switches = 0;
	int last=0, cur;
	while (cs1 && cs2)
	{
		if (*cs1 < *cs2)
			cs1 = i1.read(),
			cur = -1;
		else
		if (*cs1 > *cs2)
			cs2 = i2.read(),
			cur = 1;
		else
		{
			dups++;
			cs1 = i1.read();
			cs2 = i2.read();
			cur = 0;
		}
		if (cur != last)
			switches++;
		last = cur;
	}
	printf("%llu duplicate states\n", dups);
	printf("%llu interweaves\n", switches);
	return 0;
}

// ******************************************************************************************************

#ifdef GROUP_FRAMES

// This works only if the size of CompressedState is the same as the old version (without the subframe field).

// HACK: the following code uses pointer arithmetics with BufferedInputStream objects to quickly determine the subframe from which a CompressedState came from.

void convertMerge(BufferedInputStream inputs[], int inputCount, BufferedOutputStream* output)
{
	InputHeap<BufferedInputStream> heap(inputs, inputCount);
	//uint64_t* positions = new uint64_t[inputCount];
	//for (int i=0; i<inputCount; i++)
	//	positions[i] = 0;

	CompressedState cs = *heap.getHead();
	debug_assert(heap.getHeadInput() >= inputs && heap.getHeadInput() < inputs+FRAMES_PER_GROUP);
	cs.subframe = heap.getHeadInput() - inputs;
	bool oooFound = false, equalFound = false;
	while (heap.next())
	{
		CompressedState cs2 = *heap.getHead();
		debug_assert(heap.getHeadInput() >= inputs && heap.getHeadInput() < inputs+FRAMES_PER_GROUP);
		uint8_t subframe = heap.getHeadInput() - inputs;
		//positions[subframe]++;
		cs2.subframe = subframe;
		if (cs2 < cs) // work around flush bug in older versions
		{
			if (!oooFound)
			{
				printf("Unordered states found in subframe %d, skipping\n", subframe);
				oooFound = true;
			}
			continue;
		}
		if (cs == cs2) // CompressedState::operator== does not compare subframe
		{
			if (!equalFound)
			{
				//printf("Duplicate states in subframes %d at %lld and %d at %lld\n", cs.subframe, positions[cs.subframe], subframe, positions[subframe]);
				printf("Duplicate states found in subframes %d and %d\n", cs.subframe, subframe);
				equalFound = true;
			}
			if (cs.subframe > subframe) // in case of duplicate frames, pick the one from the smallest frame
				cs.subframe = subframe;
		}
		else
		{
			output->write(&cs, true);
			cs = cs2;
		}
	}
	output->write(&cs, true);
}

int convert()
{
	for (FRAME_GROUP g=firstFrameGroup; g<maxFrameGroups; g++)
	{
		bool haveClosed=false, haveOpen=false;
		BufferedInputStream inputs[FRAMES_PER_GROUP];
		for (FRAME f=g*FRAMES_PER_GROUP; f<(g+1)*FRAMES_PER_GROUP; f++)
			if (fileExists(formatProblemFileName("closed", format("%u", f), "bin")))
				inputs[f%FRAMES_PER_GROUP].open(formatProblemFileName("closed", format("%u", f), "bin")),
				haveClosed = true;
			else
			if (fileExists(formatProblemFileName("open", format("%u", f), "bin")))
				inputs[f%FRAMES_PER_GROUP].open(formatProblemFileName("open", format("%u", f), "bin")),
				haveOpen = true;
		if (haveOpen || haveClosed)
		{
			printf(GROUP_FORMAT "...\n", g);
			{
				BufferedOutputStream output(formatFileName("converting", g));
				convertMerge(inputs, FRAMES_PER_GROUP, &output);
			}
			renameFile(formatFileName("converting", g), formatFileName(haveOpen ? "open" : "closed", g));
		}
		free(inputs);
	}
	return 0;
}

// ******************************************************************************************************

// Unpack a closed node frame group file to individual frames.

int unpack()
{
	for (FRAME_GROUP g=firstFrameGroup; g<maxFrameGroups; g++)
		if (fileExists(formatFileName("closed", g)))
		{
			printTime(); printf("Frame" GROUP_STR " " GROUP_FORMAT "\n", g); fflush(stdout);
			BufferedInputStream input(formatFileName("closed", g));
			BufferedOutputStream outputs[FRAMES_PER_GROUP];
			for (int i=0; i<FRAMES_PER_GROUP; i++)
				outputs[i].open(formatProblemFileName("closed", format("%u", g*FRAMES_PER_GROUP+i), "bin"));
			const CompressedState* cs;
			while (cs = input.read())
			{
				CompressedState cs2 = *cs;
				cs2.subframe = 0;
				outputs[cs->subframe].write(&cs2);
			}
		}
	return 0;
}

// ******************************************************************************************************

int count()
{
	for (FRAME_GROUP g=firstFrameGroup; g<maxFrameGroups; g++)
		if (fileExists(formatFileName("closed", g)))
		{
			printTime(); printf("Frame" GROUP_STR " " GROUP_FORMAT ":\n", g);
			BufferedInputStream input(formatFileName("closed", g));
			const CompressedState* cs;
			uint64_t counts[FRAMES_PER_GROUP] = {0};
			while (cs = input.read())
				counts[cs->subframe]++;
			for (int i=0; i<FRAMES_PER_GROUP; i++)
				if (counts[i])
					printf("Frame %u: %llu\n", g*FRAMES_PER_GROUP+i, counts[i]);
			fflush(stdout);
		}
	return 0;
}

#endif // GROUP_FRAMES

// ******************************************************************************************************

int verify(const char* filename)
{
	BufferedInputStream input(filename);
	CompressedState cs = *input.read();
	bool equalFound=false, oooFound=false;
	uint64_t pos = 0;
	while (1)
	{
		const CompressedState* cs2 = input.read();
		pos++;
		if (cs2==NULL)
			return 0;
		if (cs == *cs2)
			if (!equalFound)
			{
				printf("Equal states found: %lld\n", pos);
				equalFound = true;
			}
		if (cs > *cs2)
			if (!oooFound)
			{
				printf("Unordered states found: %lld\n", pos);
				oooFound = true;
			}
#ifdef GROUP_FRAMES
		if (cs2->subframe >= FRAMES_PER_GROUP)
			error("Invalid subframe (corrupted data?)");
#endif
		cs = *cs2;
		if (equalFound && oooFound)
			return 0;
	}
}

// ******************************************************************************************************

int sortOpen()
{
	for (FRAME_GROUP currentFrameGroup=maxFrameGroups-1; currentFrameGroup>=firstFrameGroup; currentFrameGroup--)
	{
		if (!fileExists(formatFileName("open", currentFrameGroup)))
			continue;
		if (fileExists(formatFileName("merged", currentFrameGroup)))
			error("Merged file present");
		uint64_t initialSize, finalSize;
		{
			InputStream s(formatFileName("open", currentFrameGroup));
			initialSize = s.size();
		}
		if (initialSize==0)
			continue;

		printTime(); printf("Frame" GROUP_STR " " GROUP_FORMAT "/" GROUP_FORMAT ": ", currentFrameGroup, maxFrameGroups); fflush(stdout);

		sortAndMerge(currentFrameGroup);
		
		deleteFile(formatFileName("open", currentFrameGroup));
		renameFile(formatFileName("merged", currentFrameGroup), formatFileName("open", currentFrameGroup));

		{
			InputStream s(formatFileName("open", currentFrameGroup));
			finalSize = s.size();
		}

		printf("Done: %lld -> %lld.\n", initialSize, finalSize);

		if (checkStop())
			return 3;
	}
	return 0;
}

// ******************************************************************************************************

// Filters open node lists without expanding nodes.

int seqFilterOpen()
{
	// override global *FrameGroup vars
	for (FRAME_GROUP currentFrameGroup=firstFrameGroup; currentFrameGroup<maxFrameGroups; currentFrameGroup++)
	{
		if (!fileExists(formatFileName("open", currentFrameGroup)))
			continue;

		printTime(); printf("Frame" GROUP_STR " " GROUP_FORMAT "/" GROUP_FORMAT ": ", currentFrameGroup, maxFrameGroups); fflush(stdout);

		uint64_t initialSize, finalSize;

		if (fileExists(formatFileName("merged", currentFrameGroup)))
		{
			printf("(reopening merged)    ");
		}
		else
		{
			{
				InputStream s(formatFileName("open", currentFrameGroup));
				initialSize = s.size();
			}

			// Step 1 & 2: sort and merge open nodes
			sortAndMerge(currentFrameGroup);
		}

		// Step 3: dedup against previous frames
		printf("Filtering... "); fflush(stdout);
		{
			class NullStateHandler
			{
			public:
				INLINE static void handle(const CompressedState* state) {}
			};

			BufferedInputStream* source = new BufferedInputStream(formatFileName("merged", currentFrameGroup));
			BufferedInputStream* inputs = new BufferedInputStream[MAX_FRAME_GROUPS+1];
			int inputCount = 0;
			for (FRAME_GROUP g=0; g<currentFrameGroup; g++)
			{
				const char* fn = formatFileName("open", g);
#ifndef USE_ALL
				if (!fileExists(fn))
					fn = formatFileName("closed", g);
#endif
				if (fileExists(fn))
				{
					inputs[inputCount].open(fn);
					if (inputs[inputCount].size())
						inputCount++;
					else
						inputs[inputCount].close();
				}
			}
#ifdef USE_ALL
			inputs[inputCount++].open(formatFileName("all"));
#endif
			BufferedOutputStream* output = new BufferedOutputStream(formatFileName("filtering", currentFrameGroup));
			filterStream<NullStateHandler>(source, inputs, inputCount, output);
			delete source;
			output->flush(); // force disk flush
			delete output;
			delete[] inputs;
			deleteFile(formatFileName("merged", currentFrameGroup));
		}

		deleteFile(formatFileName("open", currentFrameGroup));
		renameFile(formatFileName("filtering", currentFrameGroup), formatFileName("open", currentFrameGroup));

		{
			InputStream s(formatFileName("open", currentFrameGroup));
			finalSize = s.size();
		}

		printf("Done: %lld -> %lld.\n", initialSize, finalSize);

		if (checkStop())
			return 3;
	}
	return 0;
}

// ******************************************************************************************************

void filterStreams(BufferedInputStream closed[], int closedCount, BufferedRewriteStream open[], int openCount)
{
	InputHeap<BufferedInputStream> closedHeap(closed, closedCount);
	InputHeap<BufferedRewriteStream> openHeap(open, openCount);

	bool done = false;
	while (!done)
	{
		CompressedState o = *openHeap.getHead();
		FRAME lowestFrame = MAX_FRAMES;
		do
		{
			FRAME_GROUP group = (FRAME_GROUP)(openHeap.getHeadInput() - open);
			FRAME frame = GET_FRAME(group, *openHeap.getHead());
			if (lowestFrame > frame)
				lowestFrame = frame;
			if (!openHeap.next())
			{
				done = true;
				break;
			}
			if (o > *openHeap.getHead())
				error(format("Unsorted open node file for frame" GROUP_STR " " GROUP_FORMAT "/" GROUP_FORMAT, group, openHeap.getHeadInput() - open));
		} while (o == *openHeap.getHead());
		
		if (closedHeap.scanTo(o))
			if (*closedHeap.getHead() == o)
			{
				closedHeap.next();
				continue;
			}
		SET_SUBFRAME(o, lowestFrame);
		open[lowestFrame/FRAMES_PER_GROUP].write(&o, true);
	}
}

int filterOpen()
{
	BufferedRewriteStream* open = new BufferedRewriteStream[MAX_FRAME_GROUPS];
	for (FRAME_GROUP g=0; g<MAX_FRAME_GROUPS; g++)
		if (fileExists(formatFileName("open", g)))
		{
			enforce(!fileExists(formatFileName("closed", g)), format("Open and closed node files present for the same frame" GROUP_STR " " GROUP_FORMAT, g));
			open[g].open(formatFileName("open", g));
	    }

#ifndef USE_ALL
	BufferedInputStream* closed = new BufferedInputStream[MAX_FRAME_GROUPS];
	for (FRAME_GROUP g=0; g<MAX_FRAME_GROUPS; g++)
		if (fileExists(formatFileName("closed", g)))
			closed[g].open(formatFileName("closed", g));

	filterStreams(closed, MAX_FRAME_GROUPS, open, MAX_FRAME_GROUPS);

	delete[] closed;
#else
	BufferedInputStream all(formatFileName("all"));
	filterStreams(&all, 1, open, MAX_FRAME_GROUPS);
#endif
	
	for (FRAME_GROUP g=0; g<MAX_FRAME_GROUPS; g++) // on success, truncate open nodes manually
		if (open[g].isOpen())
			open[g].truncate();
	
	delete[] open;
	return 0;
}

// ******************************************************************************************************

int regenerateOpen()
{
	for (FRAME_GROUP g=0; g<MAX_FRAME_GROUPS; g++)
		if (fileExists(formatFileName("closed", g)) || fileExists(formatFileName("open", g)))
			noQueue[g] = true;
	
#ifdef MULTITHREADING
	for (int i=0; i<THREADS-1; i++)
		THREAD_CREATE(&worker);
#endif

	while (maxFrameGroups>0 && !fileExists(formatFileName("closed", maxFrameGroups-1)))
		maxFrameGroups--;

	uint64_t oldSize = 0;
	for (currentFrameGroup=firstFrameGroup; currentFrameGroup<maxFrameGroups; currentFrameGroup++)
		if (fileExists(formatFileName("closed", currentFrameGroup)))
		{
			printTime(); printf("Frame" GROUP_STR " " GROUP_FORMAT "/" GROUP_FORMAT ": ", currentFrameGroup, maxFrameGroups); fflush(stdout);
			
			BufferedInputStream closed(formatFileName("closed", currentFrameGroup));
			const CompressedState* cs;
			while (cs = closed.read())
				processFilteredState(cs);
			
#ifdef MULTITHREADING
			flushProcessQueue();
			//printf("%d/%d\n", processQueueHead, processQueueTail);
#endif
			flushQueue();
			
			uint64_t size = 0;
			for (FRAME_GROUP g=0; g<MAX_FRAME_GROUPS; g++)
				if (queue[g])
					size += queue[g]->size();

			printf("Done (%lld).\n", size - oldSize);
			oldSize = size;

			if (checkStop())
				return 3;
		}
	
	return 0;
}

// ******************************************************************************************************

int createAll()
{
	BufferedInputStream* closed = new BufferedInputStream[MAX_FRAME_GROUPS];
	for (FRAME_GROUP g=0; g<MAX_FRAME_GROUPS; g++)
		if (fileExists(formatFileName("closed", g)))
			closed[g].open(formatFileName("closed", g));

	{
		BufferedOutputStream all(formatFileName("allnew"));
		mergeStreams(closed, MAX_FRAME_GROUPS, &all);
	}

	renameFile(formatFileName("allnew"), formatFileName("all"));
	return 0;
}

// ******************************************************************************************************

// use background CPU and I/O priority when PC is not idle

#if defined(_WIN32)
#pragma comment(lib,"user32")
DWORD WINAPI idleWatcher(__in LPVOID lpParameter)
{
    LASTINPUTINFO lii;
    lii.cbSize = sizeof(LASTINPUTINFO);
    while (true)
    {
		do
		{
			Sleep(1000);
			GetLastInputInfo(&lii);
		}
		while (GetTickCount() - lii.dwTime > 60*1000);
		
		do
		{
			FILE* f = fopen("idle.txt", "rt");
			if (f)
			{
				int work, idle;
				fscanf(f, "%d %d", &work, &idle);
				fclose(f);
				
				Sleep(work);
				SetPriorityClass(GetCurrentProcess(), PROCESS_MODE_BACKGROUND_BEGIN);
				Sleep(idle);
				SetPriorityClass(GetCurrentProcess(), PROCESS_MODE_BACKGROUND_END);
			}
			else
				Sleep(1000);
			GetLastInputInfo(&lii);
		}
		while (GetTickCount() - lii.dwTime < 60*1000);
	}
}
#endif

// ******************************************************************************************************

timeb startTime;

void printExecutionTime()
{
	timeb endTime;
	ftime(&endTime);
	time_t ms = (endTime.time - startTime.time)*1000
	       + (endTime.millitm - startTime.millitm);
	printf("Time: %d.%03d seconds.\n", ms/1000, ms%1000);
}

// ***********************************************************************************

int parseInt(const char* str)
{
	int result;
	if (!sscanf(str, "%d", &result))
		error(format("'%s' is not a valid integer", str));
	return result;
}

void parseFrameRange(int argc, const char* argv[])
{
	if (argc==0)
		firstFrameGroup = 0, 
		maxFrameGroups  = MAX_FRAME_GROUPS;
	else
	if (argc==1)
		firstFrameGroup = parseInt(argv[0]),
		maxFrameGroups  = firstFrameGroup+1;
	else
	if (argc==2)
		firstFrameGroup = parseInt(argv[0]),
		maxFrameGroups  = parseInt(argv[1]);
	else
		error("Too many arguments");
}

const char* usage = "\
Generic C++ DDD solver\n\
(c) 2009-2010 Vladimir \"CyberShadow\" Panteleev\n\
Usage:\n\
	search <mode> <parameters>\n\
where <mode> is one of:\n\
	search [max-frame"GROUP_STR"]\n\
		Sorts, filters and expands open nodes. 	If no open node files\n\
		are present, starts a new search from the initial state.\n\
	dump <frame"GROUP_STR">\n\
		Dumps all states from the specified frame"GROUP_STR", which\n\
		can be either open or closed.\n\
	sample <frame"GROUP_STR">\n\
		Displays a random state from the specified frame"GROUP_STR", which\n\
		can be either open or closed.\n\
	compare <filename-1> <filename-2>\n\
		Counts the number of duplicate nodes in two files. The nodes in\n\
		the files must be sorted and deduplicated.\n"
#ifdef GROUP_FRAMES
"	convert [frame"GROUP_STR"-range]\n\
		Converts individual frame files to frame"GROUP_STR" files for the\n\
		specified frame"GROUP_STR" range.\n\
	unpack [frame"GROUP_STR"-range]\n\
		Converts frame"GROUP_STR" files back to individual frame files\n\
		(reverses the \"convert\" operation).\n\
	count [frame"GROUP_STR"-range]\n\
		Counts the number of nodes in individual frames for the\n\
		specified frame"GROUP_STR" files.\n"
#endif
"	verify <filename>\n\
		Verifies that the nodes in a file are correctly sorted and\n\
		deduplicated, as well as a few additional integrity checks.\n\
	pack-open [frame"GROUP_STR"-range]\n\
		Removes duplicates within each chunk for open node files in the\n\
		specified range. Reads and writes open nodes only once.\n\
	sort-open [frame"GROUP_STR"-range]\n\
		Sorts and removes duplicates for open node files in the\n\
		specified range. File are processed in reverse order.\n\
	filter-open\n\
		Filters all open node files. Requires that all open node files\n\
		be sorted and deduplicated (run sort-open before filter-open).\n\
		Filtering is performed in-place. An aborted run shouldn't cause\n\
		data loss, but will require re-sorting.\n\
	seq-filter-open [frame"GROUP_STR"-range]\n\
		Sorts, deduplicates and filters open node files in the\n\
		specified range, one by one. Specify the range cautiously,\n\
		as this function requires that previous open node files be\n\
		sorted and deduplicated (and filtered for best performance).\n\
	regenerate-open [frame"GROUP_STR"-range]\n\
		Re-expands closed nodes in the specified frame"GROUP_STR" range.\n\
		New (open) nodes are saved only for frame"GROUP_STR"s that don't\n\
		already have an open or closed node file. Use this when an open\n\
		node file has been accidentally deleted or corrupted. To\n\
		regenerate all open nodes, delete all open node files before\n\
		running regenerate-open (this is still faster than restarting\n\
		the search).\n\
	create-all\n\
		Creates the \"all\" file from closed node files. Use when\n\
		turning on USE_ALL, or when the \"all\" file was corrupted.\n\
A [frame"GROUP_STR"-range] is a space-delimited list of zero, one or two frame"GROUP_STR"\n\
numbers. If zero numbers are specified, the range is assumed to be all\n\
frame"GROUP_STR"s. If one number is specified, the range is set to only that\n\
frame"GROUP_STR" number. If two numbers are specified, the range is set to start\n\
from the first frame"GROUP_STR" number inclusively, and end at the second\n\
frame"GROUP_STR" number NON-inclusively.\n\
";

int run(int argc, const char* argv[])
{
	enforce(sizeof(intptr_t)==sizeof(size_t), "Bad intptr_t!");
	enforce(sizeof(int)==4, "Bad int!");
	enforce(sizeof(long long)==8, "Bad long long!");

	initProblem();

#ifdef DEBUG
	printf("Debug version\n");
#else
	printf("Optimized version\n");
#endif

#ifdef MULTITHREADING
#if defined(THREAD_BOOST)
	printf("Using %u Boost threads ", THREADS);
#elif defined(THREAD_WINAPI)
	printf("Using %u WinAPI threads ", THREADS);
#else
#error Thread plugin not set
#endif
#endif

#if defined(SYNC_BOOST)
	printf("with Boost sync\n");
#elif defined(SYNC_WINAPI)
	printf("with WinAPI sync\n");
#elif defined(SYNC_WINAPI_SPIN)
	printf("with WinAPI spinlock sync\n");
#elif defined(SYNC_INTEL_SPIN)
	printf("with Intel spinlock sync\n");
#else
#error Sync plugin not set
#endif
	
	printf("Compressed state is %u bits (%u bytes)\n", COMPRESSED_BITS, sizeof(CompressedState));
#ifdef GROUP_FRAMES
	enforce(COMPRESSED_BITS <= (sizeof(CompressedState)-1)*8);
#else
	enforce(COMPRESSED_BITS <= sizeof(CompressedState)*8);
	enforce((COMPRESSED_BITS+31)/8 >= sizeof(CompressedState));
#endif
	enforce(sizeof(CompressedState)%4 == 0);

	enforce(ram, "RAM allocation failed");
	printf("Using %lld bytes of RAM for %lld cache nodes and %lld buffer nodes\n", (long long)RAM_SIZE, (long long)CACHE_HASH_SIZE, (long long)BUFFER_SIZE);

#if defined(DISK_WINFILES)
	printf("Using Windows API files\n");
#elif defined(DISK_POSIX)
	printf("Using POSIX files\n");
#else
#error Disk plugin not set
#endif

	if (fileExists(formatProblemFileName("stop", NULL, "txt")))
	{
		printf("Stop file present.\n");
		return 3;
	}

#if defined(_WIN32)
	CreateThread(NULL, 0, &idleWatcher, NULL, 0, NULL);
#endif

	printf("Command-line:");
	for (int i=0; i<argc; i++)
		printf(" %s", argv[i]);
	printf("\n");

	maxFrameGroups = MAX_FRAME_GROUPS;

	ftime(&startTime);
	atexit(&printExecutionTime);

	if (argc>1 && strcmp(argv[1], "search")==0)
	{
		if (argc>2)
			maxFrameGroups = parseInt(argv[2]);
		return search();
	}
	else
	if (argc>1 && strcmp(argv[1], "dump")==0)
	{
		enforce(argc==3, "Specify a frame"GROUP_STR" to dump");
		return dump(parseInt(argv[2]));
	}
	else
	if (argc>1 && strcmp(argv[1], "sample")==0)
	{
		enforce(argc==3, "Specify a frame"GROUP_STR" to sample");
		return sample(parseInt(argv[2]));
	}
	else
	if (argc>1 && strcmp(argv[1], "compare")==0)
	{
		enforce(argc==4, "Specify two files to compare");
		return compare(argv[2], argv[3]);
	}
	else
#ifdef GROUP_FRAMES
	if (argc>1 && strcmp(argv[1], "convert")==0)
	{
		parseFrameRange(argc-2, argv+2);
		return convert();
	}
	else
	if (argc>1 && strcmp(argv[1], "unpack")==0)
	{
		parseFrameRange(argc-2, argv+2);
		return unpack();
	}
	else
	if (argc>1 && strcmp(argv[1], "count")==0)
	{
		parseFrameRange(argc-2, argv+2);
		return count();
	}
	else
#endif
	if (argc>1 && strcmp(argv[1], "verify")==0)
	{
		enforce(argc==3, "Specify a file to verify");
		return verify(argv[2]);
	}
	else
	if (argc>1 && strcmp(argv[1], "pack-open")==0)
	{
		parseFrameRange(argc-2, argv+2);
		return packOpen();
	}
	else
	if (argc>1 && strcmp(argv[1], "sort-open")==0)
	{
		parseFrameRange(argc-2, argv+2);
		return sortOpen();
	}
	else
	if (argc>1 && strcmp(argv[1], "filter-open")==0)
	{
		return filterOpen();
	}
	else
	if (argc>1 && strcmp(argv[1], "seq-filter-open")==0)
	{
		parseFrameRange(argc-2, argv+2);
		return seqFilterOpen();
	}
	else
	if (argc>1 && strcmp(argv[1], "regenerate-open")==0)
	{
		parseFrameRange(argc-2, argv+2);
		return regenerateOpen();
	}
	if (argc>1 && strcmp(argv[1], "create-all")==0)
	{
		return createAll();
	}
	else
	{
		printf("%s", usage);
		return 0;
	}
}

// ***********************************************************************************

int main(int argc, const char* argv[]) { try { return run(argc, argv); } catch(const char* s) { printf("\n%s\n", s); return 1; } }
//#include "test_body.cpp"
