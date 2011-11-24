// Copyright (c) 2004-2006 Atheros Communications Inc.
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
// Portions of this code were developed with information supplied from the 
// SD Card Association Simplified Specifications. The following conditions and disclaimers may apply:
//
//  The following conditions apply to the release of the SD simplified specification (“Simplified
//  Specification”) by the SD Card Association. The Simplified Specification is a subset of the complete 
//  SD Specification which is owned by the SD Card Association. This Simplified Specification is provided 
//  on a non-confidential basis subject to the disclaimers below. Any implementation of the Simplified 
//  Specification may require a license from the SD Card Association or other third parties.
//  Disclaimers:
//  The information contained in the Simplified Specification is presented only as a standard 
//  specification for SD Cards and SD Host/Ancillary products and is provided "AS-IS" without any 
//  representations or warranties of any kind. No responsibility is assumed by the SD Card Association for 
//  any damages, any infringements of patents or other right of the SD Card Association or any third 
//  parties, which may result from its use. No license is granted by implication, estoppel or otherwise 
//  under any patent or other rights of the SD Card Association or any third party. Nothing herein shall 
//  be construed as an obligation by the SD Card Association to disclose or distribute any technical 
//  information, know-how or other confidential information to any third party.
//
//
// The initial developers of the original code are Seung Yi and Paul Lever
//
// sdio@atheros.com
//
//

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
@file: ctsystem_linux.h

@abstract: common system include file for Linux.
 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#ifndef __CPSYSTEM_LINUX_H___
#define __CPSYSTEM_LINUX_H___
 
/* #define DBG_TIMESTAMP 1 */
#define SD_TRACK_REQ 1

/* LINUX support */
#include <linux/version.h>

#ifndef KERNEL_VERSION
  #error KERNEL_VERSION macro not defined!
#endif

#ifndef  LINUX_VERSION_CODE
  #error LINUX_VERSION_CODE macro not defined!
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
#include <linux/autoconf.h>
#else
#include <generated/autoconf.h>
#endif
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/module.h>

#include <linux/slab.h>

#include <linux/interrupt.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)
#include <linux/pnp.h>
#include <asm/hardirq.h> 
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
#include <asm/semaphore.h>
#else
#include <linux/semaphore.h>
#endif
#include <asm/io.h>
#include <asm/scatterlist.h> 
#ifdef DBG_TIMESTAMP
#include <asm/timex.h>  
#endif /* DBG_TIMESTAMP */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)
#ifndef in_atomic 
    /* released version of 2.6.9 */
#include <linux/hardirq.h> 
#endif
#endif
#include <linux/delay.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#include <linux/device.h>
#endif 

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)) && (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,16))
    /* for legacy systems that have PnP enabled */
#define SDIO_USE_LINUX_PNP
#endif

/* generic types */
typedef    unsigned char    UCHAR;
typedef    unsigned char *  PUCHAR;
typedef    char             TEXT;
typedef    char *           PTEXT;
typedef    unsigned short   USHORT;
typedef    unsigned short*  PUSHORT;
typedef    unsigned int     UINT;
typedef    unsigned int*    PUINT;
typedef    int              INT;
typedef    int*             PINT;
typedef    unsigned long    ULONG;
typedef    unsigned long*   PULONG;
typedef    u8               UINT8;
typedef    u16              UINT16;
typedef    u32              UINT32;
typedef    u8*              PUINT8;
typedef    u16*             PUINT16;
typedef    u32*             PUINT32;
typedef    unsigned char *  ULONG_PTR;
typedef    void*            PVOID;
typedef    unsigned char    BOOL;
typedef    BOOL*            PBOOL;
typedef    int              SDIO_STATUS;
typedef    int              SYSTEM_STATUS;
typedef    unsigned int     EVENT_TYPE;
typedef    unsigned int     EVENT_ARG;
typedef    unsigned int*    PEVENT_TYPE;
typedef    struct semaphore OS_SEMAPHORE;
typedef    struct semaphore* POS_SEMAPHORE;
typedef    struct semaphore  OS_SIGNAL;    /* OS signals are just semaphores */
typedef    struct semaphore* POS_SIGNAL;  
typedef    spinlock_t OS_CRITICALSECTION;
typedef    spinlock_t *POS_CRITICALSECTION;  
typedef    int              SDPOWER_STATE;
typedef    unsigned long    ATOMIC_FLAGS;
typedef    INT              THREAD_RETURN;
typedef    dma_addr_t       DMA_ADDRESS;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)
typedef    struct task_struct* PKERNEL_TASK;
typedef    struct device_driver OS_DRIVER;
typedef    struct device_driver* POS_DRIVER;
typedef    struct device    OS_DEVICE;
typedef    struct device*   POS_DEVICE;

