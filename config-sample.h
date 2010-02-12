// DEBUG enables asserts and some other debug checks.
//#define DEBUG

// Which problem to solve?
#define PROBLEM SampleMaze

// Add problem settings (such as level to solve) here.
//#define POWER_LEVEL 9001

// Use this in combination with DISK_WINFILES to achieve more efficient disk I/O when the data set has gotten very large (however, this is slower with small data sets)
//#define USE_UNBUFFERED_DISK_IO
#define DISK_IO_CHUNK_SIZE (16*1024*1024)

// MULTITHREADING will enable threading and synchronization code.
#define MULTITHREADING
#define THREADS (1+4)

// THREAD_* defines how will threads be created.
#define THREAD_BOOST
//#define THREAD_WINAPI

// SYNC_* selects the synchronization (mutex and condition) methods.
//#define SYNC_BOOST
//#define SYNC_WINAPI
//#define SYNC_WINAPI_SPIN
#define SYNC_INTEL_SPIN

// How many bytes of RAM to use?
#define RAM_SIZE (8LL*1024*1024*1024)

// How many bytes to use for file stream buffers?
#define STANDARD_BUFFER_SIZE  (  1*1024*1024 / sizeof(Node)) // allocated separately in heap - used for open node files and other files
#define CLOSED_IN_BUFFER_SIZE ( 16*1024*1024 / sizeof(Node)) // for input to the Expanding phase

// The expected ratio of Merging output/input
#define EXPECTED_MERGING_RATIO 0.99


// The parameters for the multithreaded Expansion queue/scheduler

//#define DEBUG_EXPANSION
//#define ENABLE_EXPANSION_SPILLOVER // buggy, drops a small number of nodes during Expansion; leave this disabled for now unless working on fixing it

#ifdef DEBUG_EXPANSION
#define EXPANSION_NODES_PER_QUEUE_ELEMENT (RAM_SIZE / sizeof(OpenNode) / 256)
#else
#define EXPANSION_NODES_PER_QUEUE_ELEMENT 0x1000
#endif

#ifdef ENABLE_EXPANSION_SPILLOVER
#define EXPANSION_BUFFER_FILL_RATIO 0.98
#define SPILLOVER_CHUNK_SIZE (2LL*1024*1024*1024)
#else
#define EXPANSION_BUFFER_FILL_RATIO (1./WORKERS)
#endif


// DISK_* selects the back-end storage to be used for the node disk files.
#define DISK_WINFILES
//#define DISK_POSIX

// This option disables flushing files to disk (fflush/FlushFileBuffers).
// Turning this on will speed up search, but will likely cause data loss in case of system crash or power failure.
#ifdef DEBUG
#define NO_DISK_FLUSH
#endif
