#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>

#define THREAD boost::thread*
#define THREAD_CREATE(delegate) new boost::thread(delegate)
#define THREAD_JOIN(thread) (thread)->join()
#define THREAD_DESTROY(thread) delete (thread)

#define MUTEX boost::mutex
#define MUTEX_SET_SPIN_COUNT(mutex, spin)

#define SCOPED_LOCK boost::mutex::scoped_lock
#define SCOPED_LOCK_UNLOCK(lock) (lock).unlock()
#define SCOPED_LOCK_LOCK(lock) (lock).lock()

#define CONDITION boost::condition
#define CONDITION_WAIT(condition, lock) (condition).wait(lock)
#define CONDITION_NOTIFY_ALL(condition) (condition).notify_all()
