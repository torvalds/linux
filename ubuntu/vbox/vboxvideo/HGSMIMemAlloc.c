/* $Id: HGSMIMemAlloc.cpp $ */
/** @file
 * VBox Host Guest Shared Memory Interface (HGSMI) - Memory allocator.
 */

/*
 * Copyright (C) 2014-2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/*
 * Memory allocator
 * ----------------
 *
 * Area [0; AreaSize) contains only the data, control structures are separate.
 * Block sizes are power of 2: 32B, ..., 1MB
 * Area size can be anything and will be divided initially to largest possible free blocks.
 *
 * The entire area is described by a list of 32 bit block descriptors:
 *  * bits 0..3  - order, which is log2 size of the block - 5: 2^(0+5) ... 2^(15+5) == 32B .. 1MB
 *  * bit  4     - 1 for free blocks.
 *  * bits 5..31 - block offset.
 *
 * 31 ... 5 | 4 | 3 ... 0
 *  offset    F   order
 *
 * There is a sorted collection of all block descriptors
 * (key is the block offset, bits 0...4 do not interfere with sorting).
 * Also there are lists of free blocks for each size for fast allocation.
 *
 *
 * Implementation
 * --------------
 *
 * The blocks collection is a sorted linear list.
 *
 * Initially the entire area consists of one or more largest blocks followed by smaller blocks:
 *  * 100B area - 64B block with descriptor: 0x00000011
 *                32B block with descriptor: 0x00000030
 *                4B unused
 *  * 64K area  - one 64K block with descriptor: 0x0000001C
 *  * 512K area - one 512K block with descriptor: 0x0000001F
 *
 * When allocating a new block:
 *  * larger free blocks are splitted when there are no smaller free blocks;
 *  * smaller free blocks are merged if they can build a requested larger block.
 */
#include <VBox/HGSMI/HGSMIMemAlloc.h>
#include <VBox/HGSMI/HGSMI.h>

#include <iprt/err.h>
#include <iprt/string.h>


DECLINLINE(HGSMIOFFSET) hgsmiMADescriptor(HGSMIOFFSET off, bool fFree, HGSMIOFFSET order)
{
    return (off & HGSMI_MA_DESC_OFFSET_MASK) |
           (fFree? HGSMI_MA_DESC_FREE_MASK: 0) |
           (order & HGSMI_MA_DESC_ORDER_MASK);
}

static void hgsmiMABlockFree(HGSMIMADATA *pMA, HGSMIMABLOCK *pBlock)
{
    pMA->env.pfnFree(pMA->env.pvEnv, pBlock);
}

static int hgsmiMABlockAlloc(HGSMIMADATA *pMA, HGSMIMABLOCK **ppBlock)
{
    int rc = VINF_SUCCESS;

    HGSMIMABLOCK *pBlock = (HGSMIMABLOCK *)pMA->env.pfnAlloc(pMA->env.pvEnv, sizeof(HGSMIMABLOCK));
    if (pBlock)
    {
        RT_ZERO(pBlock->nodeBlock);
        *ppBlock = pBlock;
    }
    else
    {
        rc = VERR_NO_MEMORY;
    }

    return rc;
}

