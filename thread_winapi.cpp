#ifndef _WIN32
#error Wrong platform?
#endif

DWORD WINAPI CallCFunction(__in LPVOID lpParameter)
{
	typedef void (*C_FUNC)();
	C_FUNC f = (C_FUNC)lpParameter;
	f();
	return 0;
}

#define THREAD HANDLE
#define THREAD_CREATE(delegate) CreateThread(NULL, 0, &CallCFunction, delegate, 0, NULL)
#define THREAD_JOIN(thread) WaitForSingleObject(thread, INFINITE);
#define THREAD_DESTROY(thread) CloseHandle(thread)