#ifdef SDIO_USE_LINUX_PNP
typedef    struct pnp_driver OS_PNPDRIVER;
typedef    struct pnp_driver* POS_PNPDRIVER;
typedef    struct pnp_dev   OS_PNPDEVICE;
typedef    struct pnp_dev*  POS_PNPDEVICE;
#else
    /* not using PnP subsystem */
typedef    PVOID            OS_PNPDRIVER;
typedef    PVOID*           POS_PNPDRIVER;
typedef    PVOID            OS_PNPDEVICE;
typedef    PVOID*           POS_PNPDEVICE;
#endif

typedef    struct module*   POS_MODULE;
#else
/* 2.4 */ 
typedef    int              PKERNEL_TASK;
typedef    PVOID            OS_DRIVER;
typedef    PVOID*           POS_DRIVER;
typedef    PVOID            OS_DEVICE;
typedef    PVOID*           POS_DEVICE;
typedef    PVOID            OS_PNPDRIVER;
typedef    PVOID*           POS_PNPDRIVER;
typedef    PVOID            OS_PNPDEVICE;
typedef    PVOID*           POS_PNPDEVICE;
typedef    struct module*   POS_MODULE;
#define    module_param(a,b,c) MODULE_PARM(a, "i")
#endif

typedef    int              CT_DEBUG_LEVEL;


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
    UINT16      Flags;      /* SDDMA_DESCRIPTION_FLAG_xxx */
    UINT16      MaxDescriptors; /* number of supported scatter gather entries */
    UINT32      MaxBytesPerDescriptor;  /* maximum bytes in a DMA descriptor entry */
    u64         Mask;              /* dma address mask */
    UINT32      AddressAlignment;  /* dma address alignment mask, least significant bits indicate illegal address bits */
    UINT32      LengthAlignment;   /* dma buffer length alignment mask, least significant bits indicate illegal length bits  */
}SDDMA_DESCRIPTION, *PSDDMA_DESCRIPTION;
typedef struct scatterlist SDDMA_DESCRIPTOR, *PSDDMA_DESCRIPTOR;

#define INLINE  inline
#define CT_PACK_STRUCT __attribute__ ((packed))

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)
#define CT_DECLARE_MODULE_PARAM_INTEGER(p)  module_param(p, int, 0644);
#else
#define CT_DECLARE_MODULE_PARAM_INTEGER(p)  MODULE_PARM(p, "i");
#endif


/* debug print macros */
//#define SDDBG_KERNEL_PRINT_LEVEL KERN_DEBUG
#define SDDBG_KERNEL_PRINT_LEVEL KERN_ALERT
#ifdef DEBUG
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Evaluation the expression and throw an assertion if false.
  
  @function name: DBG_ASSERT
  @prototype: void DBG_ASSERT(test)
  @category: Support_Reference
  @input:  test   - boolean expression
 
  @output: none

  @return: 
 
  @notes: This function can be conditionally compiled using the c-define DEBUG.
                
  @see also: DBG_PRINT
  @example: Assert test:
        count--;
        DBG_ASSERT(count >= 0);

