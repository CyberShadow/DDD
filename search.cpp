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

#include "Kwirk.cpp"
#include "hsiehhash.cpp"

// ******************************************************************************************************

State initialState, blankState;

// ******************************************************************************************************

typedef uint32_t FRAME;
typedef uint16_t PACKED_FRAME;

#pragma pack(1)
struct Step
{
	Action action;
	uint8_t x;
	uint8_t y;
	uint8_t extraSteps;
};

INLINE int replayStep(State* state, FRAME* frame, Step step)
{
	Player* p = &state->players[state->activePlayer];
	int nx = step.x+1;
	int ny = step.y+1;
	int steps = abs((int)p->x - nx) + abs((int)p->y - ny) + step.extraSteps;
	p->x = nx;
	p->y = ny;
	assert(state->map[ny][nx]==0, "Bad coordinates");
	int res = state->perform((Action)step.action);
	assert(res>0, "Replay failed");
	*frame += steps * DELAY_MOVE + res;
	return steps; // not counting actual action
}

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

#define STREAM_BUFFER_SIZE (1024*1024 / sizeof(Node))

Node* streamBufPtr = NULL; // where in "ram" to allocate the next stream's buffer; if NULL, allocate it dynamically

class BufferedStream
{
protected:
	Node* buf;
	int pos;
public:
	BufferedStream() : pos(0)
	{
		if (streamBufPtr)
		{
			buf = streamBufPtr;
			streamBufPtr += STREAM_BUFFER_SIZE;
			if ((char*)streamBufPtr > (char*)ram + RAM_SIZE)
				error("Out of RAM for stream buffers!");
		}
		else
			buf = new Node[STREAM_BUFFER_SIZE];
	}

	~BufferedStream()
	{
		if (streamBufPtr==NULL) // used dynamic allocation
			delete[] buf;
	}
};

class BufferedOutputStream : BufferedStream
{
	OutputStream s;
public:
	BufferedOutputStream(const char* filename, bool resume=false) : s(filename, resume) { }

	void write(const Node* p, bool verify=false)
	{
		buf[pos++] = *p;
#ifdef DEBUG
		if (verify && pos > 1)
			assert(buf[pos-1] > buf[pos-2], "Output is not sorted");
#endif
		if (pos == STREAM_BUFFER_SIZE)
		{
			flushBuffer();
			pos = 0;
		}
	}

	void flushBuffer()
	{
		s.write(buf, pos);
	}

	void flush()
	{
		flushBuffer();
		s.flush();
	}

	~BufferedOutputStream()
	{
		flushBuffer();
	}
};

class BufferedInputStream : BufferedStream
{
	InputStream s;
	int end;
	uint64_t left;
public:
	BufferedInputStream(const char* filename) : s(filename), end(0)
	{
		left = s.size();
	}

	const Node* read()
	{
		if (pos == end)
		{
			if (left == 0)
				return NULL;
			buffer();
			assert (pos != end);
		}
#ifdef DEBUG
		if (pos > 0) 
			assert(buf[pos-1] <= buf[pos], "Input is not sorted");
#endif
		return &buf[pos++];
	}

	void buffer()
	{
		pos = 0;
		end = s.read(buf, left < STREAM_BUFFER_SIZE ? left : STREAM_BUFFER_SIZE);
		left -= end;
	}

	uint64_t size() { return s.size(); }
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
	while (records = input.read(buffer, amount))
		output.write(buffer, records);
	output.flush(); // force disk flush
}

// ******************************************************************************************************

class InputHeap
{
	struct HeapNode
	{
		const CompressedState* state;
		BufferedInputStream* input;
		INLINE bool operator<(const HeapNode& b) const { return *this->state < *b.state; }
	};

	HeapNode *heap, *head;
	int size;

public:
	InputHeap(BufferedInputStream** inputs, int count)
	{
		size = count;
		heap = new HeapNode[size];
		for (int i=0; i<size; i++)
		{
			heap[i].input = inputs[i];
			heap[i].state = inputs[i]->read();
		}
		std::sort(heap, heap+size);
		head = heap;
		heap--; // heap[0] is now invalid, use heap[1] to heap[size] inclusively; head == heap[1]
		test();
	}

