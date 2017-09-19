/* $Id: heapoffset.cpp $ */
/** @file
 * IPRT - An Offset Based Heap.
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
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
#define LOG_GROUP RTLOGGROUP_DEFAULT
#include <iprt/heap.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/param.h>
#include <iprt/string.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to the heap anchor block. */
typedef struct RTHEAPOFFSETINTERNAL *PRTHEAPOFFSETINTERNAL;
/** Pointer to a heap block. */
typedef struct RTHEAPOFFSETBLOCK *PRTHEAPOFFSETBLOCK;
/** Pointer to a free heap block. */
typedef struct RTHEAPOFFSETFREE *PRTHEAPOFFSETFREE;

/**
 * Structure describing a block in an offset based heap.
 *
 * If this block is allocated, it is followed by the user data.
 * If this block is free, see RTHEAPOFFSETFREE.
 */
typedef struct RTHEAPOFFSETBLOCK
{
    /** The next block in the global block list. */
    uint32_t /*PRTHEAPOFFSETBLOCK*/     offNext;
    /** The previous block in the global block list. */
    uint32_t /*PRTHEAPOFFSETBLOCK*/     offPrev;
    /** Offset into the heap of this block. Used to locate the anchor block. */
    uint32_t /*PRTHEAPOFFSETINTERNAL*/  offSelf;
    /** Flags + magic. */
    uint32_t                            fFlags;
} RTHEAPOFFSETBLOCK;
AssertCompileSize(RTHEAPOFFSETBLOCK, 16);

/** The block is free if this flag is set. When cleared it's allocated. */
#define RTHEAPOFFSETBLOCK_FLAGS_FREE        (RT_BIT_32(0))
/** The magic value. */
#define RTHEAPOFFSETBLOCK_FLAGS_MAGIC       (UINT32_C(0xabcdef00))
/** The mask that needs to be applied to RTHEAPOFFSETBLOCK::fFlags to obtain the magic value. */
#define RTHEAPOFFSETBLOCK_FLAGS_MAGIC_MASK  (~RT_BIT_32(0))

/**
 * Checks if the specified block is valid or not.
 * @returns boolean answer.
 * @param   pBlock      Pointer to a RTHEAPOFFSETBLOCK structure.
 */
#define RTHEAPOFFSETBLOCK_IS_VALID(pBlock)  \
    ( ((pBlock)->fFlags & RTHEAPOFFSETBLOCK_FLAGS_MAGIC_MASK) == RTHEAPOFFSETBLOCK_FLAGS_MAGIC )

/**
 * Checks if the specified block is valid and in use.
 * @returns boolean answer.
 * @param   pBlock      Pointer to a RTHEAPOFFSETBLOCK structure.
 */
#define RTHEAPOFFSETBLOCK_IS_VALID_USED(pBlock)  \
    ( ((pBlock)->fFlags & (RTHEAPOFFSETBLOCK_FLAGS_MAGIC_MASK | RTHEAPOFFSETBLOCK_FLAGS_FREE)) \
       == RTHEAPOFFSETBLOCK_FLAGS_MAGIC )

/**
 * Checks if the specified block is valid and free.
 * @returns boolean answer.
 * @param   pBlock      Pointer to a RTHEAPOFFSETBLOCK structure.
 */
#define RTHEAPOFFSETBLOCK_IS_VALID_FREE(pBlock)  \
    ( ((pBlock)->fFlags & (RTHEAPOFFSETBLOCK_FLAGS_MAGIC_MASK | RTHEAPOFFSETBLOCK_FLAGS_FREE)) \
       == (RTHEAPOFFSETBLOCK_FLAGS_MAGIC | RTHEAPOFFSETBLOCK_FLAGS_FREE) )

/**
 * Checks if the specified block is free or not.
 * @returns boolean answer.
 * @param   pBlock      Pointer to a valid RTHEAPOFFSETBLOCK structure.
 */
#define RTHEAPOFFSETBLOCK_IS_FREE(pBlock)   (!!((pBlock)->fFlags & RTHEAPOFFSETBLOCK_FLAGS_FREE))

/**
 * A free heap block.
 * This is an extended version of RTHEAPOFFSETBLOCK that takes the unused
 * user data to store free list pointers and a cached size value.
 */
typedef struct RTHEAPOFFSETFREE
{
    /** Core stuff. */
    RTHEAPOFFSETBLOCK               Core;
    /** Pointer to the next free block. */
    uint32_t /*PRTHEAPOFFSETFREE*/  offNext;
    /** Pointer to the previous free block. */
    uint32_t /*PRTHEAPOFFSETFREE*/  offPrev;
    /** The size of the block (excluding the RTHEAPOFFSETBLOCK part). */
    uint32_t                        cb;
    /** An alignment filler to make it a multiple of 16 bytes. */
    uint32_t                        Alignment;
} RTHEAPOFFSETFREE;
AssertCompileSize(RTHEAPOFFSETFREE, 16+16);


/**
 * The heap anchor block.
 * This structure is placed at the head of the memory block specified to RTHeapOffsetInit(),
 * which means that the first RTHEAPOFFSETBLOCK appears immediately after this structure.
 */
typedef struct RTHEAPOFFSETINTERNAL
{
    /** The typical magic (RTHEAPOFFSET_MAGIC). */
    uint32_t                        u32Magic;
    /** The heap size. (This structure is included!) */
    uint32_t                        cbHeap;
    /** The amount of free memory in the heap. */
    uint32_t                        cbFree;
    /** Free head pointer. */
    uint32_t /*PRTHEAPOFFSETFREE*/  offFreeHead;
    /** Free tail pointer. */
    uint32_t /*PRTHEAPOFFSETFREE*/  offFreeTail;
    /** Make the size of this structure 32 bytes. */
    uint32_t                        au32Alignment[3];
} RTHEAPOFFSETINTERNAL;
AssertCompileSize(RTHEAPOFFSETINTERNAL, 32);