/* Divide entire area to free blocks. */
static int hgsmiMAFormat(HGSMIMADATA *pMA)
{
    int rc = VINF_SUCCESS;

    /* Initial value, it will be updated in the loop below. */
    pMA->cbMaxBlock = HGSMI_MA_BLOCK_SIZE_MIN;
    pMA->cBlocks = 0;

    HGSMISIZE cbBlock = HGSMI_MA_BLOCK_SIZE_MAX;
    HGSMISIZE cbRemaining = pMA->area.cbArea;
    HGSMIOFFSET off = 0;

    while (cbBlock >= HGSMI_MA_BLOCK_SIZE_MIN)
    {
        /* Build a list of free memory blocks with u32BlockSize. */
        uint32_t cBlocks = cbRemaining / cbBlock;
        if (cBlocks > 0)
        {
            if (pMA->cbMaxBlock < cbBlock)
            {
                pMA->cbMaxBlock = cbBlock;
            }

            HGSMIOFFSET order = HGSMIMASize2Order(cbBlock);

            uint32_t i;
            for (i = 0; i < cBlocks; ++i)
            {
                /* A new free block. */
                HGSMIMABLOCK *pBlock;
                rc = hgsmiMABlockAlloc(pMA, &pBlock);
                if (RT_FAILURE(rc))
                {
                    break;
                }

                pBlock->descriptor = hgsmiMADescriptor(off, true, order);
                RTListAppend(&pMA->listBlocks, &pBlock->nodeBlock);
                ++pMA->cBlocks;

                off += cbBlock;
                cbRemaining -= cbBlock;
            }
        }

        if (RT_FAILURE(rc))
        {
            break;
        }

        cbBlock /= 2;
    }

    return rc;
}

static int hgsmiMARebuildFreeLists(HGSMIMADATA *pMA)
{
    int rc = VINF_SUCCESS;

    HGSMIMABLOCK *pIter;
    RTListForEach(&pMA->listBlocks, pIter, HGSMIMABLOCK, nodeBlock)
    {
        if (HGSMI_MA_DESC_IS_FREE(pIter->descriptor))
        {
            HGSMIOFFSET order = HGSMI_MA_DESC_ORDER(pIter->descriptor);
            RTListAppend(&pMA->aListFreeBlocks[order], &pIter->nodeFree);
        }
    }

    return rc;
}

static int hgsmiMARestore(HGSMIMADATA *pMA, HGSMIOFFSET *paDescriptors, uint32_t cDescriptors, HGSMISIZE cbMaxBlock)
{
    int rc = VINF_SUCCESS;

    pMA->cbMaxBlock = cbMaxBlock;
    pMA->cBlocks = 0;

    HGSMISIZE cbRemaining = pMA->area.cbArea;
    HGSMIOFFSET off = 0;

    uint32_t i;
    for (i = 0; i < cDescriptors; ++i)
    {
        /* Verify the descriptor. */
        HGSMISIZE cbBlock = HGSMIMAOrder2Size(HGSMI_MA_DESC_ORDER(paDescriptors[i]));
        if (   off != HGSMI_MA_DESC_OFFSET(paDescriptors[i])
            || cbBlock > cbRemaining
            || cbBlock > pMA->cbMaxBlock)
        {
            rc = VERR_INVALID_PARAMETER;
            break;
        }

        /* A new free block. */
        HGSMIMABLOCK *pBlock;
        rc = hgsmiMABlockAlloc(pMA, &pBlock);
        if (RT_FAILURE(rc))
        {
            break;
        }

        pBlock->descriptor = paDescriptors[i];
        RTListAppend(&pMA->listBlocks, &pBlock->nodeBlock);
        ++pMA->cBlocks;

        off += cbBlock;
        cbRemaining -= cbBlock;
    }

    return rc;
}

