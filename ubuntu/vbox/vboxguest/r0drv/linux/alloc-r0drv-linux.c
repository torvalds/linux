/* $Id: alloc-r0drv-linux.c $ */
/** @file
 * IPRT - Memory Allocation, Ring-0 Driver, Linux.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "the-linux-kernel.h"
#include "internal/iprt.h"
#include <iprt/mem.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include "r0drv/alloc-r0drv.h"


#if (defined(RT_ARCH_AMD64) || defined(DOXYGEN_RUNNING)) && !defined(RTMEMALLOC_EXEC_HEAP)
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23)
/**
 * Starting with 2.6.23 we can use __get_vm_area and map_vm_area to allocate
 * memory in the moduel range.  This is preferrable to the exec heap below.
 */
#  define RTMEMALLOC_EXEC_VM_AREA
# else
/**
 * We need memory in the module range (~2GB to ~0) this can only be obtained
 * thru APIs that are not exported (see module_alloc()).
 *
 * So, we'll have to create a quick and dirty heap here using BSS memory.
 * Very annoying and it's going to restrict us!
 */
#  define RTMEMALLOC_EXEC_HEAP
# endif
#endif

#ifdef RTMEMALLOC_EXEC_HEAP
# include <iprt/heap.h>
# include <iprt/spinlock.h>
# include <iprt/errcore.h>
#endif

#include "internal/initterm.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#ifdef RTMEMALLOC_EXEC_VM_AREA
/**
 * Extended header used for headers marked with RTMEMHDR_FLAG_EXEC_VM_AREA.
 *
 * This is used with allocating executable memory, for things like generated
 * code and loaded modules.
 */
typedef struct RTMEMLNXHDREX
{
    /** The VM area for this allocation. */
    struct vm_struct   *pVmArea;
    void               *pvDummy;
    /** The header we present to the generic API. */
    RTMEMHDR            Hdr;
} RTMEMLNXHDREX;
AssertCompileSize(RTMEMLNXHDREX, 32);
/** Pointer to an extended memory header. */
typedef RTMEMLNXHDREX *PRTMEMLNXHDREX;
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef RTMEMALLOC_EXEC_HEAP
/** The heap. */
static RTHEAPSIMPLE g_HeapExec = NIL_RTHEAPSIMPLE;
/** Spinlock protecting the heap. */
static RTSPINLOCK   g_HeapExecSpinlock = NIL_RTSPINLOCK;
#endif


/**
 * API for cleaning up the heap spinlock on IPRT termination.
 * This is as RTMemExecDonate specific to AMD64 Linux/GNU.
 */
DECLHIDDEN(void) rtR0MemExecCleanup(void)
{
#ifdef RTMEMALLOC_EXEC_HEAP
    RTSpinlockDestroy(g_HeapExecSpinlock);
    g_HeapExecSpinlock = NIL_RTSPINLOCK;
#endif
}


/**
 * Donate read+write+execute memory to the exec heap.
 *
 * This API is specific to AMD64 and Linux/GNU. A kernel module that desires to
 * use RTMemExecAlloc on AMD64 Linux/GNU will have to donate some statically
 * allocated memory in the module if it wishes for GCC generated code to work.
 * GCC can only generate modules that work in the address range ~2GB to ~0
 * currently.
 *
 * The API only accept one single donation.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if the code isn't enabled.
 * @param   pvMemory    Pointer to the memory block.
 * @param   cb          The size of the memory block.
 */
RTR0DECL(int) RTR0MemExecDonate(void *pvMemory, size_t cb)
{
#ifdef RTMEMALLOC_EXEC_HEAP
    int rc;
    AssertReturn(g_HeapExec == NIL_RTHEAPSIMPLE, VERR_WRONG_ORDER);

    rc = RTSpinlockCreate(&g_HeapExecSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "RTR0MemExecDonate");
    if (RT_SUCCESS(rc))
    {
        rc = RTHeapSimpleInit(&g_HeapExec, pvMemory, cb);
        if (RT_FAILURE(rc))
            rtR0MemExecCleanup();
    }
    return rc;
#else
    RT_NOREF_PV(pvMemory); RT_NOREF_PV(cb);
    return VERR_NOT_SUPPORTED;
#endif
}
RT_EXPORT_SYMBOL(RTR0MemExecDonate);