/** The minimum allocation size. */
#define RTHEAPOFFSET_MIN_BLOCK  (sizeof(RTHEAPOFFSETBLOCK))
AssertCompile(RTHEAPOFFSET_MIN_BLOCK >= sizeof(RTHEAPOFFSETBLOCK));
AssertCompile(RTHEAPOFFSET_MIN_BLOCK >= sizeof(RTHEAPOFFSETFREE) - sizeof(RTHEAPOFFSETBLOCK));

/** The minimum and default alignment.  */
#define RTHEAPOFFSET_ALIGNMENT  (sizeof(RTHEAPOFFSETBLOCK))


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef RT_STRICT
# define RTHEAPOFFSET_STRICT 1
#endif

/**
 * Converts RTHEAPOFFSETBLOCK::offSelf into a heap anchor block pointer.
 *
 * @returns Pointer of given type.
 * @param   pBlock          The block to find the heap anchor block for.
 */
#define RTHEAPOFF_GET_ANCHOR(pBlock)    ( (PRTHEAPOFFSETINTERNAL)((uint8_t *)(pBlock) - (pBlock)->offSelf ) )


/**
 * Converts an offset to a pointer.
 *
 * All offsets are relative to the heap to make life simple.
 *
 * @returns Pointer of given type.
 * @param   pHeapInt        Pointer to the heap anchor block.
 * @param   off             The offset to convert.
 * @param   type            The desired type.
 */
#ifdef RTHEAPOFFSET_STRICT
# define RTHEAPOFF_TO_PTR_N(pHeapInt, off, type)   ( (type)rtHeapOffCheckedOffToPtr(pHeapInt, off, true /*fNull*/) )
#else
# define RTHEAPOFF_TO_PTR_N(pHeapInt, off, type)   ( (type)((off) ? (uint8_t *)(pHeapInt) + (off) : NULL) )
#endif

/**
 * Converts an offset to a pointer.
 *
 * All offsets are relative to the heap to make life simple.
 *
 * @returns Pointer of given type.
 * @param   pHeapInt        Pointer to the heap anchor block.
 * @param   off             The offset to convert.
 * @param   type            The desired type.
 */
#ifdef RTHEAPOFFSET_STRICT
# define RTHEAPOFF_TO_PTR(pHeapInt, off, type)   ( (type)rtHeapOffCheckedOffToPtr(pHeapInt, off, false /*fNull*/) )
#else
# define RTHEAPOFF_TO_PTR(pHeapInt, off, type)   ( (type)((uint8_t *)(pHeapInt) + (off)) )
#endif

/**
 * Converts a pointer to an offset.
 *
 * All offsets are relative to the heap to make life simple.
 *
 * @returns Offset into the heap.
 * @param   pHeapInt        Pointer to the heap anchor block.
 * @param   ptr             The pointer to convert.
 */
#ifdef RTHEAPOFFSET_STRICT
# define RTHEAPOFF_TO_OFF(pHeapInt, ptr)    rtHeapOffCheckedPtrToOff(pHeapInt, ptr)
#else
# define RTHEAPOFF_TO_OFF(pHeapInt, ptr)    ( (uint32_t)((ptr) ? (uintptr_t)(ptr) - (uintptr_t)(pHeapInt) : UINT32_C(0)) )
#endif

#define ASSERT_L(a, b)    AssertMsg((a) <  (b), ("a=%08x b=%08x\n", (a), (b)))
#define ASSERT_LE(a, b)   AssertMsg((a) <= (b), ("a=%08x b=%08x\n", (a), (b)))
#define ASSERT_G(a, b)    AssertMsg((a) >  (b), ("a=%08x b=%08x\n", (a), (b)))
#define ASSERT_GE(a, b)   AssertMsg((a) >= (b), ("a=%08x b=%08x\n", (a), (b)))
#define ASSERT_ALIGN(a)   AssertMsg(!((uintptr_t)(a) & (RTHEAPOFFSET_ALIGNMENT - 1)), ("a=%p\n", (uintptr_t)(a)))

#define ASSERT_PREV(pHeapInt, pBlock)  \
    do { ASSERT_ALIGN((pBlock)->offPrev); \
         if ((pBlock)->offPrev) \
         { \
             ASSERT_L((pBlock)->offPrev, RTHEAPOFF_TO_OFF(pHeapInt, pBlock)); \
             ASSERT_GE((pBlock)->offPrev, sizeof(RTHEAPOFFSETINTERNAL)); \
         } \
         else \
             Assert((pBlock) == (PRTHEAPOFFSETBLOCK)((pHeapInt) + 1)); \
    } while (0)

#define ASSERT_NEXT(pHeap, pBlock) \
    do { ASSERT_ALIGN((pBlock)->offNext); \
         if ((pBlock)->offNext) \
         { \
             ASSERT_L((pBlock)->offNext, (pHeapInt)->cbHeap); \
             ASSERT_G((pBlock)->offNext, RTHEAPOFF_TO_OFF(pHeapInt, pBlock)); \
         } \
    } while (0)

