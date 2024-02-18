// DEBUG enables asserts and some other debug checks.
//#define DEBUG
//#define KEEP_PAST_FILES
//#define USE_MEMCMP
#define PRINT_RUNNING_TOTAL_TIME

// Which problem to solve?
#define PROBLEM SampleMaze

// Specify the level to solve here.
//#define LEVEL 18

// An unfinished feature, work in progress; sort new nodes as they are added; eliminates the need to do sorting as a postprocessing step
// Needs to be supported by PROBLEM; should result in a huge speed boost
//#define USE_TRANSFORM_INVARIANT_SORTING

// Use this in combination with DISK_WINFILES to achieve more efficient disk I/O when the data set has gotten very large (however, this is slower with small data sets)
//#define USE_UNBUFFERED_DISK_IO
#define DISK_IO_CHUNK_SIZE (16*1024*1024)

// MULTITHREADING will enable threading and synchronization code.
#define MULTITHREADING
#define THREADS (1+4)
#define QUEUE_CHUNK_SIZE 256 // increases the efficiency of multithreading by dequeueing in chunks of this many nodes, reducing the amount of time spent waiting for sync

// THREAD_* defines how will threads be created.
#define THREAD_BOOST
//#define THREAD_WINAPI

// SYNC_* selects the synchronization (mutex and condition) methods.
//#define SYNC_BOOST
//#define SYNC_WINAPI
//#define SYNC_WINAPI_SPIN
#define SYNC_INTEL_SPIN

// TLS_* selects how thread-local storage is done.
#define TLS_COMPILER
//#define TLS_WINAPI
//#define TLS_BOOST

// How many bytes of RAM to use?
#define RAM_SIZE (8LL*1024*1024*1024)

// How many bytes to use for file stream buffers?
#define STANDARD_BUFFER_SIZE  (  1*1024*1024 / sizeof(Node)) // allocated separately in heap - used for open node files and other files
#define CLOSED_IN_BUFFER_SIZE ( 16*1024*1024 / sizeof(Node)) // for input to the Expanding phase

// The expected ratio of Merging output/input
#define EXPECTED_MERGING_RATIO 0.99

// If defined, preallocate expanded chunks to this size to avoid disk fragmentation, then truncate them to their actual size upon closing. This
// dramatically speeds up the Merging step when using magnetic hard drives (as opposed to solid state drives). it requires administrative-level
// privilege, as the preallocated space contains whatever contents previously occupied that location on disk.
//#define PREALLOCATE_EXPANDED 2760704000

// If defined, preallocate files outputted during the Combining step ("closing" and "combining")
#define PREALLOCATE_COMBINING


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

//#define ALIGN_TO_32BITS


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
