#ifndef _WIN32
#error Wrong platform?
#endif

typedef size_t THREAD_ID;

template<void (*WORKER_FUNCTION)(THREAD_ID threadID)>
DWORD WINAPI CallCFunction(__in LPVOID lpParameter)
{
	try
	{
		WORKER_FUNCTION((THREAD_ID)lpParameter);
	}
	catch (const char* s)
	{
		puts(s);
		exit(1);
	}
	return 0;
}

#define THREAD HANDLE
#define THREAD_CREATE(_delegate,threadID) CreateThread(NULL, 0, &CallCFunction<_delegate>, (void*)threadID, 0, NULL)
#define THREAD_JOIN(thread) WaitForSingleObject(thread, INFINITE);
#define THREAD_DESTROY(thread) CloseHandle(thread)