#define ASSERT_BLOCK(pHeapInt, pBlock) \
    do { AssertMsg(RTHEAPOFFSETBLOCK_IS_VALID(pBlock), ("%#x\n", (pBlock)->fFlags)); \
         AssertMsg(RTHEAPOFF_GET_ANCHOR(pBlock) == (pHeapInt), ("%p != %p\n", RTHEAPOFF_GET_ANCHOR(pBlock), (pHeapInt))); \
         ASSERT_GE(RTHEAPOFF_TO_OFF(pHeapInt, pBlock), sizeof(RTHEAPOFFSETINTERNAL)); \
         ASSERT_L( RTHEAPOFF_TO_OFF(pHeapInt, pBlock), (pHeapInt)->cbHeap); \
         ASSERT_NEXT(pHeapInt, pBlock); \
         ASSERT_PREV(pHeapInt, pBlock); \
    } while (0)

#define ASSERT_BLOCK_USED(pHeapInt, pBlock) \
    do { AssertMsg(RTHEAPOFFSETBLOCK_IS_VALID_USED((pBlock)), ("%#x\n", (pBlock)->fFlags)); \
         AssertMsg(RTHEAPOFF_GET_ANCHOR(pBlock) == (pHeapInt), ("%p != %p\n", RTHEAPOFF_GET_ANCHOR(pBlock), (pHeapInt))); \
         ASSERT_GE(RTHEAPOFF_TO_OFF(pHeapInt, pBlock), sizeof(RTHEAPOFFSETINTERNAL)); \
         ASSERT_L( RTHEAPOFF_TO_OFF(pHeapInt, pBlock), (pHeapInt)->cbHeap); \
         ASSERT_NEXT(pHeapInt, pBlock); \
         ASSERT_PREV(pHeapInt, pBlock); \
    } while (0)

#define ASSERT_FREE_PREV(pHeapInt, pBlock) \
    do { ASSERT_ALIGN((pBlock)->offPrev); \
         if ((pBlock)->offPrev) \
         { \
             ASSERT_GE((pBlock)->offPrev, (pHeapInt)->offFreeHead); \
             ASSERT_L((pBlock)->offPrev, RTHEAPOFF_TO_OFF(pHeapInt, pBlock)); \
             ASSERT_LE((pBlock)->offPrev, (pBlock)->Core.offPrev); \
         } \
         else \
             Assert((pBlock) == RTHEAPOFF_TO_PTR(pHeapInt, (pHeapInt)->offFreeHead, PRTHEAPOFFSETFREE) ); \
    } while (0)

#define ASSERT_FREE_NEXT(pHeapInt, pBlock) \
    do { ASSERT_ALIGN((pBlock)->offNext); \
         if ((pBlock)->offNext) \
         { \
             ASSERT_LE((pBlock)->offNext, (pHeapInt)->offFreeTail); \
             ASSERT_G((pBlock)->offNext, RTHEAPOFF_TO_OFF(pHeapInt, pBlock)); \
             ASSERT_GE((pBlock)->offNext, (pBlock)->Core.offNext); \
         } \
         else \
             Assert((pBlock) == RTHEAPOFF_TO_PTR(pHeapInt, (pHeapInt)->offFreeTail, PRTHEAPOFFSETFREE)); \
    } while (0)

#ifdef RTHEAPOFFSET_STRICT
# define ASSERT_FREE_CB(pHeapInt, pBlock) \
    do { size_t cbCalc = ((pBlock)->Core.offNext ? (pBlock)->Core.offNext : (pHeapInt)->cbHeap) \
                       - RTHEAPOFF_TO_OFF((pHeapInt), (pBlock)) - sizeof(RTHEAPOFFSETBLOCK); \
         AssertMsg((pBlock)->cb == cbCalc, ("cb=%#zx cbCalc=%#zx\n", (pBlock)->cb, cbCalc)); \
    } while (0)
#else
# define ASSERT_FREE_CB(pHeapInt, pBlock) do {} while (0)
#endif

/** Asserts that a free block is valid. */
#define ASSERT_BLOCK_FREE(pHeapInt, pBlock) \
    do { ASSERT_BLOCK(pHeapInt, &(pBlock)->Core); \
         Assert(RTHEAPOFFSETBLOCK_IS_VALID_FREE(&(pBlock)->Core)); \
         ASSERT_GE(RTHEAPOFF_TO_OFF(pHeapInt, pBlock), (pHeapInt)->offFreeHead); \
         ASSERT_LE(RTHEAPOFF_TO_OFF(pHeapInt, pBlock), (pHeapInt)->offFreeTail); \
         ASSERT_FREE_NEXT(pHeapInt, pBlock); \
         ASSERT_FREE_PREV(pHeapInt, pBlock); \
         ASSERT_FREE_CB(pHeapInt, pBlock); \
    } while (0)

/** Asserts that the heap anchor block is ok. */
#define ASSERT_ANCHOR(pHeapInt) \
    do { AssertPtr(pHeapInt);\
         Assert((pHeapInt)->u32Magic == RTHEAPOFFSET_MAGIC); \
    } while (0)


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifdef RTHEAPOFFSET_STRICT
static void rtHeapOffsetAssertAll(PRTHEAPOFFSETINTERNAL pHeapInt);
#endif
static PRTHEAPOFFSETBLOCK rtHeapOffsetAllocBlock(PRTHEAPOFFSETINTERNAL pHeapInt, size_t cb, size_t uAlignment);
static void rtHeapOffsetFreeBlock(PRTHEAPOFFSETINTERNAL pHeapInt, PRTHEAPOFFSETBLOCK pBlock);

#ifdef RTHEAPOFFSET_STRICT

/** Checked version of RTHEAPOFF_TO_PTR and RTHEAPOFF_TO_PTR_N. */
static void *rtHeapOffCheckedOffToPtr(PRTHEAPOFFSETINTERNAL pHeapInt, uint32_t off, bool fNull)
{
    Assert(off || fNull);
    if (!off)
        return NULL;
    AssertMsg(off < pHeapInt->cbHeap,   ("%#x %#x\n", off, pHeapInt->cbHeap));
    AssertMsg(off >= sizeof(*pHeapInt), ("%#x %#x\n", off, sizeof(*pHeapInt)));
    return (uint8_t *)pHeapInt + off;
}