#ifdef RTMEMALLOC_EXEC_VM_AREA
/**
 * Allocate executable kernel memory in the module range.
 *
 * @returns Pointer to a allocation header success.  NULL on failure.
 *
 * @param   cb          The size the user requested.
 */
static PRTMEMHDR rtR0MemAllocExecVmArea(size_t cb)
{
    size_t const        cbAlloc = RT_ALIGN_Z(sizeof(RTMEMLNXHDREX) + cb, PAGE_SIZE);
    size_t const        cPages  = cbAlloc >> PAGE_SHIFT;
    struct page       **papPages;
    struct vm_struct   *pVmArea;
    size_t              iPage;

    pVmArea = __get_vm_area(cbAlloc, VM_ALLOC, MODULES_VADDR, MODULES_END);
    if (!pVmArea)
        return NULL;
    pVmArea->nr_pages = 0;    /* paranoia? */
    pVmArea->pages    = NULL; /* paranoia? */

    papPages = (struct page **)kmalloc(cPages * sizeof(papPages[0]), GFP_KERNEL | __GFP_NOWARN);
    if (!papPages)
    {
        vunmap(pVmArea->addr);
        return NULL;
    }

    for (iPage = 0; iPage < cPages; iPage++)
    {
        papPages[iPage] = alloc_page(GFP_KERNEL | __GFP_HIGHMEM | __GFP_NOWARN);
        if (!papPages[iPage])
            break;
    }
    if (iPage == cPages)
    {
        /*
         * Map the pages.
         *
         * Not entirely sure we really need to set nr_pages and pages here, but
         * they provide a very convenient place for storing something we need
         * in the free function, if nothing else...
         */
# if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
        struct page **papPagesIterator = papPages;
# endif
        pVmArea->nr_pages = cPages;
        pVmArea->pages    = papPages;
        if (!map_vm_area(pVmArea, PAGE_KERNEL_EXEC,
# if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
                         &papPagesIterator
# else
                         papPages
# endif
                         ))
        {
            PRTMEMLNXHDREX pHdrEx = (PRTMEMLNXHDREX)pVmArea->addr;
            pHdrEx->pVmArea     = pVmArea;
            pHdrEx->pvDummy     = NULL;
            return &pHdrEx->Hdr;
        }
        /* bail out */
# if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
        pVmArea->nr_pages = papPagesIterator - papPages;
# endif
    }

    vunmap(pVmArea->addr);

    while (iPage-- > 0)
        __free_page(papPages[iPage]);
    kfree(papPages);

    return NULL;
}
#endif /* RTMEMALLOC_EXEC_VM_AREA */


/**
 * OS specific allocation function.
 */
