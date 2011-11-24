//------------------------------------------------------------------------------
// <copyright file="sdio_lib_os.c" company="Atheros">
//    Copyright (c) 2008 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================


/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   windows CE implementation module for SDIO library
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define MODULE_NAME  SDLIB_
/* debug level for this module*/
#define DBG_DECLARE 4;
#include  <ctsystem.h>
 #include <sdio_busdriver.h>
#include  <sdio_lib.h>

CRITICAL_SECTION g_AtomicLock;
CRITICAL_SECTION g_DebugPrintLock;

BOOL SDIO_LibraryInit()
{
    InitializeCriticalSection(&g_AtomicLock);   
    InitializeCriticalSection(&g_DebugPrintLock);
    return TRUE;
} 

void SDIO_LibraryDeinit()
{
    DeleteCriticalSection(&g_AtomicLock); 
    DeleteCriticalSection(&g_DebugPrintLock);   
}


/* helper function launcher */
DWORD HelperLaunch(PVOID pContext)
{
    THREAD_RETURN exit;
        /* call function */
    exit = ((POSKERNEL_HELPER)pContext)->pHelperFunc((POSKERNEL_HELPER)pContext);
    
    return 0;
}

/*
 * OSCreateHelper - create a worker kernel thread
*/
SDIO_STATUS SDLIB_OSCreateHelper(POSKERNEL_HELPER pHelper,
                                 PHELPER_FUNCTION pFunction, 
                                 PVOID            pContext)
{
    DWORD       threadId;
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    
    memset(pHelper,0,sizeof(OSKERNEL_HELPER));  
    
    do {
        pHelper->pContext = pContext;
        pHelper->pHelperFunc = pFunction;
        
        status = SignalInitialize(&pHelper->WakeSignal);
        
        if (!SDIO_SUCCESS(status)) {
            break; 
        }    
        
        pHelper->hThread = CreateThread(NULL,0,HelperLaunch,pHelper,0,&threadId);
        
        if (NULL == pHelper->hThread) {
            status = SDIO_STATUS_NO_RESOURCES;
            break;    
        }        

    } while (FALSE);
    
    if (!SDIO_SUCCESS(status)) {
        SDLIB_OSDeleteHelper(pHelper);   
    }
    return status;
}
                           
/*
 * OSDeleteHelper - delete thread created with OSCreateHelper
*/
void SDLIB_OSDeleteHelper(POSKERNEL_HELPER pHelper)
{
 
    if (pHelper->hThread != NULL) {
        pHelper->ShutDown = TRUE;       
        SignalSet(&pHelper->WakeSignal); 
            /* wait for thread to exit */ 
        WaitForSingleObject(pHelper->hThread,INFINITE);
    }  
    
    SignalDelete(&pHelper->WakeSignal);
}
   


    
ATOMIC_FLAGS AtomicTest_SetWithMask(volatile ATOMIC_FLAGS *pValue, ATOMIC_FLAGS OrMask)
{
    ATOMIC_FLAGS oldValue;
    
    EnterCriticalSection(&g_AtomicLock);
    oldValue = *pValue;
    *pValue |= OrMask;
    LeaveCriticalSection(&g_AtomicLock);
    return oldValue;
}

ATOMIC_FLAGS AtomicTest_ClearWithMask(volatile ATOMIC_FLAGS *pValue, ATOMIC_FLAGS AndMask)
{
    ATOMIC_FLAGS oldValue;
    
    EnterCriticalSection(&g_AtomicLock);
    oldValue = *pValue;
    *pValue &= AndMask;
    LeaveCriticalSection(&g_AtomicLock);
    return oldValue;
}
                       
SDIO_STATUS SDLIB_GetRegistryKeyValue(HKEY   hKey,
                                      WCHAR  *pKeyPath,
                                      WCHAR  *pValueName,
                                      PUCHAR pValue,
                                      ULONG  BufferSize)
{ 
    LONG  status;       /* reg api status */
    HKEY  hOpenKey;     /* opened key handle */

    status = RegOpenKeyEx(hKey,
                          pKeyPath,
                          0,
                          0,
                          &hOpenKey);

    if (status != ERROR_SUCCESS) {
        return SDIO_STATUS_ERROR;
    }

    status = RegQueryValueEx(hOpenKey,
                             pValueName,
                             NULL,
                             NULL,
                             pValue,
                             &BufferSize);

    RegCloseKey(hOpenKey); 
    if (ERROR_SUCCESS == status) {
        return SDIO_STATUS_SUCCESS;
    } 
    return SDIO_STATUS_ERROR;
}


