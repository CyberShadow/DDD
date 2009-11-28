#include <boost/thread/thread.hpp>

#define THREAD boost::thread*
#define THREAD_CREATE(delegate) new boost::thread(delegate)
#define THREAD_JOIN(thread) (thread)->join()
#define THREAD_DESTROY(thread) delete (thread)