DECLHIDDEN(int) rtR0MemAllocEx(size_t cb, uint32_t fFlags, PRTMEMHDR *ppHdr)
{
    PRTMEMHDR pHdr;
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Allocate.
     */
    if (fFlags & RTMEMHDR_FLAG_EXEC)
    {
        if (fFlags & RTMEMHDR_FLAG_ANY_CTX)
            return VERR_NOT_SUPPORTED;

#if defined(RT_ARCH_AMD64)
# ifdef RTMEMALLOC_EXEC_HEAP
        if (g_HeapExec != NIL_RTHEAPSIMPLE)
        {
            RTSpinlockAcquire(g_HeapExecSpinlock);
            pHdr = (PRTMEMHDR)RTHeapSimpleAlloc(g_HeapExec, cb + sizeof(*pHdr), 0);
            RTSpinlockRelease(g_HeapExecSpinlock);
            fFlags |= RTMEMHDR_FLAG_EXEC_HEAP;
        }
        else
            pHdr = NULL;

# elif defined(RTMEMALLOC_EXEC_VM_AREA)
        pHdr = rtR0MemAllocExecVmArea(cb);
        fFlags |= RTMEMHDR_FLAG_EXEC_VM_AREA;

# else  /* !RTMEMALLOC_EXEC_HEAP */
# error "you don not want to go here..."
        pHdr = (PRTMEMHDR)__vmalloc(cb + sizeof(*pHdr), GFP_KERNEL | __GFP_HIGHMEM | __GFP_NOWARN, MY_PAGE_KERNEL_EXEC);
# endif /* !RTMEMALLOC_EXEC_HEAP */

#elif defined(PAGE_KERNEL_EXEC) && defined(CONFIG_X86_PAE)
        pHdr = (PRTMEMHDR)__vmalloc(cb + sizeof(*pHdr), GFP_KERNEL | __GFP_HIGHMEM | __GFP_NOWARN, MY_PAGE_KERNEL_EXEC);
#else
        pHdr = (PRTMEMHDR)vmalloc(cb + sizeof(*pHdr));
#endif
    }
    else
    {
        if (
#if 1 /* vmalloc has serious performance issues, avoid it. */
               cb <= PAGE_SIZE*16 - sizeof(*pHdr)
#else
               cb <= PAGE_SIZE
#endif
            || (fFlags & RTMEMHDR_FLAG_ANY_CTX)
           )
        {
            fFlags |= RTMEMHDR_FLAG_KMALLOC;
            pHdr = kmalloc(cb + sizeof(*pHdr),
                           (fFlags & RTMEMHDR_FLAG_ANY_CTX_ALLOC) ? (GFP_ATOMIC | __GFP_NOWARN)
                                                                  : (GFP_KERNEL | __GFP_NOWARN));
            if (RT_UNLIKELY(   !pHdr
                            && cb > PAGE_SIZE
                            && !(fFlags & RTMEMHDR_FLAG_ANY_CTX) ))
            {
                fFlags &= ~RTMEMHDR_FLAG_KMALLOC;
                pHdr = vmalloc(cb + sizeof(*pHdr));
            }
        }
        else
            pHdr = vmalloc(cb + sizeof(*pHdr));
    }
    if (RT_UNLIKELY(!pHdr))
    {
        IPRT_LINUX_RESTORE_EFL_AC();
        return VERR_NO_MEMORY;
    }

    /*
     * Initialize.
     */
    pHdr->u32Magic  = RTMEMHDR_MAGIC;
    pHdr->fFlags    = fFlags;
    pHdr->cb        = cb;
    pHdr->cbReq     = cb;

    *ppHdr = pHdr;
    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}


/**
 * OS specific free function.
 */
DECLHIDDEN(void) rtR0MemFree(PRTMEMHDR pHdr)
{
    IPRT_LINUX_SAVE_EFL_AC();

    pHdr->u32Magic += 1;
    if (pHdr->fFlags & RTMEMHDR_FLAG_KMALLOC)
        kfree(pHdr);
#ifdef RTMEMALLOC_EXEC_HEAP
    else if (pHdr->fFlags & RTMEMHDR_FLAG_EXEC_HEAP)
    {
        RTSpinlockAcquire(g_HeapExecSpinlock);
        RTHeapSimpleFree(g_HeapExec, pHdr);
        RTSpinlockRelease(g_HeapExecSpinlock);
    }
#endif
#ifdef RTMEMALLOC_EXEC_VM_AREA
    else if (pHdr->fFlags & RTMEMHDR_FLAG_EXEC_VM_AREA)
    {
        PRTMEMLNXHDREX pHdrEx    = RT_FROM_MEMBER(pHdr, RTMEMLNXHDREX, Hdr);
        size_t         iPage     = pHdrEx->pVmArea->nr_pages;
        struct page  **papPages  = pHdrEx->pVmArea->pages;
        void          *pvMapping = pHdrEx->pVmArea->addr;

        vunmap(pvMapping);

        while (iPage-- > 0)
            __free_page(papPages[iPage]);
        kfree(papPages);
    }
#endif
    else
        vfree(pHdr);

    IPRT_LINUX_RESTORE_EFL_AC();
}



/**
 * Compute order. Some functions allocate 2^order pages.
 *
 * @returns order.
 * @param   cPages      Number of pages.
 */
static int CalcPowerOf2Order(unsigned long cPages)
{
    int             iOrder;
    unsigned long   cTmp;

    for (iOrder = 0, cTmp = cPages; cTmp >>= 1; ++iOrder)
        ;
    if (cPages & ~(1 << iOrder))
        ++iOrder;

    return iOrder;
}


/**
 * Allocates physical contiguous memory (below 4GB).
 * The allocation is page aligned and the content is undefined.
 *
 * @returns Pointer to the memory block. This is page aligned.
 * @param   pPhys   Where to store the physical address.
 * @param   cb      The allocation size in bytes. This is always
 *                  rounded up to PAGE_SIZE.
 */