SDIO_STATUS SDLIB_GetRegistryKeyDWORD(HKEY   hKey, 
                                      WCHAR  *pKeyPath,
                                      WCHAR  *pValueName, 
                                      PDWORD pValue)
{
    LONG  status;       /* reg api status */
    HKEY  hOpenKey;     /* opened key handle */
    DWORD type;
    DWORD value;
    ULONG bufferSize;
    
    status = RegOpenKeyEx(hKey,
                          pKeyPath,
                          0,
                          0,
                          &hOpenKey);

    if (status != ERROR_SUCCESS) {
        return SDIO_STATUS_ERROR;
    }
    
    bufferSize = sizeof(DWORD);
    
    status = RegQueryValueEx(hOpenKey,
                             pValueName,
                             NULL,
                             &type,
                             (PUCHAR)&value,
                             &bufferSize);

    RegCloseKey(hOpenKey); 
    
    if (ERROR_SUCCESS == status) {        
        if (REG_DWORD == type) {
            *pValue = value;    
            return SDIO_STATUS_SUCCESS;
        }
    } 
    
    return SDIO_STATUS_ERROR;
}

#define MAX_DEBUG_STRING 325

CHAR    g_DebugBuffer[MAX_DEBUG_STRING];
WCHAR   g_WideDebugBuffer[MAX_DEBUG_STRING];
            
    /* output wide string, window ce debugger uses wide-chars */               
static VOID OutputPrintToDebugger(CHAR *pDebugStr)
{
    PWCHAR pBuffer;

    pBuffer = g_WideDebugBuffer;

    while (*pDebugStr != (CHAR)0) {
        *pBuffer = (WCHAR)*pDebugStr;
        pDebugStr++;
        pBuffer++;
    }
    
    *pBuffer = (WCHAR)0;
    OutputDebugString(g_WideDebugBuffer);
}

    /* output debug string */
VOID CTOutputDebug(CHAR *pDbgStr,...)
{
    va_list       argumentList;
    int           debugChars,ii;

    va_start(argumentList, pDbgStr);

    EnterCriticalSection(&g_DebugPrintLock);
    
    g_DebugBuffer[MAX_DEBUG_STRING - 1] = (CHAR)0;
        
    do {
        
        debugChars = _vsnprintf(g_DebugBuffer, (MAX_DEBUG_STRING - 1), pDbgStr, argumentList);

        if (debugChars < 0) {
            RETAILMSG(TRUE, 
                   (TEXT(" Debug string TOO LONG!!! \r\n")));
            break;
        } 

        if (debugChars > 2) {
            for (ii = (debugChars - 3); ii < debugChars; ii++) {
                    /* replace the last line feed with carriage return + line feed */
                if ('\n' == g_DebugBuffer[ii]) {
                    if (debugChars < (MAX_DEBUG_STRING - 3)) {
                        g_DebugBuffer[ii] = '\r';
                        g_DebugBuffer[ii+1] = '\n';   
                        g_DebugBuffer[ii+2] = (CHAR)0; 
                        break;
                    }    
                }  
            }            
        }        

        OutputPrintToDebugger(g_DebugBuffer);

    } while (FALSE);
    
    LeaveCriticalSection(&g_DebugPrintLock);
    
    va_end(argumentList); 
}

typedef struct _CT_WORKER_THREAD {
    HANDLE              hWorkerThread;
    HANDLE              hWorkerThreadWakeUp;
    CRITICAL_SECTION    WorkThreadLock;
    BOOL                ShutDown;
    SDLIST              WorkList;
    PCT_WORKER_TASK     pCurrentTask;
}CT_WORKER_THREAD, *PCT_WORKER_THREAD;


#define LOCK_WORKER(pW)   EnterCriticalSection(&(pW)->WorkThreadLock)
#define UNLOCK_WORKER(pW) LeaveCriticalSection(&(pW)->WorkThreadLock)

 
static DWORD WorkerThread(PVOID pContext);

VOID SDLIB_FlushWorkTask(PVOID Worker, PCT_WORKER_TASK pTask)
{
    
    PCT_WORKER_THREAD pWorker = (PCT_WORKER_THREAD)Worker;
    volatile BOOL *pQueued;
    volatile PCT_WORKER_TASK *pRunningTask;
    
    LOCK_WORKER(pWorker);
    do {
        if (pTask->Queued) {
                /* its queued, spin wait for it to make it out of the queue */
            pQueued = &pTask->Queued;
            UNLOCK_WORKER(pWorker);
            while (1) {
             
                if (!(*pQueued)) {
                    break;    
                }
                
                OSSleep(200);    
            }            
            LOCK_WORKER(pWorker);
            /* fall through and see if it is running */
        }
        
        pRunningTask = &pWorker->pCurrentTask;
        UNLOCK_WORKER(pWorker);
        while (1) {
         
            if ((*pRunningTask) != pTask) {
                break;    
            }
            OSSleep(200);    
        }            
        LOCK_WORKER(pWorker);
        
    } while (FALSE);
    
    UNLOCK_WORKER(pWorker);
}