/** Checked version of RTHEAPOFF_TO_OFF. */
static uint32_t rtHeapOffCheckedPtrToOff(PRTHEAPOFFSETINTERNAL pHeapInt, void *pv)
{
    if (!pv)
        return 0;
    uintptr_t off = (uintptr_t)pv - (uintptr_t)pHeapInt;
    AssertMsg(off < pHeapInt->cbHeap,   ("%#x %#x\n", off, pHeapInt->cbHeap));
    AssertMsg(off >= sizeof(*pHeapInt), ("%#x %#x\n", off, sizeof(*pHeapInt)));
    return (uint32_t)off;
}

#endif /* RTHEAPOFFSET_STRICT */



RTDECL(int) RTHeapOffsetInit(PRTHEAPOFFSET phHeap, void *pvMemory, size_t cbMemory)
{
    PRTHEAPOFFSETINTERNAL pHeapInt;
    PRTHEAPOFFSETFREE pFree;
    unsigned i;

    /*
     * Validate input. The imposed minimum heap size is just a convenient value.
     */
    AssertReturn(cbMemory >= PAGE_SIZE, VERR_INVALID_PARAMETER);
    AssertReturn(cbMemory < UINT32_MAX, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pvMemory, VERR_INVALID_POINTER);
    AssertReturn((uintptr_t)pvMemory + (cbMemory - 1) > (uintptr_t)cbMemory, VERR_INVALID_PARAMETER);

    /*
     * Place the heap anchor block at the start of the heap memory,
     * enforce 32 byte alignment of it. Also align the heap size correctly.
     */
    pHeapInt = (PRTHEAPOFFSETINTERNAL)pvMemory;
    if ((uintptr_t)pvMemory & 31)
    {
        const uintptr_t off = 32 - ((uintptr_t)pvMemory & 31);
        cbMemory -= off;
        pHeapInt = (PRTHEAPOFFSETINTERNAL)((uintptr_t)pvMemory + off);
    }
    cbMemory &= ~(RTHEAPOFFSET_ALIGNMENT - 1);


    /* Init the heap anchor block. */
    pHeapInt->u32Magic = RTHEAPOFFSET_MAGIC;
    pHeapInt->cbHeap = (uint32_t)cbMemory;
    pHeapInt->cbFree = (uint32_t)cbMemory
                     - sizeof(RTHEAPOFFSETBLOCK)
                     - sizeof(RTHEAPOFFSETINTERNAL);
    pHeapInt->offFreeTail = pHeapInt->offFreeHead = sizeof(*pHeapInt);
    for (i = 0; i < RT_ELEMENTS(pHeapInt->au32Alignment); i++)
        pHeapInt->au32Alignment[i] = UINT32_MAX;

    /* Init the single free block. */
    pFree = RTHEAPOFF_TO_PTR(pHeapInt, pHeapInt->offFreeHead, PRTHEAPOFFSETFREE);
    pFree->Core.offNext = 0;
    pFree->Core.offPrev = 0;
    pFree->Core.offSelf = pHeapInt->offFreeHead;
    pFree->Core.fFlags = RTHEAPOFFSETBLOCK_FLAGS_MAGIC | RTHEAPOFFSETBLOCK_FLAGS_FREE;
    pFree->offNext = 0;
    pFree->offPrev = 0;
    pFree->cb = pHeapInt->cbFree;

    *phHeap = pHeapInt;

#ifdef RTHEAPOFFSET_STRICT
    rtHeapOffsetAssertAll(pHeapInt);
#endif
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTHeapOffsetInit);


RTDECL(void *) RTHeapOffsetAlloc(RTHEAPOFFSET hHeap, size_t cb, size_t cbAlignment)
{
    PRTHEAPOFFSETINTERNAL pHeapInt = hHeap;
    PRTHEAPOFFSETBLOCK pBlock;

    /*
     * Validate and adjust the input.
     */
    AssertPtrReturn(pHeapInt, NULL);
    if (cb < RTHEAPOFFSET_MIN_BLOCK)
        cb = RTHEAPOFFSET_MIN_BLOCK;
    else
        cb = RT_ALIGN_Z(cb, RTHEAPOFFSET_ALIGNMENT);
    if (!cbAlignment)
        cbAlignment = RTHEAPOFFSET_ALIGNMENT;
    else
    {
        Assert(!(cbAlignment & (cbAlignment - 1)));
        Assert((cbAlignment & ~(cbAlignment - 1)) == cbAlignment);
        if (cbAlignment < RTHEAPOFFSET_ALIGNMENT)
            cbAlignment = RTHEAPOFFSET_ALIGNMENT;
    }

    /*
     * Do the allocation.
     */
    pBlock = rtHeapOffsetAllocBlock(pHeapInt, cb, cbAlignment);
    if (RT_LIKELY(pBlock))
    {
        void *pv = pBlock + 1;
        return pv;
    }
    return NULL;
}
RT_EXPORT_SYMBOL(RTHeapOffsetAlloc);