RTR0DECL(void *) RTMemContAlloc(PRTCCPHYS pPhys, size_t cb)
{
    int             cOrder;
    unsigned        cPages;
    struct page    *paPages;
    void           *pvRet;
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * validate input.
     */
    Assert(VALID_PTR(pPhys));
    Assert(cb > 0);

    /*
     * Allocate page pointer array.
     */
    cb = RT_ALIGN_Z(cb, PAGE_SIZE);
    cPages = cb >> PAGE_SHIFT;
    cOrder = CalcPowerOf2Order(cPages);
#if (defined(RT_ARCH_AMD64) || defined(CONFIG_X86_PAE)) && defined(GFP_DMA32)
    /* ZONE_DMA32: 0-4GB */
    paPages = alloc_pages(GFP_DMA32 | __GFP_NOWARN, cOrder);
    if (!paPages)
#endif
#ifdef RT_ARCH_AMD64
        /* ZONE_DMA; 0-16MB */
        paPages = alloc_pages(GFP_DMA | __GFP_NOWARN, cOrder);
#else
        /* ZONE_NORMAL: 0-896MB */
        paPages = alloc_pages(GFP_USER | __GFP_NOWARN, cOrder);
#endif
    if (paPages)
    {
        /*
         * Reserve the pages and mark them executable.
         */
        unsigned iPage;
        for (iPage = 0; iPage < cPages; iPage++)
        {
            Assert(!PageHighMem(&paPages[iPage]));
            if (iPage + 1 < cPages)
            {
                AssertMsg(          (uintptr_t)phys_to_virt(page_to_phys(&paPages[iPage])) + PAGE_SIZE
                                ==  (uintptr_t)phys_to_virt(page_to_phys(&paPages[iPage + 1]))
                          &&        page_to_phys(&paPages[iPage]) + PAGE_SIZE
                                ==  page_to_phys(&paPages[iPage + 1]),
                          ("iPage=%i cPages=%u [0]=%#llx,%p [1]=%#llx,%p\n", iPage, cPages,
                           (long long)page_to_phys(&paPages[iPage]),     phys_to_virt(page_to_phys(&paPages[iPage])),
                           (long long)page_to_phys(&paPages[iPage + 1]), phys_to_virt(page_to_phys(&paPages[iPage + 1])) ));
            }

            SetPageReserved(&paPages[iPage]);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 4, 20) /** @todo find the exact kernel where change_page_attr was introduced. */
            MY_SET_PAGES_EXEC(&paPages[iPage], 1);
#endif
        }
        *pPhys = page_to_phys(paPages);
        pvRet = phys_to_virt(page_to_phys(paPages));
    }
    else
        pvRet = NULL;

    IPRT_LINUX_RESTORE_EFL_AC();
    return pvRet;
}
RT_EXPORT_SYMBOL(RTMemContAlloc);


/**
 * Frees memory allocated using RTMemContAlloc().
 *
 * @param   pv      Pointer to return from RTMemContAlloc().
 * @param   cb      The cb parameter passed to RTMemContAlloc().
 */
RTR0DECL(void) RTMemContFree(void *pv, size_t cb)
{
    if (pv)
    {
        int             cOrder;
        unsigned        cPages;
        unsigned        iPage;
        struct page    *paPages;
        IPRT_LINUX_SAVE_EFL_AC();

        /* validate */
        AssertMsg(!((uintptr_t)pv & PAGE_OFFSET_MASK), ("pv=%p\n", pv));
        Assert(cb > 0);

        /* calc order and get pages */
        cb = RT_ALIGN_Z(cb, PAGE_SIZE);
        cPages = cb >> PAGE_SHIFT;
        cOrder = CalcPowerOf2Order(cPages);
        paPages = virt_to_page(pv);

        /*
         * Restore page attributes freeing the pages.
         */
        for (iPage = 0; iPage < cPages; iPage++)
        {
            ClearPageReserved(&paPages[iPage]);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 4, 20) /** @todo find the exact kernel where change_page_attr was introduced. */
            MY_SET_PAGES_NOEXEC(&paPages[iPage], 1);
#endif
        }
        __free_pages(paPages, cOrder);
        IPRT_LINUX_RESTORE_EFL_AC();
    }
}
RT_EXPORT_SYMBOL(RTMemContFree);