static HGSMIMABLOCK *hgsmiMAGetFreeBlock(HGSMIMADATA *pMA, HGSMIOFFSET order)
{
    HGSMIMABLOCK *pBlock = NULL;

    HGSMIOFFSET i;
    for (i = order; i < RT_ELEMENTS(pMA->aListFreeBlocks); ++i)
    {
        pBlock = RTListGetFirst(&pMA->aListFreeBlocks[i], HGSMIMABLOCK, nodeFree);
        if (pBlock)
        {
            break;
        }
    }

    if (pBlock)
    {
        AssertReturn(HGSMI_MA_DESC_IS_FREE(pBlock->descriptor), NULL);

        /* Where the block starts. */
        HGSMIOFFSET off = HGSMI_MA_DESC_OFFSET(pBlock->descriptor);

        /* 'i' is the order of the block. */
        while (i != order)
        {
            /* A larger block was found and need to be split to 2 smaller blocks. */
            HGSMIMABLOCK *pBlock2;
            int rc = hgsmiMABlockAlloc(pMA, &pBlock2);
            if (RT_FAILURE(rc))
            {
                pBlock = NULL;
                break;
            }

            /* Create 2 blocks with descreased order. */
            --i;

            /* Remove from the free list. */
            RTListNodeRemove(&pBlock->nodeFree);

            pBlock->descriptor = hgsmiMADescriptor(off, true, i);
            pBlock2->descriptor = hgsmiMADescriptor(off + HGSMIMAOrder2Size(i), true, i);

            /* Update list of all blocks by inserting pBlock2 after pBlock. */
            RTListNodeInsertAfter(&pBlock->nodeBlock, &pBlock2->nodeBlock);
            ++pMA->cBlocks;

            /* Update the free list. */
            RTListAppend(&pMA->aListFreeBlocks[i], &pBlock->nodeFree);
            RTListAppend(&pMA->aListFreeBlocks[i], &pBlock2->nodeFree);
        }
    }

    return pBlock;
}

static void hgsmiMAReformatFreeBlocks(HGSMIMADATA *pMA, HGSMIOFFSET maxId,
                                      HGSMIMABLOCK *pStart, HGSMIMABLOCK *pEnd, HGSMISIZE cbBlocks)
{
    int rc = VINF_SUCCESS;

    /*
     * Blocks starting from pStart until pEnd will be replaced with
     * another set of blocks.
     *
     * The new set will include the block with the required order.
     * Since the required order is larger than any existing block,
     * it will replace at least two existing blocks.
     * The new set will also have minimal possible number of blocks.
     * Therefore the new set will have at least one block less.
     * Blocks will be updated in place and remaining blocks will be
     * deallocated.
     */

    HGSMISIZE u32BlockSize = HGSMIMAOrder2Size(maxId);
    HGSMISIZE cbRemaining = cbBlocks;
    HGSMIOFFSET off = HGSMI_MA_DESC_OFFSET(pStart->descriptor);
    HGSMIMABLOCK *pBlock = pStart;

    while (u32BlockSize >= HGSMI_MA_BLOCK_SIZE_MIN && cbRemaining)
    {
        /* Build a list of free memory blocks with u32BlockSize. */
        uint32_t cBlocks = cbRemaining / u32BlockSize;
        if (cBlocks > 0)
        {
            HGSMIOFFSET order = HGSMIMASize2Order(u32BlockSize);

            uint32_t i;
            for (i = 0; i < cBlocks; ++i)
            {
                if (pBlock == pEnd)
                {
                    /* Should never happen because the new set of blocks is supposed to be smaller. */
                    AssertFailed();
                    rc = VERR_OUT_OF_RESOURCES;
                    break;
                }

                /* Remove from the free list. */
                RTListNodeRemove(&pBlock->nodeFree);

                pBlock->descriptor = hgsmiMADescriptor(off, true, order);

                RTListAppend(&pMA->aListFreeBlocks[order], &pBlock->nodeFree);

                off += u32BlockSize;
                cbRemaining -= u32BlockSize;

                pBlock = RTListGetNext(&pMA->listBlocks, pBlock, HGSMIMABLOCK, nodeBlock);
            }
        }

        if (RT_FAILURE(rc))
        {
            break;
        }

        u32BlockSize /= 2;
    }

    Assert(cbRemaining == 0);

    if (RT_SUCCESS(rc))
    {
        /* Remove remaining free blocks from pBlock until pEnd */
        for (;;)
        {
            bool fEnd = (pBlock == pEnd);
            HGSMIMABLOCK *pNext = RTListGetNext(&pMA->listBlocks, pBlock, HGSMIMABLOCK, nodeBlock);

            RTListNodeRemove(&pBlock->nodeFree);
            RTListNodeRemove(&pBlock->nodeBlock);
            --pMA->cBlocks;

            hgsmiMABlockFree(pMA, pBlock);

            if (fEnd)
            {
                break;
            }

            pBlock = pNext;
        }
    }
}

