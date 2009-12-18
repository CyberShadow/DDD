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
typedef uint32_t FRAME_GROUP;
typedef uint16_t PACKED_FRAME;

//#pragma pack(1)
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

class BufferedStream
{
protected:
	Node* buf;
	int pos;
public:
	BufferedStream() : pos(0)
	{
		buf = new Node[STREAM_BUFFER_SIZE];
	}

	~BufferedStream()
	{
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
			flushBuffer();
	}

	void flushBuffer()
	{
		s.write(buf, pos);
		pos = 0;
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
	uint64_t left; // TODO: get rid of left
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
			assert(buf[pos-1] < buf[pos], "Input is not sorted");
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
			if (heap[i].state==NULL)
				i--, size--;
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
	BufferedInputStream* getHeadInput() const { return head->input; }

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
	InputHeap heap(inputs, inputCount);

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
			if (cs.subframe > cs2.subframe) // in case of duplicate frames, pick the one from the smallest frame
				cs.subframe = cs2.subframe;
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
void filterStream(BufferedInputStream* source, BufferedInputStream** inputs, int inputCount, BufferedOutputStream* output)
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
	
	InputHeap heap(inputs, inputCount);

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
#ifdef DEBUG
		assert(*cc < *co);
#endif
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
			if ((write-1)->subframe > read->subframe)
				(write-1)->subframe = read->subframe;
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
	return format("%s-%u.bin", name, LEVEL);
}

const char* formatFileName(const char* name, FRAME_GROUP g)
{
	return format("%s-%u-%ux.bin", name, LEVEL, g);
}

const char* formatFileName(const char* name, FRAME_GROUP g, unsigned chunk)
{
	return format("%s-%u-%ux-%u.bin", name, LEVEL, g, chunk);
}

// ******************************************************************************************************

#define MAX_FRAME_GROUPS ((MAX_FRAMES+9)/10)

BufferedOutputStream* queue[MAX_FRAME_GROUPS];
#ifdef MULTITHREADING
MUTEX queueMutex[MAX_FRAME_GROUPS];
#endif

#ifdef MULTITHREADING
#define PARTITIONS (CACHE_HASH_SIZE/256)
MUTEX cacheMutex[PARTITIONS];
#endif

void queueState(CompressedState* state, FRAME frame)
{
	FRAME_GROUP group = frame/10;
	state->subframe = frame%10;
#ifdef MULTITHREADING
	SCOPED_LOCK lock(queueMutex[group]);
#endif
	if (!queue[group])
		queue[group] = new BufferedOutputStream(formatFileName("open", group));
	queue[group]->write(state);
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
	for (FRAME_GROUP g=0; g<MAX_FRAME_GROUPS; g++)
		if (queue[g])
			queue[g]->flush();
}

// ******************************************************************************************************

FRAME_GROUP firstFrameGroup, maxFrameGroups;
FRAME_GROUP currentFrameGroup;

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
FRAME exitFrame;
State exitState;

void processState(const CompressedState* cs)
{
	State s = blankState;
	s.decompress(cs);
#ifdef DEBUG
	CompressedState test;
	s.compress(&test);
	assert(test == *cs, "Compression/decompression failed");
#endif
	FRAME currentFrame = currentFrameGroup*10 + cs->subframe;
	if (s.playersLeft()==0)
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
	addChildren(currentFrame, &s);
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

void dequeueNode(CompressedState* state)
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
		dequeueNode(&cs);
		processState(&cs);
	}
}

