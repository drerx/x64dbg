#include "thread.h"
#include "console.h"
#include "undocumented.h"
#include "memory.h"
#include "threading.h"

static std::vector<THREADINFO> threadList;
static int threadNum;

void threadcreate(CREATE_THREAD_DEBUG_INFO* CreateThread)
{
    THREADINFO curInfo;
    memset(&curInfo, 0, sizeof(THREADINFO));

    curInfo.ThreadNumber        = threadNum;
    curInfo.Handle              = CreateThread->hThread;
    curInfo.ThreadId            = ((DEBUG_EVENT*)GetDebugData())->dwThreadId;
    curInfo.ThreadStartAddress  = (uint)CreateThread->lpStartAddress;
    curInfo.ThreadLocalBase     = (uint)CreateThread->lpThreadLocalBase;

    // The first thread (#0) is always the main program thread
    if(threadNum <= 0)
        strcpy_s(curInfo.threadName, "Main Thread");

    // Modify global thread list
    EXCLUSIVE_ACQUIRE(LockThreads);
    threadList.push_back(curInfo);
    threadNum++;
    EXCLUSIVE_RELEASE();

    // Notify GUI
    GuiUpdateThreadView();
}

void threadexit(DWORD dwThreadId)
{
    EXCLUSIVE_ACQUIRE(LockThreads);

    for(auto itr = threadList.begin(); itr != threadList.end(); itr++)
    {
        if(itr->ThreadId == dwThreadId)
        {
            threadList.erase(itr);
            break;
        }
    }

    EXCLUSIVE_RELEASE();
    GuiUpdateThreadView();
}

void threadclear()
{
    threadNum = 0;

    // Clear the current array of threads
    EXCLUSIVE_ACQUIRE(LockThreads);
    threadList.clear();
    EXCLUSIVE_RELEASE();

    // Update the GUI's list
    GuiUpdateThreadView();
}

bool ThreadGetTeb(uint TEBAddress, TEB* Teb)
{
    //
    // TODO: Keep a cached copy inside the vector
    //
    memset(Teb, 0, sizeof(TEB));

    return memread(fdProcessInfo->hProcess, (void*)TEBAddress, Teb, sizeof(TEB), nullptr);
}

int ThreadGetSuspendCount(HANDLE Thread)
{
    //
    // Suspend a thread in order to get the previous suspension count
    // WARNING: This function is very bad (threads should not be randomly interrupted)
    //
    int suspendCount = (int)SuspendThread(Thread);

    if(suspendCount == -1)
        return 0;

    // Resume the thread's normal execution
    ResumeThread(Thread);

    return suspendCount;
}

THREADPRIORITY ThreadGetPriority(HANDLE Thread)
{
    return (THREADPRIORITY)GetThreadPriority(Thread);
}

THREADWAITREASON ThreadGetWaitReason(HANDLE Thread)
{
    UNREFERENCED_PARAMETER(Thread);

    //TODO: Implement this
    return _Executive;
}

DWORD ThreadGetLastError(uint tebAddress)
{
    TEB teb;
    if(!ThreadGetTeb(tebAddress, &teb))
    {
        // TODO: Assert (Why would the TEB fail?)
        return 0;
    }

    return teb.LastErrorValue;
}

void ThreadGetList(THREADLIST* list)
{
    SHARED_ACQUIRE(LockThreads);

    //
    // This function converts a C++ std::vector to a C-style THREADLIST[]
    // Also assume BridgeAlloc zeros the returned buffer
    //
    size_t count = threadList.size();

    if(count <= 0)
        return;

    list->count = (int)count;
    list->list  = (THREADALLINFO*)BridgeAlloc(count * sizeof(THREADALLINFO));

    // Fill out the list data
    for(size_t i = 0; i < count; i++)
    {
        HANDLE threadHandle = threadList[i].Handle;

        // Get the debugger's current thread index
        if(threadHandle == hActiveThread)
            list->CurrentThread = (int)i;

        memcpy(&list->list[i].BasicInfo, &threadList[i], sizeof(THREADINFO));

        list->list[i].ThreadCip     = GetContextDataEx(threadHandle, UE_CIP);
        list->list[i].SuspendCount  = ThreadGetSuspendCount(threadHandle);
        list->list[i].Priority      = ThreadGetPriority(threadHandle);
        list->list[i].WaitReason    = ThreadGetWaitReason(threadHandle);
        list->list[i].LastError     = ThreadGetLastError(list->list[i].BasicInfo.ThreadLocalBase);
    }
}

bool ThreadIsValid(DWORD dwThreadId)
{
    SHARED_ACQUIRE(LockThreads);

    for(auto itr = threadList.begin(); itr != threadList.end(); itr++)
    {
        if(itr->ThreadId == dwThreadId)
            return true;
    }

    return false;
}

bool ThreadSetName(DWORD dwThreadId, const char* name)
{
    EXCLUSIVE_ACQUIRE(LockThreads);

    // This modifies a variable (name), so an exclusive lock is required
    for(auto itr = threadList.begin(); itr != threadList.end(); itr++)
    {
        if(itr->ThreadId == dwThreadId)
        {
            if(name)
                strcpy_s(itr->threadName, name);
            else
                itr->threadName[0] = '\0';
        }
    }

    return false;
}

HANDLE ThreadGetHandle(DWORD dwThreadId)
{
    SHARED_ACQUIRE(LockThreads);

    for(auto itr = threadList.begin(); itr != threadList.end(); itr++)
    {
        if(itr->ThreadId == dwThreadId)
            return itr->Handle;
    }

    // TODO: Set an assert if the handle is never found,
    // using a bad handle causes random/silent issues everywhere
    return 0;
}

DWORD ThreadGetId(HANDLE hThread)
{
    SHARED_ACQUIRE(LockThreads);

    // Search for the ID in the local list
    for(auto itr = threadList.begin(); itr != threadList.end(); itr++)
    {
        if(itr->Handle == hThread)
            return itr->ThreadId;
    }

    // Wasn't found, check with Windows
    // This also returns 0 on error
    /*
    REQUIRES VISTA+

    return GetThreadId(hThread);
    */

    // TODO: Same problem with threadgethandle()
    return 0;
}

int ThreadGetCount()
{
    return (int)threadList.size();
}

int ThreadSuspendAll()
{
    //
    // SuspendThread does not modify any internal variables
    //
    SHARED_ACQUIRE(LockThreads);

    int count = 0;
    for(auto itr = threadList.begin(); itr != threadList.end(); itr++)
    {
        if(SuspendThread(itr->Handle) != -1)
            count++;
    }

    return count;
}

int ThreadResumeAll()
{
    //
    // ResumeThread does not modify any internal variables
    //
    EXCLUSIVE_ACQUIRE(LockThreads);

    int count = 0;
    for(auto itr = threadList.begin(); itr != threadList.end(); itr++)
    {
        if(ResumeThread(itr->Handle) != -1)
            count++;
    }

    return count;
}