+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define DBG_ASSERT(test) \
{                         \
    if (!(test)) {          \
        DBG_PRINT(SDDBG_ERROR, ("Debug Assert Caught, File %s, Line: %d, Test:%s \n",__FILE__, __LINE__,#test)); \
    }                     \
}
#define DBG_ASSERT_WITH_MSG(test,str) \
{                                   \
    if (!(test)) {                  \
        DBG_PRINT(SDDBG_ERROR, ("Assert:%s File %s, Line: %d \n",(str),__FILE__, __LINE__)); \
    }                     \
}

#ifdef DBG_DECLARE
CT_DEBUG_LEVEL debuglevel = DBG_DECLARE;
#ifdef DBG_TIMESTAMP
cycles_t g_lasttimestamp = 0;
#endif /* DBG_TIMESTAMP */
#else  /* DBG_DECLARE */
extern CT_DEBUG_LEVEL debuglevel;
#ifdef DBG_TIMESTAMP
extern cycles_t g_lasttimestamp;
#endif /* DBG_TIMESTAMP */
#endif /* DBG_DECLARE */

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Print a string to the debugger or console
  
  @function name: DBG_PRINT
  @prototype: void DBG_PRINT(INT Level, string)
  @category: Support_Reference
  @input:  Level - debug level for the print
 
  @output: none

  @return: 
 
  @notes: If Level is less than the current debug level, the print will be
          issued.  This function can be conditionally compiled using the c-define DEBUG.
  @see also: REL_PRINT              
  @example: DBG_PRINT(MY_DBG_LEVEL, ("Return Status: %d\r\n",status));

+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#ifdef DBG_TIMESTAMP
#define DBG_PRINT(lvl, args)\
    {if (lvl <= DBG_GET_DEBUG_LEVEL()) {\
          ulong _delta_timestamp;\
          cycles_t _last_timestamp = g_lasttimestamp;\
          g_lasttimestamp = get_cycles();\
          /* avoid 64-bit divides, to microseconds*/\
          _delta_timestamp =((ulong)(g_lasttimestamp - _last_timestamp)/(ulong)(cpu_khz/(ulong)1000));\
          printk(SDDBG_KERNEL_PRINT_LEVEL "(%lld:%ldus) ", g_lasttimestamp, _delta_timestamp);\
          printk(SDDBG_KERNEL_PRINT_LEVEL _DBG_PRINTX_ARG args);\
        }\
    }
#else /* DBG_TIMESTAMP */
#define DBG_PRINT(lvl, args)\
    {if (lvl <= DBG_GET_DEBUG_LEVEL())\
        printk(SDDBG_KERNEL_PRINT_LEVEL _DBG_PRINTX_ARG args);\
    }
#endif /* DBG_TIMESTAMP */
#else /* DEBUG */

#ifdef DBG_DECLARE
CT_DEBUG_LEVEL debuglevel = DBG_DECLARE;
#else  /* DBG_DECLARE */
extern CT_DEBUG_LEVEL debuglevel;
#endif /* DBG_DECLARE */

#define DBG_PRINT(lvl, str)
#define DBG_ASSERT(test)
#define DBG_ASSERT_WITH_MSG(test,s)
#endif /* DEBUG */

#define _DBG_PRINTX_ARG(arg...) arg /* unroll the parens around the var args*/
#define DBG_GET_DEBUG_LEVEL() debuglevel
#define DBG_SET_DEBUG_LEVEL(v) debuglevel = (v)
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
    {if (lvl <= DBG_GET_DEBUG_LEVEL())\
        printk(SDDBG_KERNEL_PRINT_LEVEL _DBG_PRINTX_ARG args);\
    }
/* debug output levels, this must be order low number to higher */
#define SDDBG_ERROR 3  
#define SDDBG_WARN  4  
#define SDDBG_DEBUG 6  
#define SDDBG_TRACE 7  

#ifdef DBG_CRIT_SECTION_RECURSE
   /* this macro thows an exception if the lock is recursively taken
    * the kernel must be configured with: CONFIG_DEBUG_SPINLOCK=y */
#define call_spin_lock(pCrit) \
{                                     \
  UINT32 unlocked = 1;                \
  if ((pCrit)->lock) {unlocked = 0;}  \
  spin_lock_bh(pCrit);                \
  if (!unlocked) {                     \
     unlocked = 0x01;                   \
     unlocked = *((volatile UINT32 *)unlocked); \
  }                                   \
}

#define call_spin_lock_irqsave(pCrit,isc) \
{                                     \
  UINT32 unlocked = 1;                \
  if ((pCrit)->lock) {unlocked = 0;}  \
  spin_lock_irqsave(pCrit,isc);                \
  if (!unlocked) {                     \
     unlocked = 0x01;                   \
     unlocked = *((volatile UINT32 *)unlocked); \
  }                                   \
}

#else
#define call_spin_lock(s) spin_lock_bh(s)
#define call_spin_lock_irqsave(s,isc) spin_lock_irqsave(s,isc)
#endif
 
#define call_spin_unlock(s) spin_unlock_bh((s))
#define call_spin_unlock_irqrestore(s,isc) spin_unlock_irqrestore(s,isc)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)
#define NonSchedulable() (in_atomic() || irqs_disabled())
#else
#define NonSchedulable() (irqs_disabled())
#endif
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
static inline SDIO_STATUS CriticalSectionInit(POS_CRITICALSECTION pCrit) {
    spin_lock_init(pCrit);
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
static inline SDIO_STATUS CriticalSectionAcquire(POS_CRITICALSECTION pCrit) {
    call_spin_lock(pCrit);
    return SDIO_STATUS_SUCCESS; 
}

// macro-tized versions
#define CriticalSectionAcquire_M(pCrit) \
    SDIO_STATUS_SUCCESS; call_spin_lock(pCrit) 
#define CriticalSectionRelease_M(pCrit) \
    SDIO_STATUS_SUCCESS; call_spin_unlock(pCrit)
    
#define CT_DECLARE_IRQ_SYNC_CONTEXT() unsigned long _ctSyncFlags

#define CriticalSectionAcquireSyncIrq(pCrit) \
    SDIO_STATUS_SUCCESS; call_spin_lock_irqsave(pCrit,_ctSyncFlags)

#define CriticalSectionReleaseSyncIrq(pCrit) \
    SDIO_STATUS_SUCCESS; call_spin_unlock_irqrestore(pCrit,_ctSyncFlags)


     
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
static inline SDIO_STATUS CriticalSectionRelease(POS_CRITICALSECTION pCrit) {
    call_spin_unlock(pCrit); 
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
static inline void CriticalSectionDelete(POS_CRITICALSECTION pCrit) {
    return; 
}

/* internal use */
static inline SDIO_STATUS SignalInitialize(POS_SIGNAL pSignal) {
    sema_init(pSignal, 0);       
    return SDIO_STATUS_SUCCESS;
}
/* internal use */
static inline void SignalDelete(POS_SIGNAL pSignal) {
    return;  
}
/* internal use */
static inline SDIO_STATUS SignalWaitInterruptible(POS_SIGNAL pSignal) {
    DBG_ASSERT_WITH_MSG(!NonSchedulable(),"SignalWaitInterruptible not allowed\n");
    if (down_interruptible(pSignal) == 0) {
        return SDIO_STATUS_SUCCESS;
    } else {
        return SDIO_STATUS_INTERRUPTED;
    }
}
/* internal use */
static inline SDIO_STATUS SignalWait(POS_SIGNAL pSignal) {
    DBG_ASSERT_WITH_MSG(!NonSchedulable(),"SignalWait not allowed\n");
    down(pSignal);
    return SDIO_STATUS_SUCCESS;
}

/* internal use */
static inline SDIO_STATUS SignalSet(POS_SIGNAL pSignal) {
    up(pSignal);
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
static inline SDIO_STATUS SemaphoreInitialize(POS_SEMAPHORE pSem, UINT value) {
    sema_init(pSem, value);
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
static inline void SemaphoreDelete(POS_SEMAPHORE pSem) {
    return;
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
static inline SDIO_STATUS SemaphorePend(POS_SEMAPHORE pSem) {
    DBG_ASSERT_WITH_MSG(!NonSchedulable(),"SemaphorePend not allowed\n");
    down(pSem);
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
static inline SDIO_STATUS SemaphorePendInterruptable(POS_SEMAPHORE pSem) {
    DBG_ASSERT_WITH_MSG(!NonSchedulable(),"SemaphorePendInterruptable not allowed\n");
    if (down_interruptible(pSem) == 0) {
        return SDIO_STATUS_SUCCESS;
    } else {
        return SDIO_STATUS_INTERRUPTED;
    }
}
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
static inline SDIO_STATUS SemaphorePost(POS_SEMAPHORE pSem) {
    DBG_ASSERT_WITH_MSG(!NonSchedulable(),"SemaphorePost not allowed\n");
    up(pSem);
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
static inline PVOID KernelAlloc(UINT size) {
    PVOID pMem = kmalloc(size, GFP_KERNEL);
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
static inline void KernelFree(PVOID ptr) {
    kfree(ptr);
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
static inline PVOID KernelAllocIrqSafe(UINT size) {
    return kmalloc(size, GFP_ATOMIC);
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
static inline void KernelFreeIrqSafe(PVOID ptr) {
    kfree(ptr);
}

/* error status conversions */
static inline SYSTEM_STATUS SDIOErrorToOSError(SDIO_STATUS status) {
    switch (status) {
        case SDIO_STATUS_SUCCESS: 
            return 0;
        case SDIO_STATUS_INVALID_PARAMETER:
            return -EINVAL;
        case SDIO_STATUS_PENDING:
            return -EAGAIN; /* try again */
        case SDIO_STATUS_DEVICE_NOT_FOUND:
            return -ENXIO;
        case SDIO_STATUS_DEVICE_ERROR:
            return -EIO;
        case SDIO_STATUS_INTERRUPTED:
            return -EINTR;
        case SDIO_STATUS_NO_RESOURCES:
            return -ENOMEM;
        case SDIO_STATUS_ERROR:    
        default:
            return -EFAULT;
    }
}
static inline SDIO_STATUS OSErrorToSDIOError(SYSTEM_STATUS status) {
    if (status >=0) {
        return SDIO_STATUS_SUCCESS;
    }
    switch (status) {
        case -EINVAL: 
            return SDIO_STATUS_INVALID_PARAMETER;
        case -ENXIO:
            return SDIO_STATUS_DEVICE_NOT_FOUND;
        case -EIO:
            return SDIO_STATUS_DEVICE_ERROR;
        case -EINTR:
            return SDIO_STATUS_INTERRUPTED;
        case -ENOMEM:
            return SDIO_STATUS_NO_RESOURCES;
        case -EFAULT:
            return SDIO_STATUS_ERROR;
        default:
            return SDIO_STATUS_ERROR;
    }
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
static inline SDIO_STATUS OSSleep(INT SleepInterval) {
    UINT32 delta; 

    DBG_ASSERT_WITH_MSG(!NonSchedulable(),"OSSleep not allowed\n");
        /* convert timeout to ticks */
    delta = (SleepInterval * HZ)/1000;
    if (delta == 0) {
        delta = 1;  
    }
    set_current_state(TASK_INTERRUPTIBLE);
    if (schedule_timeout(delta) != 0) {
        return SDIO_STATUS_INTERRUPTED;    
    }
    return SDIO_STATUS_SUCCESS;
} 

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: get the OSs device object
  
  @function name: SD_GET_OS_DEVICE
  @prototype: POS_DEVICE SD_GET_OS_DEVICE(PSDDEVICE pDevice)
  @category: Support_Reference
  
  @input: pDevice - the device on the HCD

  @return: pointer to the OSs device

  @see also:         
  @example: obtain low level device
        pFunctionContext->GpsDevice.Port.dev = SD_GET_OS_DEVICE(pDevice); 

        
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#ifdef SDIO_USE_LINUX_PNP
#define SD_GET_OS_DEVICE(pDevice) &((pDevice)->Device.dev)
#else
#define SD_GET_OS_DEVICE(pDevice)  (NULL)
#endif

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: get the HCD's OS object
  
  @function name: SD_GET_HCD_OS_DEVICE
  @prototype: POS_DEVICE SD_GET_HCD_OS_DEVICE(PSDDEVICE pDevice)
  @category: Support_Reference
  
  @input: pDevice - the device on the HCD

  @return: pointer to the HCD's os device

  @see also:         
  @example:
        
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SD_GET_HCD_OS_DEVICE(p)  (p)->pHcd->pDevice

#ifdef __iomem 
    /* new type checking in 2.6.9 */
    /* I/O Access macros */
#define _READ_DWORD_REG(reg)  \
        readl((const volatile void __iomem *)(reg))
#define _READ_WORD_REG(reg)  \
        readw((const volatile void __iomem *)(reg)) 
#define _READ_BYTE_REG(reg)  \
        readb((const volatile void __iomem *)(reg))
#define _WRITE_DWORD_REG(reg,value)  \
        writel((value),(volatile void __iomem *)(reg))
#define _WRITE_WORD_REG(reg,value)  \
        writew((value),(volatile void __iomem *)(reg))
#define _WRITE_BYTE_REG(reg,value)  \
        writeb((value),(volatile void __iomem *)(reg))
#else
    /* I/O Access macros */
#define _READ_DWORD_REG(reg)  \
        readl((reg))
#define _READ_WORD_REG(reg)  \
        readw((reg))
#define _READ_BYTE_REG(reg)  \
        readb((reg))
#define _WRITE_DWORD_REG(reg,value)  \
        writel((value),(reg))
#define _WRITE_WORD_REG(reg,value)  \
        writew((value),(reg))
#define _WRITE_BYTE_REG(reg,value)  \
        writeb((value),(reg))
#endif        
    /* atomic operators */
static inline ATOMIC_FLAGS AtomicTest_Set(volatile ATOMIC_FLAGS *pValue, INT BitNo) {
    return test_and_set_bit(BitNo,(ATOMIC_FLAGS *)pValue);    
}   
static inline ATOMIC_FLAGS AtomicTest_Clear(volatile ATOMIC_FLAGS *pValue, INT BitNo) {
    return test_and_clear_bit(BitNo,(ATOMIC_FLAGS *)pValue);    
}  

struct _OSKERNEL_HELPER;

typedef THREAD_RETURN (*PHELPER_FUNCTION)(struct _OSKERNEL_HELPER *);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25))
    /* using semaphores as signals on newer kernels is discouraged! */
#define CT_NO_HELPER_WAKE_SIGNAL
#endif

typedef struct _OSKERNEL_HELPER {
    PKERNEL_TASK            pTask;
    BOOL                    ShutDown;
#ifndef CT_NO_HELPER_WAKE_SIGNAL  
    OS_SIGNAL               WakeSignal;
#endif    
    /* Add WakeLock and WakeState to protect SMP systems
     * from race condition */
    spinlock_t              WakeLock;
    BOOL                    WakeState;

    struct completion       Completion; 
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

#ifdef CT_NO_HELPER_WAKE_SIGNAL

static inline SDIO_STATUS _SDWakeOSHelper(POSKERNEL_HELPER pOSHelper) {
    
    spin_lock_bh(&pOSHelper->WakeLock);
    {
        /* protect access here to avoid race condition with helper thread */
        pOSHelper->WakeState = TRUE;
        wake_up_process(pOSHelper->pTask);
    }
    spin_unlock_bh(&pOSHelper->WakeLock);

    return SDIO_STATUS_SUCCESS;    
}

#define SD_WAKE_OS_HELPER(p)  _SDWakeOSHelper((p))
     
#else
#define SD_WAKE_OS_HELPER(p)        SignalSet(&(p)->WakeSignal)
#endif

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

#ifdef CT_NO_HELPER_WAKE_SIGNAL

static inline SDIO_STATUS _SDWaitForWakeup(POSKERNEL_HELPER p) {
    BOOL sleepOK = FALSE;
    
    spin_lock_bh(&p->WakeLock);
    {
        /* protect access here to avoid race condition with "waking" thread */
        if(p->WakeState == FALSE) {
            sleepOK = TRUE;
            set_current_state(TASK_INTERRUPTIBLE);    
        }
    }
    spin_unlock_bh(&p->WakeLock);
    
    if(sleepOK) {
        schedule();    
    } 
        
    spin_lock_bh(&p->WakeLock);
    {        
        /* protect access here to avoid race condition with "waking" 
         * thread <protection here not required but doesn't hurt> */    
        p->WakeState = FALSE;              
        set_current_state(TASK_RUNNING);
    }
    spin_unlock_bh(&p->WakeLock);
                          
    return SDIO_STATUS_SUCCESS;          
}
#define SD_WAIT_FOR_WAKEUP(p)  _SDWaitForWakeup(p)               


#else
#define SD_WAIT_FOR_WAKEUP(p)   SignalWait(&(p)->WakeSignal); 
#endif
 
#define CT_LE16_TO_CPU_ENDIAN(x) __le16_to_cpu(x)
#define CT_LE32_TO_CPU_ENDIAN(x) __le32_to_cpu(x)
#define CT_CPU_ENDIAN_TO_LE16(x) __cpu_to_le16(x)
#define CT_CPU_ENDIAN_TO_LE32(x) __cpu_to_le32(x)

#define CT_CPU_ENDIAN_TO_BE16(x) __cpu_to_be16(x)
#define CT_CPU_ENDIAN_TO_BE32(x) __cpu_to_be32(x)
#define CT_BE16_TO_CPU_ENDIAN(x) __be16_to_cpu(x)
#define CT_BE32_TO_CPU_ENDIAN(x) __be32_to_cpu(x)
#endif /* __CPSYSTEM_LINUX_H___ */

