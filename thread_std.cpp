#include <thread>

#define THREAD std::thread*

template<void (*WORKER_FUNCTION)()>
void THREAD_CREATE(THREAD_ID threadID)
{
	new std::thread([threadID] {
		TLS_SET_THREAD_ID(threadID);
		WORKER_FUNCTION();
	});
}

// #define THREAD_JOIN(thread) (thread)->join()
// #define THREAD_DESTROY(thread) delete (thread)