SDIO_STATUS SDLIB_QueueWorkTask(PVOID Worker, PCT_WORKER_TASK pTask)
{
    SDIO_STATUS       status = SDIO_STATUS_SUCCESS;
    PCT_WORKER_THREAD pWorker = (PCT_WORKER_THREAD)Worker;
    
    LOCK_WORKER(pWorker);
    
    do {
        if (pTask->Queued) {
            break;    
        }
        
        if (pWorker->ShutDown) {
            status = SDIO_STATUS_CANCELED;
            break;    
        }
        
        pTask->Queued = TRUE;
        
        SDListInsertTail(&pWorker->WorkList, &pTask->List);
       
            /* wake up worker thread */
        SetEvent(pWorker->hWorkerThreadWakeUp);
        
    } while (FALSE);
    
    UNLOCK_WORKER(pWorker);
    
    return status;
}


VOID SDLIB_DestroyWorker(PVOID Worker)
{
    PCT_WORKER_THREAD pWorker = (PCT_WORKER_THREAD)Worker;
    
    pWorker->ShutDown = TRUE;
    
    if (pWorker->hWorkerThreadWakeUp != NULL) {
        SetEvent(pWorker->hWorkerThreadWakeUp);    
    }
    
    if (pWorker->hWorkerThread != NULL) {
        WaitForSingleObject(pWorker->hWorkerThread, INFINITE);
        CloseHandle(pWorker->hWorkerThread);
        pWorker->hWorkerThread = NULL;  
    }
    
    if (pWorker->hWorkerThreadWakeUp != NULL) {
        CloseHandle(pWorker->hWorkerThreadWakeUp);    
        pWorker->hWorkerThreadWakeUp = NULL;
    }
    
    DeleteCriticalSection(&pWorker->WorkThreadLock);
    
    LocalFree(pWorker);
}

 
PVOID SDLIB_CreateWorker(INT WorkerPriority)
{
    PCT_WORKER_THREAD pWorker = NULL;
    DWORD             threadID;
    BOOL              success = FALSE;

    do {
    
        pWorker = LocalAlloc(LPTR,sizeof(CT_WORKER_THREAD));
        
        if (NULL == pWorker) {
            break;    
        }   
         
        InitializeCriticalSection(&pWorker->WorkThreadLock);
        
        SDLIST_INIT(&pWorker->WorkList);
        
        pWorker->hWorkerThreadWakeUp = CreateEvent(NULL, FALSE, FALSE, NULL);
        
        if (NULL == pWorker->hWorkerThreadWakeUp) {
            break;    
        }  
        
        pWorker->hWorkerThread = CreateThread(NULL,0,WorkerThread,pWorker,0,&threadID);
        
        if (NULL == pWorker->hWorkerThread) {
            break;    
        }
         
        success = TRUE;
        
        CeSetThreadPriority(pWorker->hWorkerThread,WorkerPriority);

    } while (FALSE);
    
    if (!success) {
        if (pWorker != NULL) {
            SDLIB_DestroyWorker(pWorker); 
            pWorker = NULL;
        }   
    }
    
    return pWorker;
}


/* worker thread */
static DWORD WorkerThread(PVOID pContext)
{
    PCT_WORKER_THREAD pWorker = (PCT_WORKER_THREAD)pContext;
    PSDLIST           pListItem;
    PCT_WORKER_TASK   pTask;
    
    while (1) {
        
        if (WaitForSingleObject(pWorker->hWorkerThreadWakeUp, INFINITE) != WAIT_OBJECT_0) {
            break;    
        }    
        
            /* unload the task queue */
            
        LOCK_WORKER(pWorker);
        
        while (1) {
            
            pListItem = SDListRemoveItemFromHead(&pWorker->WorkList);
            
            if (NULL == pListItem) {
                break;    
            }
            
            pTask = CONTAINING_STRUCT(pListItem,CT_WORKER_TASK,List);
            
            pWorker->pCurrentTask = pTask;
            
            pTask->Queued = FALSE;
            
            UNLOCK_WORKER(pWorker);
            
            DBG_ASSERT(pTask->pCallBack != NULL);
            
                /* call callback */
            pTask->pCallBack(pTask->pContext);
            
            LOCK_WORKER(pWorker);  
            pWorker->pCurrentTask = NULL;          
        }
        
        UNLOCK_WORKER(pWorker);
        
        if (pWorker->ShutDown) {
            break;    
        }
                
    }
    
    return 0;
}