static void hgsmiMAQueryFreeRange(HGSMIMADATA *pMA, HGSMIMABLOCK *pBlock, HGSMISIZE cbRequired,
                                  HGSMIMABLOCK **ppStart, HGSMIMABLOCK **ppEnd, HGSMISIZE *pcbBlocks)
{
    Assert(HGSMI_MA_DESC_IS_FREE(pBlock->descriptor));

    *pcbBlocks = HGSMIMAOrder2Size(HGSMI_MA_DESC_ORDER(pBlock->descriptor));
    *ppStart = pBlock;
    *ppEnd = pBlock;

    HGSMIMABLOCK *p;
    for (;;)
    {
        p = RTListGetNext(&pMA->listBlocks, *ppEnd, HGSMIMABLOCK, nodeBlock);
        if (!p || !HGSMI_MA_DESC_IS_FREE(p->descriptor))
        {
            break;
        }
        *pcbBlocks += HGSMIMAOrder2Size(HGSMI_MA_DESC_ORDER(p->descriptor));
        *ppEnd = p;

        if (cbRequired && *pcbBlocks >= cbRequired)
        {
            return;
        }
    }
    for (;;)
    {
        p = RTListGetPrev(&pMA->listBlocks, *ppStart, HGSMIMABLOCK, nodeBlock);
        if (!p || !HGSMI_MA_DESC_IS_FREE(p->descriptor))
        {
            break;
        }
        *pcbBlocks += HGSMIMAOrder2Size(HGSMI_MA_DESC_ORDER(p->descriptor));
        *ppStart = p;

        if (cbRequired && *pcbBlocks >= cbRequired)
        {
            return;
        }
    }
}

static void hgsmiMAMergeFreeBlocks(HGSMIMADATA *pMA, HGSMIOFFSET order)
{
    /* Try to create a free block with the order from smaller free blocks. */
    if (order == 0)
    {
        /* No smaller blocks. */
        return;
    }

    HGSMISIZE cbRequired = HGSMIMAOrder2Size(order);

    /* Scan all free lists of smaller blocks.
     *
     * Get the sequence of free blocks before and after each free block.
     * If possible, re-split the sequence to get the required block and other free block(s).
     * If not possible, try the next free block.
     *
     * Free blocks are scanned from i to 0 orders.
     */
    HGSMIOFFSET i = order - 1;
    for (;;)
    {
        HGSMIMABLOCK *pIter;
        RTListForEach(&pMA->aListFreeBlocks[i], pIter, HGSMIMABLOCK, nodeFree)
        {
            Assert(HGSMI_MA_DESC_ORDER(pIter->descriptor) == i);

            HGSMISIZE cbBlocks;
            HGSMIMABLOCK *pFreeStart;
            HGSMIMABLOCK *pFreeEnd;
            hgsmiMAQueryFreeRange(pMA, pIter, cbRequired, &pFreeStart, &pFreeEnd, &cbBlocks);

            Assert((cbBlocks / HGSMI_MA_BLOCK_SIZE_MIN) * HGSMI_MA_BLOCK_SIZE_MIN == cbBlocks);

            /* Verify whether cbBlocks is enough for the requested block. */
            if (cbBlocks >= cbRequired)
            {
                /* Build new free blocks starting from the requested. */
                hgsmiMAReformatFreeBlocks(pMA, order, pFreeStart, pFreeEnd, cbBlocks);
                i = 0; /* Leave the loop. */
                break;
            }
        }

        if (i == 0)
        {
            break;
        }

        --i;
    }
}