RTDECL(void *) RTHeapOffsetAllocZ(RTHEAPOFFSET hHeap, size_t cb, size_t cbAlignment)
{
    PRTHEAPOFFSETINTERNAL pHeapInt = hHeap;
    PRTHEAPOFFSETBLOCK pBlock;

    /*
     * Validate and adjust the input.
     */
    AssertPtrReturn(pHeapInt, NULL);
    if (cb < RTHEAPOFFSET_MIN_BLOCK)
        cb = RTHEAPOFFSET_MIN_BLOCK;
    else
        cb = RT_ALIGN_Z(cb, RTHEAPOFFSET_ALIGNMENT);
    if (!cbAlignment)
        cbAlignment = RTHEAPOFFSET_ALIGNMENT;
    else
    {
        Assert(!(cbAlignment & (cbAlignment - 1)));
        Assert((cbAlignment & ~(cbAlignment - 1)) == cbAlignment);
        if (cbAlignment < RTHEAPOFFSET_ALIGNMENT)
            cbAlignment = RTHEAPOFFSET_ALIGNMENT;
    }

    /*
     * Do the allocation.
     */
    pBlock = rtHeapOffsetAllocBlock(pHeapInt, cb, cbAlignment);
    if (RT_LIKELY(pBlock))
    {
        void *pv = pBlock + 1;
        memset(pv, 0, cb);
        return pv;
    }
    return NULL;
}
RT_EXPORT_SYMBOL(RTHeapOffsetAllocZ);


/**
 * Allocates a block of memory from the specified heap.
 *
 * No parameter validation or adjustment is performed.
 *
 * @returns Pointer to the allocated block.
 * @returns NULL on failure.
 *
 * @param   pHeapInt    The heap.
 * @param   cb          Size of the memory block to allocate.
 * @param   uAlignment  The alignment specifications for the allocated block.
 */
static PRTHEAPOFFSETBLOCK rtHeapOffsetAllocBlock(PRTHEAPOFFSETINTERNAL pHeapInt, size_t cb, size_t uAlignment)
{
    PRTHEAPOFFSETBLOCK  pRet = NULL;
    PRTHEAPOFFSETFREE   pFree;

    AssertReturn((pHeapInt)->u32Magic == RTHEAPOFFSET_MAGIC, NULL);
#ifdef RTHEAPOFFSET_STRICT
    rtHeapOffsetAssertAll(pHeapInt);
#endif

    /*
     * Search for a fitting block from the lower end of the heap.
     */
    for (pFree = RTHEAPOFF_TO_PTR_N(pHeapInt, pHeapInt->offFreeHead, PRTHEAPOFFSETFREE);
         pFree;
         pFree = RTHEAPOFF_TO_PTR_N(pHeapInt, pFree->offNext, PRTHEAPOFFSETFREE))
    {
        uintptr_t offAlign;
        ASSERT_BLOCK_FREE(pHeapInt, pFree);

        /*
         * Match for size and alignment.
         */
        if (pFree->cb < cb)
            continue;
        offAlign = (uintptr_t)(&pFree->Core + 1) & (uAlignment - 1);
        if (offAlign)
        {
            PRTHEAPOFFSETFREE pPrev;

            offAlign = (uintptr_t)(&pFree[1].Core + 1) & (uAlignment - 1);
            offAlign = uAlignment - offAlign;
            if (pFree->cb < cb + offAlign + sizeof(RTHEAPOFFSETFREE))
                continue;

            /*
             * Split up the free block into two, so that the 2nd is aligned as
             * per specification.
             */
            pPrev = pFree;
            pFree = (PRTHEAPOFFSETFREE)((uintptr_t)(pFree + 1) + offAlign);
            pFree->Core.offPrev = pPrev->Core.offSelf;
            pFree->Core.offNext = pPrev->Core.offNext;
            pFree->Core.offSelf = RTHEAPOFF_TO_OFF(pHeapInt, pFree);
            pFree->Core.fFlags  = RTHEAPOFFSETBLOCK_FLAGS_MAGIC | RTHEAPOFFSETBLOCK_FLAGS_FREE;
            pFree->offPrev      = pPrev->Core.offSelf;
            pFree->offNext      = pPrev->offNext;
            pFree->cb           = (pFree->Core.offNext ? pFree->Core.offNext : pHeapInt->cbHeap)
                                - pFree->Core.offSelf - sizeof(RTHEAPOFFSETBLOCK);

            pPrev->Core.offNext = pFree->Core.offSelf;
            pPrev->offNext      = pFree->Core.offSelf;
            pPrev->cb           = pFree->Core.offSelf - pPrev->Core.offSelf - sizeof(RTHEAPOFFSETBLOCK);

            if (pFree->Core.offNext)
                RTHEAPOFF_TO_PTR(pHeapInt, pFree->Core.offNext, PRTHEAPOFFSETBLOCK)->offPrev = pFree->Core.offSelf;
            if (pFree->offNext)
                RTHEAPOFF_TO_PTR(pHeapInt, pFree->Core.offNext, PRTHEAPOFFSETFREE)->offPrev = pFree->Core.offSelf;
            else
                pHeapInt->offFreeTail = pFree->Core.offSelf;

            pHeapInt->cbFree -= sizeof(RTHEAPOFFSETBLOCK);
            ASSERT_BLOCK_FREE(pHeapInt, pPrev);
            ASSERT_BLOCK_FREE(pHeapInt, pFree);
        }

        /*
         * Split off a new FREE block?
         */
        if (pFree->cb >= cb + RT_ALIGN_Z(sizeof(RTHEAPOFFSETFREE), RTHEAPOFFSET_ALIGNMENT))
        {
            /*
             * Create a new FREE block at then end of this one.
             */
            PRTHEAPOFFSETFREE   pNew = (PRTHEAPOFFSETFREE)((uintptr_t)&pFree->Core + cb + sizeof(RTHEAPOFFSETBLOCK));

            pNew->Core.offSelf = RTHEAPOFF_TO_OFF(pHeapInt, pNew);
            pNew->Core.offNext = pFree->Core.offNext;
            if (pFree->Core.offNext)
                RTHEAPOFF_TO_PTR(pHeapInt, pFree->Core.offNext, PRTHEAPOFFSETBLOCK)->offPrev = pNew->Core.offSelf;
            pNew->Core.offPrev = RTHEAPOFF_TO_OFF(pHeapInt, pFree);
            pNew->Core.fFlags = RTHEAPOFFSETBLOCK_FLAGS_MAGIC | RTHEAPOFFSETBLOCK_FLAGS_FREE;

            pNew->offNext = pFree->offNext;
            if (pNew->offNext)
                RTHEAPOFF_TO_PTR(pHeapInt, pNew->offNext, PRTHEAPOFFSETFREE)->offPrev = pNew->Core.offSelf;
            else
                pHeapInt->offFreeTail = pNew->Core.offSelf;
            pNew->offPrev = pFree->offPrev;
            if (pNew->offPrev)
                RTHEAPOFF_TO_PTR(pHeapInt, pNew->offPrev, PRTHEAPOFFSETFREE)->offNext = pNew->Core.offSelf;
            else
                pHeapInt->offFreeHead = pNew->Core.offSelf;
            pNew->cb    = (pNew->Core.offNext ? pNew->Core.offNext : pHeapInt->cbHeap) \
                        - pNew->Core.offSelf - sizeof(RTHEAPOFFSETBLOCK);
            ASSERT_BLOCK_FREE(pHeapInt, pNew);

            /*
             * Adjust and convert the old FREE node into a USED node.
             */
            pFree->Core.fFlags &= ~RTHEAPOFFSETBLOCK_FLAGS_FREE;
            pFree->Core.offNext = pNew->Core.offSelf;
            pHeapInt->cbFree -= pFree->cb;
            pHeapInt->cbFree += pNew->cb;
            pRet = &pFree->Core;
            ASSERT_BLOCK_USED(pHeapInt, pRet);
        }
        else
        {
            /*
             * Link it out of the free list.
             */
            if (pFree->offNext)
                RTHEAPOFF_TO_PTR(pHeapInt, pFree->offNext, PRTHEAPOFFSETFREE)->offPrev = pFree->offPrev;
            else
                pHeapInt->offFreeTail = pFree->offPrev;
            if (pFree->offPrev)
                RTHEAPOFF_TO_PTR(pHeapInt, pFree->offPrev, PRTHEAPOFFSETFREE)->offNext = pFree->offNext;
            else
                pHeapInt->offFreeHead = pFree->offNext;

            /*
             * Convert it to a used block.
             */
            pHeapInt->cbFree -= pFree->cb;
            pFree->Core.fFlags &= ~RTHEAPOFFSETBLOCK_FLAGS_FREE;
            pRet = &pFree->Core;
            ASSERT_BLOCK_USED(pHeapInt, pRet);
        }
        break;
    }

#ifdef RTHEAPOFFSET_STRICT
    rtHeapOffsetAssertAll(pHeapInt);
#endif
    return pRet;
}


