#ifndef _WIN32
#error Wrong platform?
#endif

template<void (*WORKER_FUNCTION)()>
DWORD WINAPI CallCFunction(/*__in*/ LPVOID lpParameter)
{
	try
	{
		THREAD_ID threadID = (THREAD_ID&)lpParameter;
		TLS_SET_THREAD_ID(threadID);

		WORKER_FUNCTION();
	}
	catch (const char* s)
	{
		puts(s);
		exit(1);
	}
	return 0;
}

#define THREAD HANDLE
//#define THREAD_CREATE(_delegate,threadID) CreateThread(NULL, 0, &CallCFunction<_delegate>, (void*)threadID, 0, NULL)
#define THREAD_JOIN(thread) WaitForSingleObject(thread, INFINITE)
#define THREAD_DESTROY(thread) CloseHandle(thread)

template<void (*WORKER_FUNCTION)()>
void THREAD_CREATE(THREAD_ID threadID, int priority=/*THREAD_PRIORITY_NORMAL*/THREAD_PRIORITY_BELOW_NORMAL)
{
	HANDLE thread = CreateThread(NULL, 0, &CallCFunction<WORKER_FUNCTION>, (void*)threadID, 0, NULL);
    SetThreadPriority(thread, priority);
	CloseHandle(thread);
}