	~InputHeap()
	{
		heap++;
		delete[] heap;
	}

	const CompressedState* getHead() const { return head->state; }

	bool scanTo(const CompressedState& target)
	{
		test();
		if (size == 0)
			return false;
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

void mergeStreams(BufferedInputStream** inputs, int inputCount, BufferedOutputStream* output)
{
	// TODO: use InputHeap
	const CompressedState** states = new const CompressedState*[inputCount];
	CompressedState last;
	memset(&last, 0, sizeof(last));
	
	for (int i=0; i<inputCount; i++)
		states[i] = inputs[i]->read();

	while (true)
	{
		int lowestIndex;
		const CompressedState* lowest = NULL;
		for (int i=0; i<inputCount; i++)
			if (states[i])
				if (lowest == NULL || *states[i] < *lowest)
				{
					lowestIndex = i;
					lowest = states[i];
				}

		if (lowest == NULL) // all done
			return;

		if (*lowest != last)
		{
			output->write(lowest);
			last = *lowest;
		}

		states[lowestIndex] = inputs[lowestIndex]->read();
	}
}

void filterStream(BufferedInputStream* source, BufferedInputStream** inputs, int inputCount, BufferedOutputStream* output)
{
	const CompressedState* sourceState = source->read();
	if (inputCount == 0)
	{
		while (sourceState)
		{
			output->write(sourceState);
			sourceState = source->read();
		}
		return;
	}
	
	InputHeap heap(inputs, inputCount);

	while (sourceState)
	{
		bool b = heap.scanTo(*sourceState);
		if (!b) // EOF of heap sources
		{
			do {
				output->write(sourceState);
				sourceState = source->read();
			} while (sourceState);
			return;
		}
		const CompressedState* head = heap.getHead();
		assert(sourceState);
		while (sourceState && *sourceState < *head)
		{
			output->write(sourceState);
			sourceState = source->read();
		}
		while (sourceState && *sourceState == *head)
			sourceState = source->read();
	}
}

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
#ifdef DEBUG
		assert(*cc < *co);
#endif
		BufferedInputStream *ci = inputs[c];
		do
		{
			output->write(cc, true);
			if (c==0)
				output1->write(cc, true);
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
			output1->write(cc, true);
		cc = ci->read();
	}
}

size_t deduplicate(CompressedState* start, size_t records)
{
	CompressedState *read=start+1, *write=start+1;
	CompressedState *end = start+records;
	while (read < end)
	{
		if (*read != *(read-1))
		{
			*write = *read;
            write++;
		}
        read++;
	}
	return write-start;
}

// ******************************************************************************************************

BufferedOutputStream* queue[MAX_FRAMES];
#ifdef MULTITHREADING
MUTEX queueMutex[MAX_FRAMES];
#endif

#ifdef MULTITHREADING
#define PARTITIONS (CACHE_HASH_SIZE/256)
MUTEX cacheMutex[PARTITIONS];
#endif

void queueState(const CompressedState* state, FRAME frame)
{
#ifdef MULTITHREADING
	SCOPED_LOCK lock(queueMutex[frame]);
#endif
	if (!queue[frame])
		queue[frame] = new BufferedOutputStream(format("open-%u-%u.bin", LEVEL, frame));
	queue[frame]->write(state);
}

void addState(const State* state, FRAME frame)
{
	CompressedState cs;
	state->compress(&cs);
#ifdef DEBUG
	State test = blankState;
	test.decompress(&cs);
	if (!(test == *state))
	{
		printf("%s\n", state->toString());
		printf("%s\n", test.toString());
		error("Compression/decompression failed");
	}
#endif
	uint32_t hash = SuperFastHash((const char*)&cs, sizeof(cs)) % CACHE_HASH_SIZE;
	
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
	for (FRAME f=0; f<MAX_FRAMES; f++)
		if (queue[f])
			queue[f]->flush();
}

// ******************************************************************************************************

FRAME maxFrames, currentFrame;

void preprocessQueue()
{
	// Step 1: read chunks of BUFFER_SIZE nodes, sort them and write them to disk
	int chunks = 0;
	printf("Sorting... "); fflush(stdout);
	{
		InputStream input(format("open-%u-%u.bin", LEVEL, currentFrame));
		uint64_t amount = input.size();
		if (amount > BUFFER_SIZE)
			amount = BUFFER_SIZE;
		size_t records;
		while (records = input.read(buffer, amount))
		{
			std::sort(buffer, buffer + records);
			records = deduplicate(buffer, records);
			OutputStream output(format("chunk-%u-%u-%d.bin", LEVEL, currentFrame, chunks));
			output.write(buffer, records);
			chunks++;
		}
	}

	streamBufPtr = buffer;

	// Step 2: merge + dedup chunks
	printf("Merging... "); fflush(stdout);
	if (chunks>1)
	{
		BufferedInputStream** chunkInput = new BufferedInputStream*[chunks];
		for (int i=0; i<chunks; i++)
			chunkInput[i] = new BufferedInputStream(format("chunk-%u-%u-%d.bin", LEVEL, currentFrame, i));
		BufferedOutputStream* output = new BufferedOutputStream(format("merged-%u-%u.bin", LEVEL, currentFrame));
		mergeStreams(chunkInput, chunks, output);
		for (int i=0; i<chunks; i++)
			delete chunkInput[i];
		delete[] chunkInput;
		delete output;
		for (int i=0; i<chunks; i++)
			deleteFile(format("chunk-%u-%u-%d.bin", LEVEL, currentFrame, i));
	}
	else
	{
		renameFile(format("chunk-%u-%u-%d.bin", LEVEL, currentFrame, 0), format("merged-%u-%u.bin", LEVEL, currentFrame));
	}

	streamBufPtr = buffer;

	// Step 3: dedup against previous frames
	printf("Filtering... "); fflush(stdout);
#ifdef USE_ALL
	if (currentFrame==0)
	{
		copyFile(format("merged-%u-%u.bin", LEVEL, currentFrame), format("closing-%u-%u.bin", LEVEL, currentFrame));
		renameFile(format("merged-%u-%u.bin", LEVEL, currentFrame), format("all-%u.bin", LEVEL));
	}
	else
	{
		BufferedInputStream* source = new BufferedInputStream(format("merged-%u-%u.bin", LEVEL, currentFrame));
		BufferedInputStream* all = new BufferedInputStream(format("all-%u.bin", LEVEL));
		BufferedOutputStream* allnew = new BufferedOutputStream(format("allnew-%u.bin", LEVEL, currentFrame));
		BufferedOutputStream* closed = new BufferedOutputStream(format("closed-%u-%u.bin", LEVEL, currentFrame));
		mergeTwoStreams(source, all, allnew, closed);
		delete all;
		delete source;
		delete allnew;
		delete closed;
		deleteFile(format("all-%u.bin", LEVEL));
		renameFile(format("allnew-%u.bin", LEVEL), format("all-%u.bin", LEVEL));
		deleteFile(format("merged-%u-%u.bin", LEVEL, currentFrame));
	}
#else
	{
		BufferedInputStream* source = new BufferedInputStream(format("merged-%u-%u.bin", LEVEL, currentFrame));
		BufferedInputStream* inputs[MAX_FRAMES];
		int inputCount = 0;
		for (FRAME f=0; f<currentFrame; f++)
			if (fileExists(format("closed-%u-%u.bin", LEVEL, f)))
			{
				BufferedInputStream* input = new BufferedInputStream(format("closed-%u-%u.bin", LEVEL, f));
				if (input->size())
					inputs[inputCount++] = input;
				else
					delete input;
			}
		BufferedOutputStream* output = new BufferedOutputStream(format("closing-%u-%u.bin", LEVEL, currentFrame));
		filterStream(source, inputs, inputCount, output);
		for (int i=0; i<inputCount; i++)
			delete inputs[i];
		delete source;
		output->flush(); // force disk flush
		delete output;
		deleteFile(format("merged-%u-%u.bin", LEVEL, currentFrame));
	}
#endif
	deleteFile(format("open-%u-%u.bin", LEVEL, currentFrame));
	renameFile(format("closing-%u-%u.bin", LEVEL, currentFrame), format("closed-%u-%u.bin", LEVEL, currentFrame));

	streamBufPtr = NULL;
}

// ******************************************************************************************************

BufferedInputStream* currentInput;
#ifdef MULTITHREADING
MUTEX currentInputMutex;
#endif

INLINE bool dequeueNode(CompressedState* state)
{
#ifdef MULTITHREADING
	SCOPED_LOCK lock(currentInputMutex);
#endif
	const CompressedState* res = currentInput->read();
	if (res)
	{
		*state = *res;
		return true;
	}
	else
		return false;
}

// ******************************************************************************************************

INLINE void addStateStep(const State* state, Step step, FRAME frame)
{
	addState(state, frame);
}

#define EXPAND_NAME addChildren
#define EXPAND_HANDLE_CHILD addStateStep
#include "expand.cpp"

// ******************************************************************************************************

bool exitFound;
State exitState;

void worker()
{
	CompressedState cs;
	while (!exitFound && dequeueNode(&cs))
	{
		State s = blankState;
		s.decompress(&cs);
#ifdef DEBUG
		CompressedState test;
		s.compress(&test);
		assert(test == cs, "Compression/decompression failed");
#endif
		if (s.playersLeft()==0)
		{
			exitFound = true;
			exitState = s;
			return;
		}
		addChildren(currentFrame, &s);
	}
}

// ******************************************************************************************************

State exitSearchState;
FRAME exitSearchStateFrame;
Step exitSearchStateStep;
bool exitSearchStateFound = false;

INLINE void checkState(const State* state, Step step, FRAME frame)
{
	if (*state==exitSearchState && frame==exitSearchStateFrame)
	{
		exitSearchStateFound = true;
		exitSearchStateStep = step;
	}
}

#define EXPAND_NAME checkChildren
#define EXPAND_HANDLE_CHILD checkState
#include "expand.cpp"

void traceExit()
{
	Step steps[MAX_STEPS];
	int stepNr = 0;
	{
		exitSearchState = exitState;
		int frame = currentFrame;
		while (frame >= 0)
		{
	nextStep:
			exitSearchStateFound = false;
			exitSearchStateFrame = frame;
			frame -= 9;
			for (; frame >= 0; frame--)
				if (fileExists(format("closed-%u-%d.bin", LEVEL, frame)))
				{
					printf("Frame %d... \r", frame);
					// TODO: parallelize
					InputStream input(format("closed-%u-%d.bin", LEVEL, frame));
					CompressedState cs;
					while (input.read(&cs, 1))
					{
						State state = blankState;
						state.decompress(&cs);
						checkChildren(frame, &state);
						if (exitSearchStateFound)
						{
							printf("\nFound!\n");
							steps[stepNr++] = exitSearchStateStep;
							exitSearchState = state; 
							goto nextStep;
						}
					}
				}
		}
	}

	FILE* f = fopen(format("%u.txt", LEVEL), "wt");
	steps[stepNr].action = NONE;
	steps[stepNr].x = initialState.players[0].x-1;
	steps[stepNr].y = initialState.players[0].y-1;
	unsigned int totalSteps = 0;
	State state = initialState;
	FRAME frame = 0;
	while (stepNr)
	{
		fprintf(f, "@%u,%u: %s\n%s", steps[stepNr].x+1, steps[stepNr].y+1, actionNames[steps[stepNr].action], state.toString());
		totalSteps += (steps[stepNr].action<SWITCH ? 1 : 0) + replayStep(&state, &frame, steps[--stepNr]);
	}
	// last one
	fprintf(f, "@%u,%u: %s\n%s", steps[0].x+1, steps[0].y+1, actionNames[steps[0].action], state.toString());
	fprintf(f, "Total steps: %u", totalSteps);
}

// ******************************************************************************************************

int search()
{
	FRAME startFrame = 0;

	for (FRAME f=MAX_FRAMES; f>0; f--)
		if (fileExists(format("closed-%u-%u.bin", LEVEL, f)))
		{
			printf("Resuming from frame %u\n", f);
			startFrame = f;
			break;
	    }

	for (FRAME f=startFrame; f<MAX_FRAMES; f++)
		if (fileExists(format("open-%u-%u.bin", LEVEL, f)))
		{
			printTime(); printf("Reopening queue for frame %u\n", f);
			queue[f] = new BufferedOutputStream(format("open-%u-%u.bin", LEVEL, f), true);
		}

	if (startFrame==0 && !queue[0])
	{
		CompressedState c;
		initialState.compress(&c);
#ifdef DEBUG
		State test = blankState;
		test.decompress(&c);
		if (!(test == initialState))
		{
			printf("%s\n", initialState.toString());
			printf("%s\n", test.toString());
			error("Compression/decompression failed");
		}
#endif
		queueState(&c, 0);
	}

	for (currentFrame=startFrame; currentFrame<maxFrames; currentFrame++)
	{
		if (fileExists(format("closed-%u-%u.bin", LEVEL, currentFrame)))
		{
			printTime(); printf("Frame %u/%u: (loading closed nodes from disk)               ", currentFrame, maxFrames); fflush(stdout);
		}
		else
		{
			if (!queue[currentFrame])
				continue;
			delete queue[currentFrame];
			queue[currentFrame] = NULL;

			printTime(); printf("Frame %u/%u: ", currentFrame, maxFrames); fflush(stdout);

			preprocessQueue();

			printf("Clearing... "); fflush(stdout);
			memset(ram, 0, RAM_SIZE); // clear cache
		}
		
		currentInput = new BufferedInputStream(format("closed-%u-%u.bin", LEVEL, currentFrame));
		printf("Searching (%llu)... ", currentInput->size()); fflush(stdout);
#ifdef MULTITHREADING
		THREAD threads[THREADS];
		//threadsRunning = THREADS;
		for (int i=0; i<THREADS; i++)
			threads[i] = THREAD_CREATE(&worker);
		for (int i=0; i<THREADS; i++)
		{
			THREAD_JOIN(threads[i]);
			THREAD_DESTROY(threads[i]);
		}
#else
		worker();
#endif
		delete currentInput;
		
#if !defined(PROFILE) && !defined(DEBUG)
		printf("Flushing... "); fflush(stdout);
		flushQueue();
#endif

		printf("Done.\n");

		if (exitFound)
		{
			printf("Exit found, tracing path...\n");
			traceExit();
			return 0;
		}

		if (fileExists(format("stop-%u.txt", LEVEL)))
		{
			printf("Stop file found.\n");
			return 3;
		}
	}
	
	printf("Exit not found.\n");
	return 2;
}

// ******************************************************************************************************

int packOpen()
{
    for (FRAME f=0; f<MAX_FRAMES; f++)
    	if (fileExists(format("open-%u-%u.bin", LEVEL, f)))
    	{
			printTime(); printf("Frame %u: ", f);

			{
				InputStream input(format("open-%u-%u.bin", LEVEL, f));
				OutputStream output(format("openpacked-%u-%u.bin", LEVEL, f));
				uint64_t amount = input.size();
				if (amount > BUFFER_SIZE)
					amount = BUFFER_SIZE;
				size_t records;
				uint64_t read=0, written=0;
				while (records = input.read(buffer, amount))
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
			deleteFile(format("open-%u-%u.bin", LEVEL, f));
			renameFile(format("openpacked-%u-%u.bin", LEVEL, f), format("open-%u-%u.bin", LEVEL, f));
    	}
	return 0;
}

// ******************************************************************************************************

int sample(FRAME f)
{
	const char* fn = format("closed-%u-%u.bin", LEVEL, f);
	if (!fileExists(fn))
		fn = format("open-%u-%u.bin", LEVEL, f);
	if (!fileExists(fn))
		error(format("Can't find neither open nor closed node file for frame %u", f));
	
	InputStream in(fn);
	srand(time(NULL));
	in.seek(rand() % in.size());
	CompressedState cs;
	in.read(&cs, 1);
	State s = blankState;
	s.decompress(&cs);
	puts(s.toString());
	return 0;
}

// ******************************************************************************************************

int countDups(const char* fn1, const char* fn2)
{
	BufferedInputStream i1(fn1), i2(fn2);
	printf("%s: %llu states\n%s: %llu states\n", fn1, i1.size(), fn2, i2.size());
	const CompressedState *cs1, *cs2;
	cs1 = i1.read();
	cs2 = i2.read();
	uint64_t dups = 0;
	while (cs1 && cs2)
	{
		if (*cs1 < *cs2)
			cs1 = i1.read();
		else
		if (*cs1 > *cs2)
			cs2 = i2.read();
		else
		{
			dups++;
			cs1 = i1.read();
			cs2 = i2.read();
		}
	}
	printf("%llu duplicate states\n", dups);
	return 0;
}

// ******************************************************************************************************

timeb startTime;

void printExecutionTime()
{
	timeb endTime;
	ftime(&endTime);
	time_t ms = (endTime.time    - startTime.time)*1000
	       + (endTime.millitm - startTime.millitm);
	printf("Time: %d.%03d seconds.\n", ms/1000, ms%1000);
}

// ***********************************************************************************

enum RunMode
{
	MODE_SEARCH,
	MODE_PACKOPEN,
	MODE_SAMPLE,
	MODE_COUNTDUPS
};

int run(int argc, const char* argv[])
{
	printf("Level %u: %ux%u, %u players\n", LEVEL, X, Y, PLAYERS);

	enforce(sizeof(intptr_t)==sizeof(size_t), "Bad intptr_t!");
	enforce(sizeof(long long)==8, "Bad long long!");

#ifdef HAVE_VALIDATOR
	printf("Level state validator present\n");
#endif

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
	enforce(ram, "RAM allocation failed");
	printf("Using %lld bytes of RAM for %lld cache nodes and %lld buffer nodes\n", (long long)RAM_SIZE, (long long)CACHE_HASH_SIZE, (long long)BUFFER_SIZE);

#if defined(DISK_WINFILES)
	printf("Using Windows API files\n");
#elif defined(DISK_POSIX)
	printf("Using POSIX files\n");
#else
#error Disk plugin not set
#endif

	initialState.load();
	blankState = initialState;
	blankState.blank();

	maxFrames = MAX_FRAMES;
	RunMode runMode = MODE_SEARCH;
	FRAME sampleFrame;

	if (argc>1)
	{
		if (strcmp(argv[1], "pack-open")==0)
		{
			runMode = MODE_PACKOPEN;
			enforce(argc==2, "Too many arguments");
		}
		else
		if (strcmp(argv[1], "sample")==0)
		{
			runMode = MODE_SAMPLE;
			enforce(argc==3, "Specify a frame number to sample");
			sampleFrame = strtol(argv[2], NULL, 10);
		}
		else
		if (strcmp(argv[1], "count-dups")==0)
		{
			runMode = MODE_COUNTDUPS;
			enforce(argc==4, "Specify two files to compare");
		}
		else
			maxFrames = strtol(argv[1], NULL, 10);
	}

	ftime(&startTime);
	atexit(&printExecutionTime);

	int result;
	switch (runMode)
	{
		case MODE_SEARCH:
			result = search();
			break;
		case MODE_PACKOPEN:
			result = packOpen();
			break;
		case MODE_SAMPLE:
			result = sample(sampleFrame);
			break;
		case MODE_COUNTDUPS:
			result = countDups(argv[2], argv[3]);
			break;
	}
	//dumpNodes();
	return result;
}

// ***********************************************************************************

int main(int argc, const char* argv[]) { try { return run(argc, argv); } catch(const char* s) { printf("\n%s\n", s); return 1; } }
//#include "test_body.cpp"