RTDECL(void) RTHeapOffsetFree(RTHEAPOFFSET hHeap, void *pv)
{
    PRTHEAPOFFSETINTERNAL pHeapInt;
    PRTHEAPOFFSETBLOCK pBlock;

    /*
     * Validate input.
     */
    if (!pv)
        return;
    AssertPtr(pv);
    Assert(RT_ALIGN_P(pv, RTHEAPOFFSET_ALIGNMENT) == pv);

    /*
     * Get the block and heap. If in strict mode, validate these.
     */
    pBlock = (PRTHEAPOFFSETBLOCK)pv - 1;
    pHeapInt = RTHEAPOFF_GET_ANCHOR(pBlock);
    ASSERT_BLOCK_USED(pHeapInt, pBlock);
    ASSERT_ANCHOR(pHeapInt);
    Assert(pHeapInt == (PRTHEAPOFFSETINTERNAL)hHeap || !hHeap); RT_NOREF_PV(hHeap);

#ifdef RTHEAPOFFSET_FREE_POISON
    /*
     * Poison the block.
     */
    const size_t cbBlock = (pBlock->pNext ? (uintptr_t)pBlock->pNext : (uintptr_t)pHeapInt->pvEnd)
                         - (uintptr_t)pBlock - sizeof(RTHEAPOFFSETBLOCK);
    memset(pBlock + 1, RTHEAPOFFSET_FREE_POISON, cbBlock);
#endif

    /*
     * Call worker which does the actual job.
     */
    rtHeapOffsetFreeBlock(pHeapInt, pBlock);
}
RT_EXPORT_SYMBOL(RTHeapOffsetFree);


/**
 * Free a memory block.
 *
 * @param   pHeapInt       The heap.
 * @param   pBlock         The memory block to free.
 */
