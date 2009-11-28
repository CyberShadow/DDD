// DEBUG enables asserts and some other debug checks.
#define DEBUG

// Specify the level to solve here.
#define LEVEL 26

// MULTITHREADING will enable threading and synchronization code.
#define MULTITHREADING
#define THREADS 4

// THREAD_* defines how will threads be created.
#define THREAD_BOOST
//#define THREAD_WIN32

// SYNC_* selects the synchronization (mutex and condition) methods.
//#define SYNC_BOOST
//#define SYNC_WIN32
//#define SYNC_WIN32_SPIN
#define SYNC_INTEL_SPIN

// This shouldn't normally be changed.
#define MAX_NODES 0xFFFFFFFFLL

// SWAP enables swapping the state node set to disk, and using a memory cache. 
// If the solved level is not expected to exhaust memory without SWAP, it should be turned off for better performance.
#define SWAP

// CACHE_x selects the caching algorithm to use. CACHE_SPLAY needs more CPU, but causes somewhat fewer cache misses. CACHE_HASH parallelizes a lot better.
#define CACHE_SPLAY
//#define CACHE_HASH

// Number of nodes to cache. The size of each node varies depending on the CACHE_* and BFS/DFS options.
#define CACHE_SIZE 0xA000000

// SWAP_x selects the back-end storage to be used for the node swap.
//#define SWAP_RAM
//#define SWAP_MMAP
#define SWAP_WINFILES
//#define SWAP_POSIX

// Use a depth-first search algorithm instead of the default Dijksta BFS.
// (Untested) Should be faster when the number of accessed nodes for each BFS frame exceeds CACHE_SIZE (the number of nodes that fit in physical RAM).
#define DFS
