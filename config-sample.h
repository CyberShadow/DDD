// DEBUG enables asserts and some other debug checks.
//#define DEBUG

// Which problem to solve?
#define PROBLEM SampleMaze

// Add problem settings (such as level to solve) here.
//#define POWER_LEVEL 9001

// MULTITHREADING will enable threading and synchronization code.
#define MULTITHREADING
#define THREADS 7

// THREAD_* defines how will threads be created.
#define THREAD_BOOST
//#define THREAD_WINAPI

// SYNC_* selects the synchronization (mutex and condition) methods.
//#define SYNC_BOOST
//#define SYNC_WINAPI
//#define SYNC_WINAPI_SPIN
#define SYNC_INTEL_SPIN

// How much bytes of RAM to use?
#define RAM_SIZE (8LL*1024*1024*1024)

// How many bytes to use for each file stream buffer?
#define STANDARD_BUFFER_SIZE (1*1024*1024 / sizeof(Node))
#define MERGING_BUFFER_SIZE  (16*1024*1024 / sizeof(Node))
#define ALL_FILE_BUFFER_SIZE (64*1024*1024 / sizeof(Node))

// How many nodes to group under a single hash in the cache? Higher values reduce speed but allow better distribution.
#define NODES_PER_HASH 4

// DISK_* selects the back-end storage to be used for the node disk files.
#define DISK_WINFILES
//#define DISK_POSIX

// This option disables flushing files to disk (fflush/FlushFileBuffers).
// Turning this on will speed up search, but will likely cause data loss in case of system crash or power failure.
#ifdef DEBUG
#define NO_DISK_FLUSH
#endif

// Keep a file with all nodes to filter against, instead of filtering against each individual frame. 
// The "all" file contains the sorted contents of all "closed" nodes, and needs to be rewritten every frame/frame-group.
// Enable USE_ALL to use less RAM/CPU and to optimize for sequential disk I/O. Disable USE_ALL if random disk access is very fast.
#define USE_ALL

// When free disk space falls below this amount, try to free up space by filtering open node files. If not defined, free space is not checked.
//#define FREE_SPACE_THRESHOLD (50LL*1024*1024*1024)