static void rtHeapOffsetFreeBlock(PRTHEAPOFFSETINTERNAL pHeapInt, PRTHEAPOFFSETBLOCK pBlock)
{
    PRTHEAPOFFSETFREE   pFree = (PRTHEAPOFFSETFREE)pBlock;
    PRTHEAPOFFSETFREE   pLeft;
    PRTHEAPOFFSETFREE   pRight;

#ifdef RTHEAPOFFSET_STRICT
    rtHeapOffsetAssertAll(pHeapInt);
#endif

    /*
     * Look for the closest free list blocks by walking the blocks right
     * of us (both lists are sorted by address).
     */
    pLeft = NULL;
    pRight = NULL;
    if (pHeapInt->offFreeTail)
    {
        pRight = RTHEAPOFF_TO_PTR_N(pHeapInt, pFree->Core.offNext, PRTHEAPOFFSETFREE);
        while (pRight && !RTHEAPOFFSETBLOCK_IS_FREE(&pRight->Core))
        {
            ASSERT_BLOCK(pHeapInt, &pRight->Core);
            pRight = RTHEAPOFF_TO_PTR_N(pHeapInt, pRight->Core.offNext, PRTHEAPOFFSETFREE);
        }
        if (!pRight)
            pLeft = RTHEAPOFF_TO_PTR_N(pHeapInt, pHeapInt->offFreeTail, PRTHEAPOFFSETFREE);
        else
        {
            ASSERT_BLOCK_FREE(pHeapInt, pRight);
            pLeft = RTHEAPOFF_TO_PTR_N(pHeapInt, pRight->offPrev, PRTHEAPOFFSETFREE);
        }
        if (pLeft)
            ASSERT_BLOCK_FREE(pHeapInt, pLeft);
    }
    AssertMsgReturnVoid(pLeft != pFree, ("Freed twice! pv=%p (pBlock=%p)\n", pBlock + 1, pBlock));
    ASSERT_L(RTHEAPOFF_TO_OFF(pHeapInt, pLeft), RTHEAPOFF_TO_OFF(pHeapInt, pFree));
    Assert(!pRight || (uintptr_t)pRight > (uintptr_t)pFree);
    Assert(!pLeft || RTHEAPOFF_TO_PTR_N(pHeapInt, pLeft->offNext, PRTHEAPOFFSETFREE) == pRight);

    /*
     * Insert at the head of the free block list?
     */
    if (!pLeft)
    {
        Assert(pRight == RTHEAPOFF_TO_PTR_N(pHeapInt, pHeapInt->offFreeHead, PRTHEAPOFFSETFREE));
        pFree->Core.fFlags |= RTHEAPOFFSETBLOCK_FLAGS_FREE;
        pFree->offPrev = 0;
        pFree->offNext = RTHEAPOFF_TO_OFF(pHeapInt, pRight);
        if (pRight)
            pRight->offPrev = RTHEAPOFF_TO_OFF(pHeapInt, pFree);
        else
            pHeapInt->offFreeTail = RTHEAPOFF_TO_OFF(pHeapInt, pFree);
        pHeapInt->offFreeHead = RTHEAPOFF_TO_OFF(pHeapInt, pFree);
    }
    else
    {
        /*
         * Can we merge with left hand free block?
         */
        if (pLeft->Core.offNext == RTHEAPOFF_TO_OFF(pHeapInt, pFree))
        {
            pLeft->Core.offNext = pFree->Core.offNext;
            if (pFree->Core.offNext)
                RTHEAPOFF_TO_PTR(pHeapInt, pFree->Core.offNext, PRTHEAPOFFSETBLOCK)->offPrev = RTHEAPOFF_TO_OFF(pHeapInt, pLeft);
            pHeapInt->cbFree -= pLeft->cb;
            pFree = pLeft;
        }
        /*
         * No, just link it into the free list then.
         */
        else
        {
            pFree->Core.fFlags |= RTHEAPOFFSETBLOCK_FLAGS_FREE;
            pFree->offNext = RTHEAPOFF_TO_OFF(pHeapInt, pRight);
            pFree->offPrev = RTHEAPOFF_TO_OFF(pHeapInt, pLeft);
            pLeft->offNext = RTHEAPOFF_TO_OFF(pHeapInt, pFree);
            if (pRight)
                pRight->offPrev = RTHEAPOFF_TO_OFF(pHeapInt, pFree);
            else
                pHeapInt->offFreeTail = RTHEAPOFF_TO_OFF(pHeapInt, pFree);
        }
    }

    /*
     * Can we merge with right hand free block?
     */
    if (    pRight
        &&  pRight->Core.offPrev == RTHEAPOFF_TO_OFF(pHeapInt, pFree))
    {
        /* core */
        pFree->Core.offNext = pRight->Core.offNext;
        if (pRight->Core.offNext)
            RTHEAPOFF_TO_PTR(pHeapInt, pRight->Core.offNext, PRTHEAPOFFSETBLOCK)->offPrev = RTHEAPOFF_TO_OFF(pHeapInt, pFree);

        /* free */
        pFree->offNext = pRight->offNext;
        if (pRight->offNext)
            RTHEAPOFF_TO_PTR(pHeapInt, pRight->offNext, PRTHEAPOFFSETFREE)->offPrev = RTHEAPOFF_TO_OFF(pHeapInt, pFree);
        else
            pHeapInt->offFreeTail = RTHEAPOFF_TO_OFF(pHeapInt, pFree);
        pHeapInt->cbFree -= pRight->cb;
    }

    /*
     * Calculate the size and update free stats.
     */
    pFree->cb = (pFree->Core.offNext ? pFree->Core.offNext : pHeapInt->cbHeap)
              - RTHEAPOFF_TO_OFF(pHeapInt, pFree) - sizeof(RTHEAPOFFSETBLOCK);
    pHeapInt->cbFree += pFree->cb;
    ASSERT_BLOCK_FREE(pHeapInt, pFree);

#ifdef RTHEAPOFFSET_STRICT
    rtHeapOffsetAssertAll(pHeapInt);
#endif
}


#ifdef RTHEAPOFFSET_STRICT
/**
 * Internal consistency check (relying on assertions).
 * @param   pHeapInt
 */