static HGSMIOFFSET hgsmiMAAlloc(HGSMIMADATA *pMA, HGSMISIZE cb)
{
    if (cb > pMA->cbMaxBlock)
    {
        return HGSMIOFFSET_VOID;
    }

    if (cb < HGSMI_MA_BLOCK_SIZE_MIN)
    {
        cb = HGSMI_MA_BLOCK_SIZE_MIN;
    }

    HGSMIOFFSET order = HGSMIPopCnt32(cb - 1) - HGSMI_MA_DESC_ORDER_BASE;

    AssertReturn(HGSMIMAOrder2Size(order) >= cb, HGSMIOFFSET_VOID);
    AssertReturn(order < RT_ELEMENTS(pMA->aListFreeBlocks), HGSMIOFFSET_VOID);

    HGSMIMABLOCK *pBlock = hgsmiMAGetFreeBlock(pMA, order);
    if (RT_UNLIKELY(pBlock == NULL))
    {
        /* No free block with large enough size. Merge smaller free blocks and try again. */
        hgsmiMAMergeFreeBlocks(pMA, order);
        pBlock = hgsmiMAGetFreeBlock(pMA, order);
    }

    if (RT_LIKELY(pBlock != NULL))
    {
        RTListNodeRemove(&pBlock->nodeFree);
        pBlock->descriptor &= ~HGSMI_MA_DESC_FREE_MASK;
        return HGSMI_MA_DESC_OFFSET(pBlock->descriptor);
    }

    return HGSMIOFFSET_VOID;
}

static void hgsmiMAFree(HGSMIMADATA *pMA, HGSMIOFFSET off)
{
    if (off == HGSMIOFFSET_VOID)
    {
        return;
    }

    /* Find the block corresponding to the offset. */
    Assert((off / HGSMI_MA_BLOCK_SIZE_MIN) * HGSMI_MA_BLOCK_SIZE_MIN == off);

    HGSMIMABLOCK *pBlock = HGSMIMASearchOffset(pMA, off);
    if (pBlock)
    {
        if (HGSMI_MA_DESC_OFFSET(pBlock->descriptor) == off)
        {
            /* Found the right block, mark it as free. */
            pBlock->descriptor |= HGSMI_MA_DESC_FREE_MASK;
            RTListAppend(&pMA->aListFreeBlocks[HGSMI_MA_DESC_ORDER(pBlock->descriptor)], &pBlock->nodeFree);
            return;
        }
    }

    AssertFailed();
}

int HGSMIMAInit(HGSMIMADATA *pMA, const HGSMIAREA *pArea,
                HGSMIOFFSET *paDescriptors, uint32_t cDescriptors, HGSMISIZE cbMaxBlock,
                const HGSMIENV *pEnv)
{
    AssertReturn(pArea->cbArea < UINT32_C(0x80000000), VERR_INVALID_PARAMETER);
    AssertReturn(pArea->cbArea >= HGSMI_MA_BLOCK_SIZE_MIN, VERR_INVALID_PARAMETER);

    RT_ZERO(*pMA);

    HGSMISIZE cb = (pArea->cbArea / HGSMI_MA_BLOCK_SIZE_MIN) * HGSMI_MA_BLOCK_SIZE_MIN;

    int rc = HGSMIAreaInitialize(&pMA->area, pArea->pu8Base, cb, 0);
    if (RT_SUCCESS(rc))
    {
        pMA->env = *pEnv;

        uint32_t i;
        for (i = 0; i < RT_ELEMENTS(pMA->aListFreeBlocks); ++i)
        {
            RTListInit(&pMA->aListFreeBlocks[i]);
        }
        RTListInit(&pMA->listBlocks);

        if (cDescriptors)
        {
            rc = hgsmiMARestore(pMA, paDescriptors, cDescriptors, cbMaxBlock);
        }
        else
        {
            rc = hgsmiMAFormat(pMA);
        }

        if (RT_SUCCESS(rc))
        {
            rc = hgsmiMARebuildFreeLists(pMA);
        }
    }

    return rc;
}

