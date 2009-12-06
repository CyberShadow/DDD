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

#define STREAM_BUFFER_SIZE (1024*1024 / sizeof(Node))

class BufferedOutputStream
{
	OutputStream s;
	Node buf[STREAM_BUFFER_SIZE];
	int pos;
public:
	BufferedOutputStream(const char* filename) : s(filename), pos(0) { }

	void write(const Node* p)
	{
		buf[pos++] = *p;
		if (pos == STREAM_BUFFER_SIZE)
		{
			flush();
			pos = 0;
		}
	}

	void flush()
	{
		s.write(buf, pos);
	}

	~BufferedOutputStream()
	{
		flush();
	}
};

class BufferedInputStream
{
	InputStream s;
	Node buf[STREAM_BUFFER_SIZE];
	int pos, end;
	size_t left;
public:
	BufferedInputStream(const char* filename) : s(filename), pos(0), end(0) 
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
		return &buf[pos++];
	}

	void buffer()
	{
		pos = 0;
		end = s.read(buf, left < STREAM_BUFFER_SIZE ? left : STREAM_BUFFER_SIZE);
		left -= end;
	}

	size_t size() { return s.size(); }
};

// ******************************************************************************************************

void* ram = malloc(RAM_SIZE);

struct CacheNode
{
	CompressedState state;
	uint16_t frame;
};

const size_t CACHE_HASH_SIZE = RAM_SIZE / sizeof(CacheNode) / NODES_PER_HASH;
CacheNode (*cache)[NODES_PER_HASH] = (CacheNode (*)[NODES_PER_HASH]) ram;

const size_t BUFFER_SIZE = RAM_SIZE / sizeof(Node);
Node* buffer = (Node*) ram;

// ******************************************************************************************************

void printTime()
{
	time_t t;
	time(&t);
	char* tstr = ctime(&t);
	tstr[strlen(tstr)-1] = 0;
	printf("[%s] ", tstr);
}

// ******************************************************************************************************

void mergeStreams(BufferedInputStream** inputs, int inputCount, BufferedOutputStream* output)
{
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

void filterStream(BufferedInputStream** inputs, int inputCount, BufferedOutputStream* output)
{
	const CompressedState** states = new const CompressedState*[inputCount];
	
	for (int i=0; i<inputCount; i++)
		states[i] = inputs[i]->read();

	while (states[0])
	{
		int lowestIndex;
		const CompressedState* lowest = NULL;
		for (int i=1; i<inputCount; i++)
			if (states[i])
				if (lowest == NULL || *states[i] < *lowest)
				{
					lowestIndex = i;
					lowest = states[i];
				}

	recheck:
		if (lowest == NULL || *states[0] < *lowest) // advance source
		{
			output->write(states[0]);
			states[0] = inputs[0]->read();
			if (!states[0])
				break;
			goto recheck;
		}
		else
		if (*states[0] == *lowest) // advance both
		{
			states[0] = inputs[0]->read();
			states[lowestIndex] = inputs[lowestIndex]->read();
		}
		else // advance other
			states[lowestIndex] = inputs[lowestIndex]->read();
	}
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
		queue[frame] = new BufferedOutputStream(format("open-%d-%d.bin", LEVEL, frame));
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
				nodes[0].frame = frame;
				return;
			}
		
		// new node
		memmove(nodes+1, nodes, (NODES_PER_HASH-1) * sizeof(CacheNode));
		nodes[0].frame = frame;
		nodes[0].state = cs;
	}
	queueState(&cs, frame);
}

// ******************************************************************************************************

FRAME maxFrames, currentFrame;
bool frameHasNodes[MAX_FRAMES];

