//------------------------------------------------------------------------------
// <copyright file="ctsystem_wince.h" company="Atheros">
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

#ifndef __CPSYSTEM_WINCE_H___
#define __CPSYSTEM_WINCE_H___

#include <windows.h>
#include <CEDDK.h>

#define CT_BREAK_ON_ASSERT

/* generic types */

typedef    char             TEXT;
typedef    char *           PTEXT;
typedef    unsigned int     UINT;
typedef    unsigned int*    PUINT;
typedef    int              INT;
typedef    int*             PINT;
typedef    int              SDIO_STATUS;
typedef    long             SYSTEM_STATUS;
typedef    unsigned int     EVENT_TYPE;
typedef    unsigned int     EVENT_ARG;
typedef    unsigned int*    PEVENT_TYPE;
typedef    HANDLE            OS_SEMAPHORE;
typedef    HANDLE*           POS_SEMAPHORE;
typedef    HANDLE            OS_SIGNAL;    
typedef    HANDLE*           POS_SIGNAL;  
typedef    CRITICAL_SECTION  OS_CRITICALSECTION;
typedef    CRITICAL_SECTION* POS_CRITICALSECTION;  
typedef    int               SDPOWER_STATE;
typedef    unsigned long     ATOMIC_FLAGS;
typedef    DWORD             THREAD_RETURN;
typedef    PVOID            OS_DRIVER;
typedef    PVOID*           POS_DRIVER;
typedef    PVOID            OS_DEVICE;
typedef    PVOID*           POS_DEVICE;
typedef    PVOID            OS_PNPDRIVER;
typedef    PVOID*           POS_PNPDRIVER;
typedef    PVOID            OS_PNPDEVICE;
typedef    PVOID*           POS_PNPDEVICE;

typedef    DWORD            CT_DEBUG_LEVEL;

typedef  struct _CT_CE_MODULE_INFO {
    LONG    ReferenceCount;
}CT_CE_MODULE_INFO, *PCT_CE_MODULE_INFO;

typedef  PCT_CE_MODULE_INFO  POS_MODULE;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL ((PVOID)0)
#endif
#define SDDMA_DESCRIPTION_FLAG_DMA   0x1  /* DMA enabled */
#define SDDMA_DESCRIPTION_FLAG_SGDMA 0x2  /* Scatter-Gather DMA enabled */
typedef struct _SDDMA_DESCRIPTION {
    UINT16      Flags;              /* SDDMA_DESCRIPTION_FLAG_xxx */
    UINT16      MaxDescriptors;     /* number of supported scatter gather entries */
    UINT32      MaxBytesPerDescriptor;  /* maximum bytes in a DMA descriptor entry */
    UINT32      Mask;              /* dma address mask */
    UINT32      AddressAlignment;  /* dma address alignment mask, least significant bits indicate illegal address bits */
    UINT32      LengthAlignment;   /* dma buffer length alignment mask, least significant bits indicate illegal length bits  */
    DMA_ADAPTER_OBJECT *pAdapterObject; /* adapter object for memory allocations, must be filled in by HCD */
}SDDMA_DESCRIPTION, *PSDDMA_DESCRIPTION;


typedef struct _SDDMA_DESCRIPTOR {
    PHYSICAL_ADDRESS Address;        /* physical address of buffer */
    UINT32           Length;         /* length of buffer */   
}SDDMA_DESCRIPTOR, *PSDDMA_DESCRIPTOR;

#ifndef INLINE
#define INLINE  __inline
#define CT_PACK_STRUCT
#endif

VOID CTOutputDebug(CHAR *pDbgStr,...);
    
/* debug print macros */