static void rtHeapOffsetAssertAll(PRTHEAPOFFSETINTERNAL pHeapInt)
{
    PRTHEAPOFFSETFREE pPrev = NULL;
    PRTHEAPOFFSETFREE pPrevFree = NULL;
    PRTHEAPOFFSETFREE pBlock;
    for (pBlock = (PRTHEAPOFFSETFREE)(pHeapInt + 1);
         pBlock;
         pBlock = RTHEAPOFF_TO_PTR_N(pHeapInt, pBlock->Core.offNext, PRTHEAPOFFSETFREE))
    {
        if (RTHEAPOFFSETBLOCK_IS_FREE(&pBlock->Core))
        {
            ASSERT_BLOCK_FREE(pHeapInt, pBlock);
            Assert(pBlock->offPrev == RTHEAPOFF_TO_OFF(pHeapInt, pPrevFree));
            Assert(pPrevFree || pHeapInt->offFreeHead == RTHEAPOFF_TO_OFF(pHeapInt, pBlock));
            pPrevFree = pBlock;
        }
        else
            ASSERT_BLOCK_USED(pHeapInt, &pBlock->Core);
        Assert(!pPrev || RTHEAPOFF_TO_OFF(pHeapInt, pPrev) == pBlock->Core.offPrev);
        pPrev = pBlock;
    }
    Assert(pHeapInt->offFreeTail == RTHEAPOFF_TO_OFF(pHeapInt, pPrevFree));
}
#endif


RTDECL(size_t) RTHeapOffsetSize(RTHEAPOFFSET hHeap, void *pv)
{
    PRTHEAPOFFSETINTERNAL pHeapInt;
    PRTHEAPOFFSETBLOCK pBlock;
    size_t cbBlock;

    /*
     * Validate input.
     */
    if (!pv)
        return 0;
    AssertPtrReturn(pv, 0);
    AssertReturn(RT_ALIGN_P(pv, RTHEAPOFFSET_ALIGNMENT) == pv, 0);

    /*
     * Get the block and heap. If in strict mode, validate these.
     */
    pBlock = (PRTHEAPOFFSETBLOCK)pv - 1;
    pHeapInt = RTHEAPOFF_GET_ANCHOR(pBlock);
    ASSERT_BLOCK_USED(pHeapInt, pBlock);
    ASSERT_ANCHOR(pHeapInt);
    Assert(pHeapInt == (PRTHEAPOFFSETINTERNAL)hHeap || !hHeap); RT_NOREF_PV(hHeap);

    /*
     * Calculate the block size.
     */
    cbBlock = (pBlock->offNext ? pBlock->offNext : pHeapInt->cbHeap)
            - RTHEAPOFF_TO_OFF(pHeapInt, pBlock) - sizeof(RTHEAPOFFSETBLOCK);
    return cbBlock;
}
RT_EXPORT_SYMBOL(RTHeapOffsetSize);


RTDECL(size_t) RTHeapOffsetGetHeapSize(RTHEAPOFFSET hHeap)
{
    PRTHEAPOFFSETINTERNAL pHeapInt;

    if (hHeap == NIL_RTHEAPOFFSET)
        return 0;

    pHeapInt = hHeap;
    AssertPtrReturn(pHeapInt, 0);
    ASSERT_ANCHOR(pHeapInt);
    return pHeapInt->cbHeap;
}
RT_EXPORT_SYMBOL(RTHeapOffsetGetHeapSize);


RTDECL(size_t) RTHeapOffsetGetFreeSize(RTHEAPOFFSET hHeap)
{
    PRTHEAPOFFSETINTERNAL pHeapInt;

    if (hHeap == NIL_RTHEAPOFFSET)
        return 0;

    pHeapInt = hHeap;
    AssertPtrReturn(pHeapInt, 0);
    ASSERT_ANCHOR(pHeapInt);
    return pHeapInt->cbFree;
}
RT_EXPORT_SYMBOL(RTHeapOffsetGetFreeSize);


RTDECL(void) RTHeapOffsetDump(RTHEAPOFFSET hHeap, PFNRTHEAPOFFSETPRINTF pfnPrintf)
{
    PRTHEAPOFFSETINTERNAL pHeapInt = (PRTHEAPOFFSETINTERNAL)hHeap;
    PRTHEAPOFFSETFREE pBlock;

    pfnPrintf("**** Dumping Heap %p - cbHeap=%x cbFree=%x ****\n",
              hHeap, pHeapInt->cbHeap, pHeapInt->cbFree);

    for (pBlock = (PRTHEAPOFFSETFREE)(pHeapInt + 1);
         pBlock;
         pBlock = RTHEAPOFF_TO_PTR_N(pHeapInt, pBlock->Core.offNext, PRTHEAPOFFSETFREE))
    {
        size_t cb = (pBlock->offNext ? pBlock->Core.offNext : pHeapInt->cbHeap)
                  - RTHEAPOFF_TO_OFF(pHeapInt, pBlock) - sizeof(RTHEAPOFFSETBLOCK);
        if (RTHEAPOFFSETBLOCK_IS_FREE(&pBlock->Core))
            pfnPrintf("%p  %06x FREE offNext=%06x offPrev=%06x fFlags=%#x cb=%#06x : cb=%#06x offNext=%06x offPrev=%06x\n",
                      pBlock, pBlock->Core.offSelf, pBlock->Core.offNext, pBlock->Core.offPrev, pBlock->Core.fFlags, cb,
                      pBlock->cb, pBlock->offNext, pBlock->offPrev);
        else
            pfnPrintf("%p  %06x USED offNext=%06x offPrev=%06x fFlags=%#x cb=%#06x\n",
                      pBlock, pBlock->Core.offSelf, pBlock->Core.offNext, pBlock->Core.offPrev, pBlock->Core.fFlags, cb);
    }
    pfnPrintf("**** Done dumping Heap %p ****\n", hHeap);
}
RT_EXPORT_SYMBOL(RTHeapOffsetDump);