void preprocessQueue()
{
	// Step 1: read chunks of BUFFER_SIZE nodes, sort them and write them to disk
	int chunks = 0;
	printf("Sorting... "); fflush(stdout);
	{
		InputStream input(format("open-%d-%d.bin", LEVEL, currentFrame));
		size_t amount = input.size();
		if (amount > BUFFER_SIZE)
			amount = BUFFER_SIZE;
		size_t records;
		while (records = input.read(buffer, amount))
		{
			std::sort(buffer, buffer + records);
			OutputStream output(format("chunk-%d-%d-%d.bin", LEVEL, currentFrame, chunks));
			output.write(buffer, records);
			chunks++;
		}
	}
	deleteFile(format("open-%d-%d.bin", LEVEL, currentFrame));

	// Step 2: merge + dedup chunks
	printf("Merging... "); fflush(stdout);
	{
		BufferedInputStream** chunkInput = new BufferedInputStream*[chunks];
		for (int i=0; i<chunks; i++)
			chunkInput[i] = new BufferedInputStream(format("chunk-%d-%d-%d.bin", LEVEL, currentFrame, i));
		BufferedOutputStream output(format("merged-%d-%d.bin", LEVEL, currentFrame));
		mergeStreams(chunkInput, chunks, &output);
		for (int i=0; i<chunks; i++)
			delete chunkInput[i];
		delete[] chunkInput;
	}
	for (int i=0; i<chunks; i++)
		deleteFile(format("chunk-%d-%d-%d.bin", LEVEL, currentFrame, i));

	// Step 3: dedup against previous frames
	printf("Filtering... "); fflush(stdout);
	{
		BufferedInputStream* inputs[MAX_FRAMES+1];
		inputs[0] = new BufferedInputStream(format("merged-%d-%d.bin", LEVEL, currentFrame));
		int inputCount = 1;
		for (FRAME f=0; f<currentFrame; f++)
			if (frameHasNodes[f])
				inputs[inputCount++] = new BufferedInputStream(format("closed-%d-%u.bin", LEVEL, f));
		BufferedOutputStream output(format("closed-%d-%d.bin", LEVEL, currentFrame));
		filterStream(inputs, inputCount, &output);
		for (int i=0; i<inputCount; i++)
			delete inputs[i];
	}
	deleteFile(format("merged-%d-%d.bin", LEVEL, currentFrame));
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
				if (frameHasNodes[frame])
				{
					printf("Frame %d... \r", frame);
					// TODO: parallelize
					InputStream input(format("closed-%d-%d.bin", LEVEL, frame));
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

	FILE* f = fopen(format("%d.txt", LEVEL), "wt");
	steps[stepNr].action = NONE;
	steps[stepNr].x = initialState.players[0].x-1;
	steps[stepNr].y = initialState.players[0].y-1;
	unsigned int totalSteps = 0;
	State state = initialState;
	FRAME frame = 0;
	while (stepNr)
	{
		fprintf(f, "@%d,%d: %s\n%s", steps[stepNr].x+1, steps[stepNr].y+1, actionNames[steps[stepNr].action], state.toString());
		totalSteps += (steps[stepNr].action<SWITCH ? 1 : 0) + replayStep(&state, &frame, steps[--stepNr]);
	}
	// last one
	fprintf(f, "@%d,%d: %s\n%s", steps[0].x+1, steps[0].y+1, actionNames[steps[0].action], state.toString());
	fprintf(f, "Total steps: %d", totalSteps);
}

void cleanUp()
{
	for (FRAME f=0; f<=currentFrame; f++)
		if (frameHasNodes[f])
			deleteFile(format("closed-%d-%d.bin", LEVEL, f));
	for (FRAME f=currentFrame; f<MAX_FRAMES; f++)
		if (queue[f])
		{
			delete queue[f];
			deleteFile(format("open-%d-%d.bin", LEVEL, f));
		}
}

// ******************************************************************************************************

int search()
{
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
	
	for (currentFrame=0;currentFrame<maxFrames;currentFrame++)
	{
		if (!queue[currentFrame])
			continue;
		delete queue[currentFrame];
		queue[currentFrame] = NULL;

		printTime(); printf("Frame %d/%d: ", currentFrame, maxFrames); fflush(stdout);

		preprocessQueue();

		currentInput = new BufferedInputStream(format("closed-%d-%d.bin", LEVEL, currentFrame));
		
		printf("Clearing... "); fflush(stdout);
		memset(ram, 0, RAM_SIZE); // clear cache
		
		printf("Searching... "); fflush(stdout);
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
		printf("Done.\n"); fflush(stdout);
		delete currentInput;
		frameHasNodes[currentFrame] = true;

		if (exitFound)
		{
			printf("Exit found, tracing path...\n");
			traceExit();
			cleanUp();
			return 0;
		}
	}
	
	printf("Exit not found.\n");
	cleanUp();
	return 2;
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

int run(int argc, const char* argv[])
{
	printf("Level %d: %dx%d, %d players\n", LEVEL, X, Y, PLAYERS);

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
	printf("Using %d Boost threads ", THREADS);
#elif defined(THREAD_WINAPI)
	printf("Using %d WinAPI threads ", THREADS);
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
	
	printf("Compressed state is %d bytes\n", sizeof(CompressedState));
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
	if (argc>2)
		error("Too many arguments");
	if (argc==2)
		maxFrames = strtol(argv[1], NULL, 10);

	ftime(&startTime);
	atexit(&printExecutionTime);

	int result = search();
	//dumpNodes();
	return result;
}

// ***********************************************************************************

int main(int argc, const char* argv[]) { return run(argc, argv); }
//#include "test_body.cpp"