#define _GET_MODULE_NAME_DEBUG_(s) _XGET_MODULE_NAME_DEBUG_(s)
#define _XGET_MODULE_NAME_DEBUG_(s) debuglevel__ ## s
#ifdef DBG_DECLARE
#ifdef MODULE_NAME
#define _MOD_DEBUG_NAME _GET_MODULE_NAME_DEBUG_(MODULE_NAME) 
#else  /* MODULE_NAME */
#define _MOD_DEBUG_NAME debuglevel_l
#endif /* MODULE_NAME */
int _MOD_DEBUG_NAME = DBG_DECLARE;
#else
#ifdef MODULE_NAME
#define _MOD_DEBUG_NAME _GET_MODULE_NAME_DEBUG_(MODULE_NAME)
#else  /* MODULE_NAME */
#define _MOD_DEBUG_NAME debuglevel_l
#endif /* MODULE_NAME */
extern int _MOD_DEBUG_NAME;
#endif /* DBG_DECLARE */

#ifdef DEBUG

#ifdef CT_BREAK_ON_ASSERT
#define CT_DEBUG_BREAK() DebugBreak()
#else
#define CT_DEBUG_BREAK() 
#endif
 
#define DBG_ASSERT(test) \
{                         \
    if (!(test)) {          \
        DBG_PRINT(SDDBG_ERROR,  \
        ("Debug Assert Caught, File %s, Line: %d, Test:%s \n",__FILE__, __LINE__,#test)); \
        CT_DEBUG_BREAK();     \
    }                     \
}
#define DBG_ASSERT_WITH_MSG(test,s) \
{                                   \
    if (!(test)) {                  \
        DBG_PRINT(SDDBG_ERROR, ("Assert:%s File %s, Line: %d \n",(s),__FILE__, __LINE__)); \
        CT_DEBUG_BREAK();     \
    }                         \
}

#define DBG_PRINT(lvl, args) \
    if (lvl <= DBG_GET_DEBUG_LEVEL()) CTOutputDebug args


#else /* DEBUG */
#define DBG_PRINT(lvl, str)
#define DBG_ASSERT(test)
#define DBG_ASSERT_WITH_MSG(test,s)
#endif /* DEBUG */

#define DBG_GET_DEBUG_LEVEL() _MOD_DEBUG_NAME
#define DBG_SET_DEBUG_LEVEL(v) _MOD_DEBUG_NAME  = (v)

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Print a string to the debugger or console
  
  @function name: REL_PRINT
  @prototype: void REL_PRINT(INT Level, string)
  @category: Support_Reference
  @input:  Level - debug level for the print
 
  @output: none

  @return: 
 
  @notes: If Level is less than the current debug level, the print will be
          issued. This print cannot be conditionally compiled.
  @see also: DBG_PRINT              
 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define REL_PRINT(lvl, args)\
    if (lvl <= DBG_GET_DEBUG_LEVEL()) CTOutputDebug args
    
/* debug output levels, this must be order low number to higher */
#define SDDBG_ERROR 3  
#define SDDBG_WARN  4  
#define SDDBG_DEBUG 6  
#define SDDBG_TRACE 7  

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Initialize a critical section object.
  
  @function name: CriticalSectionInit
  @prototype: SDIO_STATUS CriticalSectionInit(POS_CRITICALSECTION pCrit)
  @category: Support_Reference
  @output: pCrit - pointer to critical section to initialize

  @return: SDIO_STATUS_SUCCESS on success.
 
  @notes:  CriticalSectionDelete() must be called to cleanup any resources
           associated with the critical section.

  @see also: CriticalSectionDelete, CriticalSectionAcquire, CriticalSectionRelease 
  @example: To initialize a critical section:        
        status = CriticalSectionInit(&pDevice->ListLock);
        if (!SDIO_SUCCESS(status)) {
                .. failed
            return status;
        }

+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static INLINE SDIO_STATUS CriticalSectionInit(POS_CRITICALSECTION pCrit) {
    InitializeCriticalSection(pCrit);
    return SDIO_STATUS_SUCCESS;     
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Acquire a critical section lock. 
  
  @function name: CriticalSectionAcquire
  @prototype: SDIO_STATUS CriticalSectionAcquire(POS_CRITICALSECTION pCrit)
  @category: Support_Reference
  
  @input: pCrit - pointer to critical section that was initialized

  @return: SDIO_STATUS_SUCCESS on success.
 
  @notes:  The critical section lock is acquired when this function returns 
           SDIO_STATUS_SUCCESS.  Use CriticalSectionRelease() to release
           the critical section lock.

  @see also: CriticalSectionRelease            

  @example: To acquire a critical section lock:        
        status = CriticalSectionAcquire(&pDevice->ListLock);
        if (!SDIO_SUCCESS(status)) {
                .. failed
            return status;
        }
        ... access protected data
            // unlock         
        status = CriticalSectionRelease(&pDevice->ListLock);
        if (!SDIO_SUCCESS(status)) {
                .. failed
            return status;
        }

+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static INLINE SDIO_STATUS CriticalSectionAcquire(POS_CRITICALSECTION pCrit) {
    EnterCriticalSection(pCrit);
    return SDIO_STATUS_SUCCESS; 
}

// macro-tized versions
#define CriticalSectionAcquire_M(pCrit) \
    SDIO_STATUS_SUCCESS; EnterCriticalSection(pCrit) 
#define CriticalSectionRelease_M(pCrit) \
    SDIO_STATUS_SUCCESS; LeaveCriticalSection(pCrit)
    
    /* on windows CE critical sections are usable in interrupt processing code that takes the
     * same lock */
#define CT_DECLARE_IRQ_SYNC_CONTEXT() 
#define CriticalSectionAcquireSyncIrq(pCrit) \
    SDIO_STATUS_SUCCESS; EnterCriticalSection(pCrit)
#define CriticalSectionReleaseSyncIrq(pCrit) \
    SDIO_STATUS_SUCCESS; LeaveCriticalSection(pCrit)


     
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Release a critical section lock. 
  
  @function name: CriticalSectionRelease
  @prototype: SDIO_STATUS CriticalSectionRelease(POS_CRITICALSECTION pCrit)
  @category: Support_Reference
 
  @input: pCrit - pointer to critical section that was initialized

  @return: SDIO_STATUS_SUCCESS on success.
 
  @notes:  The critical section lock is released when this function returns 
           SDIO_STATUS_SUCCESS. 

  @see also: CriticalSectionAcquire       
  
  @example: see CriticalSectionAcquire

+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static INLINE SDIO_STATUS CriticalSectionRelease(POS_CRITICALSECTION pCrit) {
    LeaveCriticalSection(pCrit); 
    return SDIO_STATUS_SUCCESS; 
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Cleanup a critical section object
  
  @function name: CriticalSectionDelete
  @prototype: void CriticalSectionDelete(POS_CRITICALSECTION pCrit)
  @category: Support_Reference
  
  @input: pCrit - an initialized critical section object

  @return: SDIO_STATUS_SUCCESS on success.
 
  @notes: 

  @see also: CriticalSectionInit, CriticalSectionAcquire, CriticalSectionRelease            

+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static INLINE void CriticalSectionDelete(POS_CRITICALSECTION pCrit) {
    DeleteCriticalSection(pCrit); 
    return; 
}

static INLINE SDIO_STATUS SignalInitialize(POS_SIGNAL pSignal) {
    *pSignal = CreateEvent(NULL,FALSE,FALSE,NULL);
    if (NULL == *pSignal) {
        return SDIO_STATUS_NO_RESOURCES;    
    }       
    return SDIO_STATUS_SUCCESS;
}

static INLINE void SignalDelete(POS_SIGNAL pSignal) {
    if (*pSignal != NULL) {
        CloseHandle(*pSignal);  
        *pSignal = NULL;  
    }
    return;  
}

static INLINE SDIO_STATUS SignalWaitInterruptible(POS_SIGNAL pSignal) {
    if (WaitForSingleObject(*pSignal,INFINITE) == WAIT_OBJECT_0) {
        return SDIO_STATUS_SUCCESS;
    } else {
        return SDIO_STATUS_INTERRUPTED;
    }
}

#define SignalWait(p) SignalWaitInterruptible((p))

static INLINE SDIO_STATUS SignalSet(POS_SIGNAL pSignal) {
    SetEvent(*pSignal);
    return SDIO_STATUS_SUCCESS;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Initialize a semaphore object.
  
  @function name: SemaphoreInitialize
  @prototype: SDIO_STATUS SemaphoreInitialize(POS_SEMAPHORE pSem, UINT value)
  @category: Support_Reference
  
  @input:  value - initial value of the semaphore

  @output: pSem - pointer to a semaphore object to initialize

  @return: SDIO_STATUS_SUCCESS on success.
 
  @notes:  SemaphoreDelete() must be called to cleanup any resources
           associated with the semaphore

  @see also: SemaphoreDelete, SemaphorePend, SemaphorePendInterruptable
  
  @example: To initialize a semaphore:        
        status = SemaphoreInitialize(&pDevice->ResourceSem,1);
        if (!SDIO_SUCCESS(status)) {
                .. failed
            return status;
        }

+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static INLINE SDIO_STATUS SemaphoreInitialize(POS_SEMAPHORE pSem, UINT value) {
    *pSem = CreateSemaphore(NULL, 
                            value, 
                            0x7FFFFFFF,
                            NULL);
    if (NULL == *pSem) {
        return SDIO_STATUS_NO_RESOURCES; 
    }
    return SDIO_STATUS_SUCCESS;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Cleanup a semaphore object.
  
  @function name: SemaphoreDelete
  @prototype: void SemaphoreDelete(POS_SEMAPHORE pSem)
  @category: Support_Reference
  
  @input: pSem - pointer to a semaphore object to cleanup

  @return:
 
  @notes:  

  @see also: SemaphoreInitialize            
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static INLINE void SemaphoreDelete(POS_SEMAPHORE pSem) {
    if (*pSem != NULL) {
        CloseHandle(*pSem);   
        *pSem = NULL; 
    }
}
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Acquire the semaphore or pend if the resource is not available
  
  @function name: SemaphorePend
  @prototype: SDIO_STATUS SemaphorePend(POS_SEMAPHORE pSem)
  @category: Support_Reference
  
  @input: pSem - pointer to an initialized semaphore object

  @return: SDIO_STATUS_SUCCESS on success.
 
  @notes: If the semaphore count is zero this function blocks until the count
          becomes non-zero, otherwise the count is decremented and execution 
          continues. While waiting, the task/thread cannot be interrupted. 
          If the task or thread should be interruptible, use SemaphorePendInterruptible.
          On some OSes SemaphorePend and SemaphorePendInterruptible behave the same.

  @see also: SemaphorePendInterruptable, SemaphorePost          
  @example: To wait for a resource using a semaphore:        
        status = SemaphorePend(&pDevice->ResourceSem);
        if (!SDIO_SUCCESS(status)) {
                .. failed
            return status;
        }     
        ... resource acquired
        SemaphorePost(&pDevice->ResourceSem);

+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static INLINE SDIO_STATUS SemaphorePend(POS_SEMAPHORE pSem) {
    if (WaitForSingleObject(*pSem,INFINITE) != WAIT_OBJECT_0) {
        return SDIO_STATUS_INTERRUPTED;    
    }
    return SDIO_STATUS_SUCCESS;
}
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Acquire the semaphore or pend if the resource is not available
  
  @function name: SemaphorePendInterruptable
  @prototype: SDIO_STATUS SemaphorePendInterruptable(POS_SEMAPHORE pSem)
  @category: Support_Reference
  
  @input: pSem - pointer to an initialized semaphore object

  @return: SDIO_STATUS_SUCCESS on success.
 
  @notes: If the semaphore count is zero this function blocks until the count
          becomes non-zero, otherwise the count is decremented and execution 
          continues. While waiting, the task/thread can be interrupted. 
          If the task or thread should not be interruptible, use SemaphorePend.

  @see also: SemaphorePend, SemaphorePost          
  @example: To wait for a resource using a semaphore:        
        status = SemaphorePendInterruptable(&pDevice->ResourceSem);
        if (!SDIO_SUCCESS(status)) {
                .. failed, could have been interrupted
            return status;
        }  
        ... resource acquired
        SemaphorePost(&pDevice->ResourceSem);

+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SemaphorePendInterruptable SemaphorePend

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Post a semaphore.
  
  @function name: SemaphorePost
  @prototype: SDIO_STATUS SemaphorePost(POS_SEMAPHORE pSem)
  @category: Support_Reference
 
  @input: pSem - pointer to an initialized semaphore object

  @return: SDIO_STATUS_SUCCESS on success.
 
  @notes: This function increments the semaphore count.

  @see also: SemaphorePend, SemaphorePendInterruptable.          
  @example: Posting a semaphore:        
        status = SemaphorePendInterruptable(&pDevice->ResourceSem);
        if (!SDIO_SUCCESS(status)) {
                .. failed, could have been interrupted
            return status;
        }  
        ... resource acquired
            // post the semaphore
        SemaphorePost(&pDevice->ResourceSem);

+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static INLINE SDIO_STATUS SemaphorePost(POS_SEMAPHORE pSem) {
    if (!ReleaseSemaphore(*pSem,1,NULL)) {
        return SDIO_STATUS_ERROR;    
    }    
    return SDIO_STATUS_SUCCESS;
}


/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Allocate a block of kernel accessible memory
  
  @function name: KernelAlloc
  @prototype: PVOID KernelAlloc(UINT size)
  @category: Support_Reference
  
  @input: size - size of memory block to allocate

  @return: pointer to the allocated memory, NULL if allocation failed
 
  @notes: For operating systems that use paging, the allocated memory is always
          non-paged memory.  Caller should only use KernelFree() to release the
          block of memory.  This call can potentially block and should only be called
          from a schedulable context.  Use KernelAllocIrqSafe() if the allocation
          must be made from a non-schedulable context.

  @see also: KernelFree, KernelAllocIrqSafe         
  @example: allocating memory:        
        pBlock = KernelAlloc(1024);
        if (pBlock == NULL) {
                .. failed, no memory
            return SDIO_STATUS_INSUFFICIENT_RESOURCES;
        }   
        
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static INLINE PVOID KernelAlloc(UINT size) {
        /* windows CE drivers are just user mode DLLs */
    PVOID pMem = malloc(size);
    if (pMem != NULL) { memset(pMem,0,size); }
    return pMem;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Free a block of kernel accessible memory.
  
  @function name: KernelFree
  @prototype: void KernelFree(PVOID ptr)
  @category: Support_Reference
  
  @input: ptr - pointer to memory allocated with KernelAlloc()

  @return: 
 
  @notes: Caller should only use KernelFree() to release memory that was allocated
          with KernelAlloc().

  @see also: KernelAlloc        
  @example: KernelFree(pBlock);
        
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static INLINE void KernelFree(PVOID ptr) {
    free(ptr);
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Allocate a block of kernel accessible memory in an IRQ-safe manner
  
  @function name: KernelAllocIrqSafe
  @prototype: PVOID KernelAllocIrqSafe(UINT size)
  @category: Support_Reference
  
  @input: size - size of memory block to allocate

  @return: pointer to the allocated memory, NULL if allocation failed
 
  @notes: This variant of KernelAlloc allows the allocation of small blocks of
          memory from an ISR or from a context where scheduling has been disabled.
          The allocations should be small as the memory is typically allocated
          from a critical heap. The caller should only use KernelFreeIrqSafe() 
          to release the block of memory.

  @see also: KernelAlloc, KernelFreeIrqSafe     
  @example: allocating memory:        
        pBlock = KernelAllocIrqSafe(16);
        if (pBlock == NULL) {
                .. failed, no memory
            return SDIO_STATUS_INSUFFICIENT_RESOURCES;
        }   
        
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static INLINE PVOID KernelAllocIrqSafe(UINT size) {
    return malloc(size);
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Free a block of kernel accessible memory.
  
  @function name: KernelFreeIrqSafe
  @prototype: void KernelFreeIrqSafe(PVOID ptr)
  @category: Support_Reference
  
  @input: ptr - pointer to memory allocated with KernelAllocIrqSafe()

  @return: 
 
  @notes: Caller should only use KernelFreeIrqSafe() to release memory that was allocated
          with KernelAllocIrqSafe().

  @see also: KernelAllocIrqSafe         
  @example: KernelFreeIrqSafe(pBlock);
        
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static INLINE void KernelFreeIrqSafe(PVOID ptr) {
    free(ptr);
}

/* error status conversions */
static INLINE SYSTEM_STATUS SDIOErrorToOSError(SDIO_STATUS status) {
    switch (status) {
        case SDIO_STATUS_SUCCESS: 
        case SDIO_STATUS_PENDING:
            return ERROR_SUCCESS;
        case SDIO_STATUS_INVALID_PARAMETER:
            return ERROR_INVALID_PARAMETER;
        case SDIO_STATUS_DEVICE_NOT_FOUND:
            return ERROR_DEVICE_NOT_AVAILABLE;
        case SDIO_STATUS_DEVICE_ERROR:
            return ERROR_BAD_DEVICE;
        case SDIO_STATUS_NO_RESOURCES:
            return ERROR_NOT_ENOUGH_MEMORY;
        case SDIO_STATUS_ERROR:    
        default:
            return ERROR_GEN_FAILURE;
    }
}
static INLINE SDIO_STATUS OSErrorToSDIOError(SYSTEM_STATUS status) {
    if (status >=0) {
        return SDIO_STATUS_SUCCESS;
    }
    return SDIO_STATUS_ERROR;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Sleep or delay the execution context for a number of milliseconds.
  
  @function name: OSSleep
  @prototype: SDIO_STATUS OSSleep(INT SleepInterval)
  @category: Support_Reference
  
  @input: SleepInterval - time in milliseconds to put the execution context to sleep

  @return: SDIO_STATUS_SUCCESS if sleep succeeded.
 
  @notes: Caller should be in a context that allows it to sleep or block.  The 
  minimum duration of sleep may be greater than 1 MS on some platforms and OSes.   

  @see also: OSSleep         
  @example: Using sleep to delay
        EnableSlotPower(pSlot);
            // wait for power to settle
        status = OSSleep(100);
        if (!SDIO_SUCCESS(status)){
            // failed..
        }

        
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static INLINE SDIO_STATUS OSSleep(INT SleepInterval) {
    Sleep(SleepInterval);
    return SDIO_STATUS_SUCCESS;
} 

    /* I/O Access macros */
#define _READ_DWORD_REG(reg)  \
        (*(volatile unsigned long * const)(reg))
#define _READ_WORD_REG(reg)  \
        (*(volatile unsigned short * const)(reg))
#define _READ_BYTE_REG(reg)  \
        (*(volatile unsigned char * const)(reg))
#define _WRITE_DWORD_REG(reg,value)  \
        (*(volatile unsigned long * const)(reg)) = (value)
#define _WRITE_WORD_REG(reg,value)  \
        (*(volatile unsigned short * const)(reg)) = (value)
#define _WRITE_BYTE_REG(reg,value)  \
        (*(volatile unsigned char * const)(reg)) = (value)


ATOMIC_FLAGS AtomicTest_SetWithMask(volatile ATOMIC_FLAGS *pValue,ATOMIC_FLAGS OrMask);
ATOMIC_FLAGS AtomicTest_ClearWithMask(volatile ATOMIC_FLAGS *pValue, ATOMIC_FLAGS AndMask);
      
    /* atomic bit operators, let the compiler build the bit mask ahead of time if possible */
#define AtomicTest_Set(pValue, BitNo) \
    AtomicTest_SetWithMask(pValue, (ATOMIC_FLAGS)(1 << (BitNo)))
    
#define AtomicTest_Clear(pValue, BitNo) \
    AtomicTest_ClearWithMask(pValue, (ATOMIC_FLAGS)(~(1 << (BitNo))))

struct _OSKERNEL_HELPER;

typedef THREAD_RETURN (*PHELPER_FUNCTION)(struct _OSKERNEL_HELPER *);

typedef struct _OSKERNEL_HELPER {
    HANDLE                  hThread;
    BOOL                    ShutDown;
    OS_SIGNAL               WakeSignal;
    PVOID                   pContext;
    PHELPER_FUNCTION        pHelperFunc;
}OSKERNEL_HELPER, *POSKERNEL_HELPER;

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Wake the helper thread
  
  @function name: SD_WAKE_OS_HELPER
  @prototype: SD_WAKE_OS_HELPER(POSKERNEL_HELPER pOSHelper)
  @category: Support_Reference
  
  @input: pOSHelper - the OS helper object

  @return: SDIO_STATUS 

  @see also: SDLIB_OSCreateHelper
         
  @example: Waking up a helper thread
        status = SD_WAKE_OS_HELPER(&pInstance->OSHelper); 

        
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SD_WAKE_OS_HELPER(p)        SignalSet(&(p)->WakeSignal)
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Obtains the context for the helper function
  
  @function name: SD_GET_OS_HELPER_CONTEXT
  @prototype: SD_GET_OS_HELPER_CONTEXT(POSKERNEL_HELPER pOSHelper)
  @category: Support_Reference
  
  @input: pOSHelper - the OS helper object

  @return: helper specific context  
  
  @notes: This macro should only be called by the function associated with
          the helper object.
          
  @see also: SDLIB_OSCreateHelper
         
  @example: Getting the helper specific context
        PMYCONTEXT pContext = (PMYCONTEXT)SD_GET_OS_HELPER_CONTEXT(pHelper); 

        
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SD_GET_OS_HELPER_CONTEXT(p)     (p)->pContext
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Check helper function shut down flag.
  
  @function name: SD_IS_HELPER_SHUTTING_DOWN
  @prototype: SD_IS_HELPER_SHUTTING_DOWN(POSKERNEL_HELPER pOSHelper)
  @category: Support_Reference
  
  @input: pOSHelper - the OS helper object

  @return: TRUE if shutting down, else FALSE 
  
  @notes: This macro should only be called by the function associated with
          the helper object.  The function should call this macro when it
          unblocks from the call to SD_WAIT_FOR_WAKEUP().  If this function 
          returns TRUE, the function should clean up and exit. 
          
  @see also: SDLIB_OSCreateHelper , SD_WAIT_FOR_WAKEUP
         
  @example: Checking for shutdown
        while(1) {
              status = SD_WAIT_FOR_WAKEUP(pHelper);
              if (!SDIO_SUCCESS(status)) {
                  break;
              }
              if (SD_IS_HELPER_SHUTTING_DOWN(pHelper)) {
                  ... shutting down
                  break;
              }
        }
        
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SD_IS_HELPER_SHUTTING_DOWN(p)   (p)->ShutDown
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Suspend and wait for wakeup signal
  
  @function name: SD_WAIT_FOR_WAKEUP
  @prototype: SD_WAIT_FOR_WAKEUP(POSKERNEL_HELPER pOSHelper)
  @category: Support_Reference
  
  @input: pOSHelper - the OS helper object

  @return: SDIO_STATUS
  
  @notes: This macro should only be called by the function associated with
          the helper object.  The function should call this function to suspend (block)
          itself and wait for a wake up signal. The function should always check 
          whether the function should exit by calling SD_IS_HELPER_SHUTTING_DOWN.
          
  @see also: SDLIB_OSCreateHelper , SD_IS_HELPER_SHUTTING_DOWN
         
  @example: block on the wake signal
        while(1) {
              status = SD_WAIT_FOR_WAKEUP(pHelper);
              if (!SDIO_SUCCESS(status)) {
                  break;
              }
              if (SD_IS_HELPER_SHUTTING_DOWN(pHelper)) {
                  ... shutting down
                  break;
              }
        }
        
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SD_WAIT_FOR_WAKEUP(p)   SignalWait(&(p)->WakeSignal); 


/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Check if module reference count is zero
  
  @function name: IsModuleRefZero
  @prototype: void IsModuleRefZero(POS_MODULE pModule)
  @category: HDK_Reference
  
  @input: pModule - module info pointer

  @return: 
 
  @notes: Host controllers should supply the module info to the busdriver to properly
          reference count the driver.  The driver should prevent unloading if
          the reference count is not zero.

  @see also:      
  @example: if (IsModuleRefZero(pDriver->Hcd.pModule)) {
                ... reference count is not zero, prevent unloading....
            }
        
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* NOTE: this function should not be done inline, to prevent the compiler from optimizing it
 * out in wait loops */
static BOOL IsModuleRefZero(POS_MODULE pModule) {
    return (0 == pModule->ReferenceCount);
}
 
    /* windows CE always runs little endian */ 
#define CT_LE16_TO_CPU_ENDIAN(x) x
#define CT_LE32_TO_CPU_ENDIAN(x) x
#define CT_CPU_ENDIAN_TO_LE16(x) x
#define CT_CPU_ENDIAN_TO_LE32(x) x

#define SWAP_16(val) ((((val) << 8) & 0xFF00) | (((val) >> 8) & 0x00FF))

#define SWAP_32(val) ((((val) << 8) & 0x00FF0000) | (((val) >> 8) & 0x0000FF00)) |         \
                     ((((val) << 24) & 0xFF000000) | (((val) >> 24) & 0x000000FF))

#define CT_CPU_ENDIAN_TO_BE16(x) SWAP_16(x)
#define CT_CPU_ENDIAN_TO_BE32(x) SWAP_32(x)
#define CT_BE16_TO_CPU_ENDIAN(x) SWAP_16(x)
#define CT_BE32_TO_CPU_ENDIAN(x) SWAP_32(x)
                                      
#define SDIO_STACK_MAX_REG_PATH  128 
#define SDIO_STACK_BASE_HKEY     HKEY_LOCAL_MACHINE
#define SDIO_STACK_REG_BASE      TEXT("\\Drivers\\CT_SDIO")
 
BOOL        SDIO_BusInit();
VOID        SDIO_BusDeinit();
SDIO_STATUS SDIO_BusLoadClients();
VOID        SDIO_BusUnloadClients();

typedef SDIO_STATUS (*PSDIO_REGISTER_FUNCTION)(PSDFUNCTION);
typedef SDIO_STATUS (*PSDIO_UNREGISTER_FUNCTION)(PSDFUNCTION);

#define SDIO_CLIENT_INIT_MAGIC 0xCAEEBEEF

typedef struct _SDIO_CLIENT_INIT_CONTEXT {
    DWORD                     Magic;
    PSDIO_REGISTER_FUNCTION   pRegisterFunction;  
    PSDIO_UNREGISTER_FUNCTION pUnregisterFunction;
} SDIO_CLIENT_INIT_CONTEXT;

#endif /* __CPSYSTEM_WINCE_H___ */