void HGSMIMAUninit(HGSMIMADATA *pMA)
{
    HGSMIMABLOCK *pIter;
    HGSMIMABLOCK *pNext;

    RTListForEachSafe(&pMA->listBlocks, pIter, pNext, HGSMIMABLOCK, nodeBlock)
    {
        RTListNodeRemove(&pIter->nodeBlock);
        hgsmiMABlockFree(pMA, pIter);
    }

    RT_ZERO(*pMA);
}

HGSMIOFFSET HGSMIMAPointerToOffset(const HGSMIMADATA *pMA, const void *pv)
{
    if (HGSMIAreaContainsPointer(&pMA->area, pv))
    {
        return HGSMIPointerToOffset(&pMA->area, pv);
    }

    AssertFailed();
    return HGSMIOFFSET_VOID;
}

void *HGSMIMAOffsetToPointer(const HGSMIMADATA *pMA, HGSMIOFFSET off)
{
    if (HGSMIAreaContainsOffset(&pMA->area, off))
    {
        return HGSMIOffsetToPointer(&pMA->area, off);
    }

    AssertFailed();
    return NULL;
}

void *HGSMIMAAlloc(HGSMIMADATA *pMA, HGSMISIZE cb)
{
    HGSMIOFFSET off = hgsmiMAAlloc(pMA, cb);
    return HGSMIMAOffsetToPointer(pMA, off);
}

void HGSMIMAFree(HGSMIMADATA *pMA, void *pv)
{
    HGSMIOFFSET off = HGSMIMAPointerToOffset(pMA, pv);
    if (off != HGSMIOFFSET_VOID)
    {
        hgsmiMAFree(pMA, off);
    }
    else
    {
        AssertFailed();
    }
}

HGSMIMABLOCK *HGSMIMASearchOffset(HGSMIMADATA *pMA, HGSMIOFFSET off)
{
    /* Binary search in the block list for the offset. */
    HGSMIMABLOCK *pStart = RTListGetFirst(&pMA->listBlocks, HGSMIMABLOCK, nodeBlock);
    HGSMIMABLOCK *pEnd = RTListGetLast(&pMA->listBlocks, HGSMIMABLOCK, nodeBlock);
    HGSMIMABLOCK *pMiddle;

    uint32_t iStart = 0;
    uint32_t iEnd = pMA->cBlocks;
    uint32_t iMiddle;

    for (;;)
    {
        pMiddle = pStart;
        iMiddle = iStart + (iEnd - iStart) / 2;
        if (iMiddle == iStart)
        {
            break;
        }

        /* Find the block with the iMiddle index. Never go further than pEnd. */
        uint32_t i;
        for (i = iStart; i < iMiddle && pMiddle != pEnd; ++i)
        {
            pMiddle = RTListNodeGetNext(&pMiddle->nodeBlock, HGSMIMABLOCK, nodeBlock);
        }

        HGSMIOFFSET offMiddle = HGSMI_MA_DESC_OFFSET(pMiddle->descriptor);
        if (offMiddle > off)
        {
            pEnd = pMiddle;
            iEnd = iMiddle;
        }
        else
        {
            pStart = pMiddle;
            iStart = iMiddle;
        }
    }

    return pMiddle;
}


/*
 * Helper.
 */

uint32_t HGSMIPopCnt32(uint32_t u32)
{
    uint32_t c = 0;
    if (u32 > 0xFFFF) { c += 16;  u32 >>= 16; }
    if (u32 > 0xFF)   { c += 8;   u32 >>= 8;  }
    if (u32 > 0xF)    { c += 4;   u32 >>= 4;  }
    if (u32 > 0x3)    { c += 2;   u32 >>= 2;  }
    if (u32 > 0x1)    { c += 1;   u32 >>= 1;  }
    return c + u32;
}
