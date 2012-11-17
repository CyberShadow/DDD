__thread THREAD_ID tls_threadID;

#define GET_THREAD_ID tls_threadID
#define SET_THREAD_ID(x) tls_threadID = (x)
