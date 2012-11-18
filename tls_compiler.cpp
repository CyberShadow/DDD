#ifdef _MSC_VER
# define THREADLOCAL __declspec(thread)
#else
# define THREADLOCAL __thread
#endif

THREADLOCAL THREAD_ID tls_threadID;

#define TLS_GET_THREAD_ID tls_threadID
#define TLS_SET_THREAD_ID(x) tls_threadID = (x)
