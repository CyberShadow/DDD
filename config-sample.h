// DEBUG enables asserts and some other debug checks.
//#define DEBUG

// Specify the level to solve here.
#define LEVEL 11

// MULTITHREADING will enable threading and synchronization code.
#define MULTITHREADING
#define THREADS 4

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

// How many nodes to group under a single hash in the cache? Higher values reduce speed but allow better distribution.
#define NODES_PER_HASH 4

// DISK_* selects the back-end storage to be used for the node disk files.
#define DISK_WINFILES
//#define DISK_POSIX

// Keep a file with all nodes to filter against, instead of filtering against each individual frame. Uses less CPU but more I/O.
//#define USE_ALL
