#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>

#define MUTEX boost::mutex
#define MUTEX_SET_SPIN_COUNT(mutex, spin)

#define SCOPED_LOCK boost::mutex::scoped_lock

#define CONDITION boost::condition
#define CONDITION_RESET(condition)