void flushProcessQueue()
{
	SCOPED_LOCK lock(processQueueMutex);

	while (processQueueHead != processQueueTail) // while not empty
		CONDITION_WAIT(processQueueReadCondition, lock);
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
				printf("Frame group %dx... \r", frameGroup);
				// TODO: parallelize?
				InputStream input(formatFileName("closed", frameGroup));
				CompressedState cs;
				while (input.read(&cs, 1))
				{
					State state = blankState;
					state.decompress(&cs);
					FRAME frame = frameGroup*10 + cs.subframe;
					checkChildren(frame, &state);
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
	firstFrameGroup = 0;

	for (FRAME_GROUP g=MAX_FRAME_GROUPS; g>0; g--)
		if (fileExists(formatFileName("closed", g)))
		{
			printf("Resuming from frame group %ux\n", g+1);
			firstFrameGroup = g+1;
			break;
	    }

	for (FRAME_GROUP g=firstFrameGroup; g<MAX_FRAME_GROUPS; g++)
		if (fileExists(formatFileName("open", g)))
		{
			printTime(); printf("Reopening queue for frame group %ux\n", g);
			queue[g] = new BufferedOutputStream(formatFileName("open", g), true);
		}

	if (firstFrameGroup==0 && !queue[0])
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

		printTime(); printf("Frame group %ux/%ux: ", currentFrameGroup, maxFrameGroups); fflush(stdout);

		if (fileExists(formatFileName("merged", currentFrameGroup)))
		{
			printf("(reopening merged)    ");
		}
		else
		{
			// Step 1: read chunks of BUFFER_SIZE nodes, sort+dedup them in RAM and write them to disk
			int chunks = 0;
			printf("Sorting... "); fflush(stdout);
			{
				InputStream input(formatFileName("open", currentFrameGroup));
				uint64_t amount = input.size();
				if (amount > BUFFER_SIZE)
					amount = BUFFER_SIZE;
				size_t records;
				while (records = input.read(buffer, amount))
				{
					std::sort(buffer, buffer + records);
					records = deduplicate(buffer, records);
					OutputStream output(formatFileName("chunk", currentFrameGroup, chunks));
					output.write(buffer, records);
					chunks++;
				}
			}

			// Step 2: merge + dedup chunks
			printf("Merging... "); fflush(stdout);
			if (chunks>1)
			{
				BufferedInputStream** chunkInput = new BufferedInputStream*[chunks];
				for (int i=0; i<chunks; i++)
					chunkInput[i] = new BufferedInputStream(formatFileName("chunk", currentFrameGroup, i));
				BufferedOutputStream* output = new BufferedOutputStream(formatFileName("merging", currentFrameGroup));
				mergeStreams(chunkInput, chunks, output);
				for (int i=0; i<chunks; i++)
					delete chunkInput[i];
				delete[] chunkInput;
				output->flush();
				delete output;
				renameFile(formatFileName("merging", currentFrameGroup), formatFileName("merged", currentFrameGroup));
				for (int i=0; i<chunks; i++)
					deleteFile(formatFileName("chunk", currentFrameGroup, i));
			}
			else
			{
				renameFile(formatFileName("chunk", currentFrameGroup, 0), formatFileName("merged", currentFrameGroup));
			}
		}

		printf("Clearing... "); fflush(stdout);
		memset(ram, 0, RAM_SIZE); // clear cache

		// Step 3: dedup against previous frames, while simultaneously processing filtered nodes
		printf("Processing... "); fflush(stdout);
#ifdef USE_ALL
		if (currentFrame==0)
		{
			copyFile(formatFileName("merged", currentFrameGroup), formatFileName("closing", currentFrameGroup));
			renameFile(formatFileName("merged", currentFrameGroup), formatFileName("all"));
		}
		else
		{
			BufferedInputStream* source = new BufferedInputStream(formatFileName("merged", currentFrameGroup));
			BufferedInputStream* all = new BufferedInputStream(formatFileName("all"));
			BufferedOutputStream* allnew = new BufferedOutputStream(formatFileName("allnew"));
			BufferedOutputStream* closed = new BufferedOutputStream(formatFileName("closed", currentFrameGroup));
			mergeTwoStreams<ProcessStateHandler>(source, all, allnew, closed);
			delete all;
			delete source;
			delete allnew;
			delete closed;
			deleteFile(formatFileName("all"));
			renameFile(formatFileName("allnew"), formatFileName("all"));
			deleteFile(formatFileName("merged", currentFrameGroup));
		}
#else
		{
			BufferedInputStream* source = new BufferedInputStream(formatFileName("merged", currentFrameGroup));
			BufferedInputStream* inputs[MAX_FRAME_GROUPS];
			int inputCount = 0;
			for (FRAME_GROUP g=0; g<currentFrameGroup; g++)
				if (fileExists(formatFileName("closed", g)))
				{
					BufferedInputStream* input = new BufferedInputStream(formatFileName("closed", g));
					if (input->size())
						inputs[inputCount++] = input;
					else
						delete input;
				}
			BufferedOutputStream* output = new BufferedOutputStream(formatFileName("closing", currentFrameGroup));
			filterStream<ProcessStateHandler>(source, inputs, inputCount, output);
			for (int i=0; i<inputCount; i++)
				delete inputs[i];
			delete source;
			output->flush(); // force disk flush
			delete output;
			deleteFile(formatFileName("merged", currentFrameGroup));
		}
#endif

#ifdef MULTITHREADING
		flushProcessQueue();
#endif

#if !defined(PROFILE) && !defined(DEBUG)
		printf("Flushing... "); fflush(stdout);
		flushQueue();
#endif

		deleteFile(formatFileName("open", currentFrameGroup));
		renameFile(formatFileName("closing", currentFrameGroup), formatFileName("closed", currentFrameGroup));
		
		printf("Done.\n");

		if (exitFound)
		{
			printf("Exit found (at frame %u), tracing path...\n", exitFrame);
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
    for (FRAME_GROUP g=0; g<MAX_FRAME_GROUPS; g++)
    	if (fileExists(formatFileName("open", g)))
    	{
			printTime(); printf("Frame group %ux: ", g);

			{
				InputStream input(formatFileName("open", g));
				OutputStream output(formatFileName("openpacked", g));
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
			deleteFile(formatFileName("open", g));
			renameFile(formatFileName("openpacked", g), formatFileName("open", g));
    	}
	return 0;
}

// ******************************************************************************************************

int sample(FRAME_GROUP g)
{
	printf("Sampling frame group %ux:\n", g);
	const char* fn = formatFileName("closed", g);
	if (!fileExists(fn))
		fn = formatFileName("open", g);
	if (!fileExists(fn))
		error(format("Can't find neither open nor closed node file for frame group %ux", g));
	
	InputStream in(fn);
	srand(time(NULL));
	in.seek(((uint64_t)rand() + ((uint64_t)rand()<<32)) % in.size());
	CompressedState cs;
	in.read(&cs, 1);
	printf("Frame %u:\n", g*10 + cs.subframe);
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

// This works only if the size of CompressedState is the same as the old version (without the subframe field).

// HACK: the following code uses pointer arithmetics with BufferedInputStream objects to quickly determine the subframe from which a CompressedState came from.

void convertMerge(BufferedInputStream* inputBase, BufferedInputStream** inputs, int inputCount, BufferedOutputStream* output)
{
	InputHeap heap(inputs, inputCount);
	//uint64_t* positions = new uint64_t[inputCount];
	//for (int i=0; i<inputCount; i++)
	//	positions[i] = 0;

	CompressedState cs = *heap.getHead();
	debug_assert(heap.getHeadInput() >= inputBase && heap.getHeadInput() < inputBase+10);
	cs.subframe = heap.getHeadInput() - inputBase;
	bool oooFound = false, equalFound = false;
	while (heap.next())
	{
		CompressedState cs2 = *heap.getHead();
		debug_assert(heap.getHeadInput() >= inputBase && heap.getHeadInput() < inputBase+10);
		uint8_t subframe = heap.getHeadInput() - inputBase;
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
		BufferedInputStream* inputs = (BufferedInputStream*) calloc(10, sizeof(BufferedInputStream));
		BufferedInputStream* inputPtrs[10];
		int inputCount = 0;
		for (FRAME f=g*10; f<(g+1)*10; f++)
			if (fileExists(format("closed-%u-%u.bin", LEVEL, f)))
				inputPtrs[inputCount++] = new(&inputs[f%10]) BufferedInputStream(format("closed-%u-%u.bin", LEVEL, f)),
				haveClosed = true;
			else
			if (fileExists(format("open-%u-%u.bin", LEVEL, f)))
				inputPtrs[inputCount++] = new(&inputs[f%10]) BufferedInputStream(format("open-%u-%u.bin", LEVEL, f)),
				haveOpen = true;
		if (haveOpen || haveClosed)
		{
			printf("%dx...\n", g);
			{
				BufferedOutputStream output(formatFileName("converting", g));
				convertMerge(inputs, inputPtrs, inputCount, &output);
			}
			renameFile(formatFileName("converting", g), format("%s-%u-%ux.bin", haveOpen ? "open" : "closed", LEVEL, g));
		}
		for (int i=0; i<inputCount; i++)
			inputPtrs[i]->~BufferedInputStream();
		free(inputs);
	}
	return 0;
}

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
		if (cs2->subframe > 9)
			error("Invalid subframe (corrupted data?)");
		cs = *cs2;
		if (equalFound && oooFound)
			return 0;
	}
}

// ******************************************************************************************************

int count()
{
	for (FRAME_GROUP g=firstFrameGroup; g<maxFrameGroups; g++)
		if (fileExists(formatFileName("closed", g)))
		{
			printTime(); printf("Frame group %ux:\n", g);
			BufferedInputStream input(formatFileName("closed", g));
			const CompressedState* cs;
			uint64_t counts[10] = {0};
			while (cs = input.read())
				counts[cs->subframe]++;
			for (int i=0; i<10; i++)
				if (counts[i])
					printf("Frame %u: %llu\n", g*10+i, counts[i]);
			fflush(stdout);
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
			printTime(); printf("Frame group %ux\n", g); fflush(stdout);
			BufferedInputStream input(formatFileName("closed", g));
			BufferedOutputStream** outputs = new BufferedOutputStream*[10];
			for (int i=0; i<10; i++)
				outputs[i] = new BufferedOutputStream(format("closed-%u-%u.bin", LEVEL, g*10+i));
			const CompressedState* cs;
			while (cs = input.read())
			{
				CompressedState cs2 = *cs;
				cs2.subframe = 0;
				outputs[cs->subframe]->write(&cs2);
			}
			for (int i=0; i<10; i++)
				delete outputs[i];
			delete[] outputs;
		}
	return 0;
}

// ******************************************************************************************************

// Filters open node lists without expanding nodes.

int filterOpen()
{
	for (currentFrameGroup=firstFrameGroup; currentFrameGroup<maxFrameGroups; currentFrameGroup++)
	{
		if (!fileExists(formatFileName("open", currentFrameGroup)))
			continue;

		printTime(); printf("Frame group %ux/%ux: ", currentFrameGroup, maxFrameGroups); fflush(stdout);

		uint64_t initialSize, finalSize;

		if (fileExists(formatFileName("merged", currentFrameGroup)))
		{
			printf("(reopening merged)    ");
			InputStream s(formatFileName("merged", currentFrameGroup));
			initialSize = s.size();
		}
		else
		{
			// Step 1: read chunks of BUFFER_SIZE nodes, sort+dedup them in RAM and write them to disk
			int chunks = 0;
			printf("Sorting... "); fflush(stdout);
			{
				InputStream input(formatFileName("open", currentFrameGroup));
				uint64_t amount = input.size();
				initialSize = amount;
				if (amount > BUFFER_SIZE)
					amount = BUFFER_SIZE;
				size_t records;
				while (records = input.read(buffer, amount))
				{
					std::sort(buffer, buffer + records);
					records = deduplicate(buffer, records);
					OutputStream output(formatFileName("chunk", currentFrameGroup, chunks));
					output.write(buffer, records);
					chunks++;
				}
			}

			// Step 2: merge + dedup chunks
			printf("Merging... "); fflush(stdout);
			if (chunks>1)
			{
				BufferedInputStream** chunkInput = new BufferedInputStream*[chunks];
				for (int i=0; i<chunks; i++)
					chunkInput[i] = new BufferedInputStream(formatFileName("chunk", currentFrameGroup, i));
				BufferedOutputStream* output = new BufferedOutputStream(formatFileName("merging", currentFrameGroup));
				mergeStreams(chunkInput, chunks, output);
				for (int i=0; i<chunks; i++)
					delete chunkInput[i];
				delete[] chunkInput;
				output->flush();
				delete output;
				renameFile(formatFileName("merging", currentFrameGroup), formatFileName("merged", currentFrameGroup));
				for (int i=0; i<chunks; i++)
					deleteFile(formatFileName("chunk", currentFrameGroup, i));
			}
			else
			{
				renameFile(formatFileName("chunk", currentFrameGroup, 0), formatFileName("merged", currentFrameGroup));
			}
		}

		// Step 3: dedup against previous frames
		printf("Filtering... "); fflush(stdout);
#ifdef USE_ALL
		#error "Not implemented"
#else
		{
			class NullStateHandler
			{
			public:
				INLINE static void handle(const CompressedState* state) {}
			};

			BufferedInputStream* source = new BufferedInputStream(formatFileName("merged", currentFrameGroup));
			BufferedInputStream* inputs[MAX_FRAME_GROUPS];
			int inputCount = 0;
			for (FRAME_GROUP g=0; g<currentFrameGroup; g++)
			{
				const char* fn = formatFileName("closed", g);
				if (!fileExists(fn))
					fn = formatFileName("open", g);
				if (fileExists(fn))
				{
					BufferedInputStream* input = new BufferedInputStream(fn);
					if (input->size())
						inputs[inputCount++] = input;
					else
						delete input;
				}
			}
			BufferedOutputStream* output = new BufferedOutputStream(formatFileName("filtering", currentFrameGroup));
			filterStream<NullStateHandler>(source, inputs, inputCount, output);
			for (int i=0; i<inputCount; i++)
				delete inputs[i];
			delete source;
			output->flush(); // force disk flush
			delete output;
			deleteFile(formatFileName("merged", currentFrameGroup));
		}
#endif

		deleteFile(formatFileName("open", currentFrameGroup));
		renameFile(formatFileName("filtering", currentFrameGroup), formatFileName("open", currentFrameGroup));

		{
			InputStream s(formatFileName("open", currentFrameGroup));
			finalSize = s.size();
		}

		printf("Done: %lld -> %lld.\n", initialSize, finalSize);

		if (fileExists(format("stop-%u.txt", LEVEL)))
		{
			printf("Stop file found.\n");
			return 3;
		}
	}
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

int parseInt(const char* str)
{
	int result;
	if (!sscanf(str, "%d", &result))
		error("Conversion");
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
	enforce(COMPRESSED_BITS <= (sizeof(CompressedState)-1)*8);
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

	initialState.load();
	blankState = initialState;
	blankState.blank();

	maxFrameGroups = MAX_FRAME_GROUPS;

	ftime(&startTime);
	atexit(&printExecutionTime);
	int result;

	if (argc>1 && strcmp(argv[1], "pack-open")==0)
	{
		enforce(argc==2, "Too many arguments");
		result = packOpen();
	}
	else
	if (argc>1 && strcmp(argv[1], "sample")==0)
	{
		enforce(argc==3, "Specify a frame number to sample");
		result = sample(parseInt(argv[2]));
	}
	else
	if (argc>1 && strcmp(argv[1], "count-dups")==0)
	{
		enforce(argc==4, "Specify two files to compare");
		result = countDups(argv[2], argv[3]);
	}
	else
	if (argc>1 && strcmp(argv[1], "convert")==0)
	{
		parseFrameRange(argc-2, argv+2);
		result = convert();
	}
	else
	if (argc>1 && strcmp(argv[1], "verify")==0)
	{
		enforce(argc==3, "Specify a file to verify");
		result = verify(argv[2]);
	}
	else
	if (argc>1 && strcmp(argv[1], "count")==0)
	{
		parseFrameRange(argc-2, argv+2);
		result = count();
	}
	else
	if (argc>1 && strcmp(argv[1], "unpack")==0)
	{
		parseFrameRange(argc-2, argv+2);
		result = unpack();
	}
	if (argc>1 && strcmp(argv[1], "filter-open")==0)
	{
		parseFrameRange(argc-2, argv+2);
		result = filterOpen();
	}
	else
	{
		if (argc>1)
		{
			int maxFrames = parseInt(argv[1]);
			enforce(maxFrames%10 == 0, "Number of frames must be divisible by 10");
			maxFrameGroups = maxFrames / 10;
		}
		result = search();
	}

	return result;
}

// ***********************************************************************************

int main(int argc, const char* argv[]) { try { return run(argc, argv); } catch(const char* s) { printf("\n%s\n", s); return 1; } }
//#include "test_body.cpp"
