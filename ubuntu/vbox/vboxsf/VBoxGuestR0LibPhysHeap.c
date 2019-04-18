/* $Id: VBoxGuestR0LibPhysHeap.cpp $ */
/** @file
 * VBoxGuestLibR0 - Physical memory heap.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "VBoxGuestR0LibInternal.h"

#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/alloc.h>

/* Physical memory heap consists of double linked list
 * of chunks. Memory blocks are allocated inside these chunks
 * and are members of Allocated and Free double linked lists.
 *
 * When allocating a block, we search in Free linked
 * list for a suitable free block. If there is no such block,
 * a new chunk is allocated and the new block is taken from
 * the new chunk as the only chunk-sized free block.
 * Allocated block is excluded from the Free list and goes to
 * Alloc list.
 *
 * When freeing block, we check the pointer and then
 * exclude block from Alloc list and move it to free list.
 *
 * For each chunk we maintain the allocated blocks counter.
 * if 2 (or more) entire chunks are free they are immediately
 * deallocated, so we always have at most 1 free chunk.
 *
 * When freeing blocks, two subsequent free blocks are always
 * merged together. Current implementation merges blocks only
 * when there is a block after the just freed one.
 *
 */

#define VBGL_PH_ASSERT      Assert
#define VBGL_PH_ASSERTMsg   AssertMsg

// #define DUMPHEAP

#ifdef DUMPHEAP
# define VBGL_PH_dprintf(a) RTAssertMsg2Weak a
#else
# define VBGL_PH_dprintf(a)
#endif

/* Heap block signature */
#define VBGL_PH_BLOCKSIGNATURE (0xADDBBBBB)


/* Heap chunk signature */
#define VBGL_PH_CHUNKSIGNATURE (0xADDCCCCC)
/* Heap chunk allocation unit */
#define VBGL_PH_CHUNKSIZE (0x10000)

/* Heap block bit flags */
#define VBGL_PH_BF_ALLOCATED (0x1)

struct _VBGLPHYSHEAPBLOCK
{
    uint32_t u32Signature;

    /* Size of user data in the block. Does not include the block header. */
    uint32_t cbDataSize;

    uint32_t fu32Flags;

    struct _VBGLPHYSHEAPBLOCK *pNext;
    struct _VBGLPHYSHEAPBLOCK *pPrev;

    struct _VBGLPHYSHEAPCHUNK *pChunk;
};

struct _VBGLPHYSHEAPCHUNK
{
    uint32_t u32Signature;

    /* Size of the chunk. Includes the chunk header. */
    uint32_t cbSize;

    /* Physical address of the chunk */
    uint32_t physAddr;

    /* Number of allocated blocks in the chunk */
    int32_t cAllocatedBlocks;

    struct _VBGLPHYSHEAPCHUNK *pNext;
    struct _VBGLPHYSHEAPCHUNK *pPrev;
};


#ifndef DUMPHEAP
#define dumpheap(a)
#else
void dumpheap (char *point)
{
   VBGL_PH_dprintf(("VBGL_PH dump at '%s'\n", point));

   VBGL_PH_dprintf(("Chunks:\n"));

   VBGLPHYSHEAPCHUNK *pChunk = g_vbgldata.pChunkHead;

   while (pChunk)
   {
       VBGL_PH_dprintf(("%p: pNext = %p, pPrev = %p, sign = %08X, size = %8d, allocated = %8d, phys = %08X\n",
                        pChunk, pChunk->pNext, pChunk->pPrev, pChunk->u32Signature, pChunk->cbSize, pChunk->cAllocatedBlocks, pChunk->physAddr));

       pChunk = pChunk->pNext;
   }

   VBGL_PH_dprintf(("Allocated blocks:\n"));

   VBGLPHYSHEAPBLOCK *pBlock = g_vbgldata.pAllocBlocksHead;

   while (pBlock)
   {
       VBGL_PH_dprintf(("%p: pNext = %p, pPrev = %p, sign = %08X, size = %8d, flags = %08X, pChunk = %p\n",
                        pBlock, pBlock->pNext, pBlock->pPrev, pBlock->u32Signature, pBlock->cbDataSize, pBlock->fu32Flags, pBlock->pChunk));

       pBlock = pBlock->pNext;
   }

   VBGL_PH_dprintf(("Free blocks:\n"));

   pBlock = g_vbgldata.pFreeBlocksHead;

   while (pBlock)
   {
       VBGL_PH_dprintf(("%p: pNext = %p, pPrev = %p, sign = %08X, size = %8d, flags = %08X, pChunk = %p\n",
                        pBlock, pBlock->pNext, pBlock->pPrev, pBlock->u32Signature, pBlock->cbDataSize, pBlock->fu32Flags, pBlock->pChunk));

       pBlock = pBlock->pNext;
   }

   VBGL_PH_dprintf(("VBGL_PH dump at '%s' done\n", point));
}
#endif


DECLINLINE(void *) vbglPhysHeapBlock2Data (VBGLPHYSHEAPBLOCK *pBlock)
{
    return (void *)(pBlock? (char *)pBlock + sizeof (VBGLPHYSHEAPBLOCK): NULL);
}

DECLINLINE(VBGLPHYSHEAPBLOCK *) vbglPhysHeapData2Block (void *p)
{
    VBGLPHYSHEAPBLOCK *pBlock = (VBGLPHYSHEAPBLOCK *)(p? (char *)p - sizeof (VBGLPHYSHEAPBLOCK): NULL);

    VBGL_PH_ASSERTMsg(pBlock == NULL || pBlock->u32Signature == VBGL_PH_BLOCKSIGNATURE,
                     ("pBlock->u32Signature = %08X\n", pBlock->u32Signature));

    return pBlock;
}

DECLINLINE(int) vbglPhysHeapEnter (void)
{
    int rc = RTSemFastMutexRequest(g_vbgldata.mutexHeap);

    VBGL_PH_ASSERTMsg(RT_SUCCESS(rc),
                     ("Failed to request heap mutex, rc = %Rrc\n", rc));

    return rc;
}

DECLINLINE(void) vbglPhysHeapLeave (void)
{
    RTSemFastMutexRelease(g_vbgldata.mutexHeap);
}


static void vbglPhysHeapInitBlock (VBGLPHYSHEAPBLOCK *pBlock, VBGLPHYSHEAPCHUNK *pChunk, uint32_t cbDataSize)
{
    VBGL_PH_ASSERT(pBlock != NULL);
    VBGL_PH_ASSERT(pChunk != NULL);

    pBlock->u32Signature = VBGL_PH_BLOCKSIGNATURE;
    pBlock->cbDataSize   = cbDataSize;
    pBlock->fu32Flags    = 0;
    pBlock->pNext        = NULL;
    pBlock->pPrev        = NULL;
    pBlock->pChunk       = pChunk;
}


static void vbglPhysHeapInsertBlock (VBGLPHYSHEAPBLOCK *pInsertAfter, VBGLPHYSHEAPBLOCK *pBlock)
{
    VBGL_PH_ASSERTMsg(pBlock->pNext == NULL,
                     ("pBlock->pNext = %p\n", pBlock->pNext));
    VBGL_PH_ASSERTMsg(pBlock->pPrev == NULL,
                     ("pBlock->pPrev = %p\n", pBlock->pPrev));

    if (pInsertAfter)
    {
        pBlock->pNext = pInsertAfter->pNext;
        pBlock->pPrev = pInsertAfter;

        if (pInsertAfter->pNext)
        {
            pInsertAfter->pNext->pPrev = pBlock;
        }

        pInsertAfter->pNext = pBlock;
    }
    else
    {
        /* inserting to head of list */
        pBlock->pPrev = NULL;

        if (pBlock->fu32Flags & VBGL_PH_BF_ALLOCATED)
        {
            pBlock->pNext = g_vbgldata.pAllocBlocksHead;

            if (g_vbgldata.pAllocBlocksHead)
            {
                g_vbgldata.pAllocBlocksHead->pPrev = pBlock;
            }

            g_vbgldata.pAllocBlocksHead = pBlock;
        }
        else
        {
            pBlock->pNext = g_vbgldata.pFreeBlocksHead;

            if (g_vbgldata.pFreeBlocksHead)
            {
                g_vbgldata.pFreeBlocksHead->pPrev = pBlock;
            }

            g_vbgldata.pFreeBlocksHead = pBlock;
        }
    }
}

static void vbglPhysHeapExcludeBlock (VBGLPHYSHEAPBLOCK *pBlock)
{
    if (pBlock->pNext)
    {
        pBlock->pNext->pPrev = pBlock->pPrev;
    }
    else
    {
        /* this is tail of list but we do not maintain tails of block lists.
         * so do nothing.
         */
        ;
    }

    if (pBlock->pPrev)
    {
        pBlock->pPrev->pNext = pBlock->pNext;
    }
    else
    {
        /* this is head of list but we do not maintain tails of block lists. */
        if (pBlock->fu32Flags & VBGL_PH_BF_ALLOCATED)
        {
            g_vbgldata.pAllocBlocksHead = pBlock->pNext;
        }
        else
        {
            g_vbgldata.pFreeBlocksHead = pBlock->pNext;
        }
    }

    pBlock->pNext = NULL;
    pBlock->pPrev = NULL;
}

static VBGLPHYSHEAPBLOCK *vbglPhysHeapChunkAlloc (uint32_t cbSize)
{
    RTCCPHYS physAddr;
    VBGLPHYSHEAPCHUNK *pChunk;
    VBGLPHYSHEAPBLOCK *pBlock;
    VBGL_PH_dprintf(("Allocating new chunk of size %d\n", cbSize));

    /* Compute chunk size to allocate */
    if (cbSize < VBGL_PH_CHUNKSIZE)
    {
        /* Includes case of block size 0 during initialization */
        cbSize = VBGL_PH_CHUNKSIZE;
    }
    else
    {
        /* Round up to next chunk size, which must be power of 2 */
        cbSize = (cbSize + (VBGL_PH_CHUNKSIZE - 1)) & ~(VBGL_PH_CHUNKSIZE - 1);
    }

    physAddr = 0;
    /* This function allocates physical contiguous memory (below 4GB) according to the IPRT docs.
     * Address < 4G is required for the port IO.
     */
    pChunk = (VBGLPHYSHEAPCHUNK *)RTMemContAlloc (&physAddr, cbSize);

    if (!pChunk)
    {
        LogRel(("vbglPhysHeapChunkAlloc: failed to alloc %u contiguous bytes.\n", cbSize));
        return NULL;
    }

    AssertRelease(physAddr < _4G && physAddr + cbSize <= _4G);

    pChunk->u32Signature     = VBGL_PH_CHUNKSIGNATURE;
    pChunk->cbSize           = cbSize;
    pChunk->physAddr         = (uint32_t)physAddr;
    pChunk->cAllocatedBlocks = 0;
    pChunk->pNext            = g_vbgldata.pChunkHead;
    pChunk->pPrev            = NULL;

    /* Initialize the free block, which now occupies entire chunk. */
    pBlock = (VBGLPHYSHEAPBLOCK *)((char *)pChunk + sizeof (VBGLPHYSHEAPCHUNK));

    vbglPhysHeapInitBlock (pBlock, pChunk, cbSize - sizeof (VBGLPHYSHEAPCHUNK) - sizeof (VBGLPHYSHEAPBLOCK));

    vbglPhysHeapInsertBlock (NULL, pBlock);

    g_vbgldata.pChunkHead = pChunk;

    VBGL_PH_dprintf(("Allocated chunk %p, block = %p size=%x\n", pChunk, pBlock, cbSize));

    return pBlock;
}


static void vbglPhysHeapChunkDelete (VBGLPHYSHEAPCHUNK *pChunk)
{
    char *p;
    VBGL_PH_ASSERT(pChunk != NULL);
    VBGL_PH_ASSERTMsg(pChunk->u32Signature == VBGL_PH_CHUNKSIGNATURE,
                     ("pChunk->u32Signature = %08X\n", pChunk->u32Signature));

    VBGL_PH_dprintf(("Deleting chunk %p size %x\n", pChunk, pChunk->cbSize));

    /* first scan the chunk and exclude all blocks from lists */

    p = (char *)pChunk + sizeof (VBGLPHYSHEAPCHUNK);

    while (p < (char *)pChunk + pChunk->cbSize)
    {
        VBGLPHYSHEAPBLOCK *pBlock = (VBGLPHYSHEAPBLOCK *)p;

        p += pBlock->cbDataSize + sizeof (VBGLPHYSHEAPBLOCK);

        vbglPhysHeapExcludeBlock (pBlock);
    }

    VBGL_PH_ASSERTMsg(p == (char *)pChunk + pChunk->cbSize,
                      ("p = %p, (char *)pChunk + pChunk->cbSize = %p, pChunk->cbSize = %08X\n",
                       p, (char *)pChunk + pChunk->cbSize, pChunk->cbSize));

    /* Exclude chunk from the chunk list */
    if (pChunk->pNext)
    {
        pChunk->pNext->pPrev = pChunk->pPrev;
    }
    else
    {
        /* we do not maintain tail */
        ;
    }

    if (pChunk->pPrev)
    {
        pChunk->pPrev->pNext = pChunk->pNext;
    }
    else
    {
        /* the chunk was head */
        g_vbgldata.pChunkHead = pChunk->pNext;
    }

    RTMemContFree (pChunk, pChunk->cbSize);
}


DECLR0VBGL(void *) VbglR0PhysHeapAlloc (uint32_t cbSize)
{
    VBGLPHYSHEAPBLOCK *pBlock, *pIter;
    int rc = vbglPhysHeapEnter ();

    if (RT_FAILURE(rc))
        return NULL;

    dumpheap ("pre alloc");

    /*
     * Search the free list.  We do this in linear fashion as we don't expect
     * there to be many blocks in the heap.
     */

    pBlock = NULL;
    if (cbSize <= PAGE_SIZE / 4 * 3)
    {
        /* Smaller than 3/4 page:  Prefer a free block that can keep the request within a single page,
           so HGCM processing in VMMDev can use page locks instead of several reads and writes. */

        VBGLPHYSHEAPBLOCK *pFallback = NULL;
        for (pIter = g_vbgldata.pFreeBlocksHead; pIter != NULL; pIter = pIter->pNext)
            if (pIter->cbDataSize >= cbSize)
            {
                if (pIter->cbDataSize == cbSize)
                {
                    if (PAGE_SIZE - ((uintptr_t)vbglPhysHeapBlock2Data(pIter) & PAGE_OFFSET_MASK) >= cbSize)
                    {
                        pBlock = pIter;
                        break;
                    }
                    pFallback = pIter;
                }
                else
                {
                    if (!pFallback || pIter->cbDataSize < pFallback->cbDataSize)
                        pFallback = pIter;
                    if (PAGE_SIZE - ((uintptr_t)vbglPhysHeapBlock2Data(pIter) & PAGE_OFFSET_MASK) >= cbSize)
                        if (!pBlock || pIter->cbDataSize < pBlock->cbDataSize)
                            pBlock = pIter;
                }
            }

        if (!pBlock)
            pBlock = pFallback;
    }
    else
    {
        /* Large than 3/4 page:  Find smallest free list match. */

        for (pIter = g_vbgldata.pFreeBlocksHead; pIter != NULL; pIter = pIter->pNext)
            if (pIter->cbDataSize >= cbSize)
            {
                if (pIter->cbDataSize == cbSize)
                {
                    /* Exact match - we're done! */
                    pBlock = pIter;
                    break;
                }

                /* Looking for a free block with nearest size. */
                if (!pBlock || pIter->cbDataSize < pBlock->cbDataSize)
                    pBlock = pIter;
            }
    }

    if (!pBlock)
    {
        /* No free blocks, allocate a new chunk,
         * the only free block of the chunk will
         * be returned.
         */
        pBlock = vbglPhysHeapChunkAlloc (cbSize);
    }

    if (pBlock)
    {
        VBGL_PH_ASSERTMsg(pBlock->u32Signature == VBGL_PH_BLOCKSIGNATURE,
                         ("pBlock = %p, pBlock->u32Signature = %08X\n", pBlock, pBlock->u32Signature));
        VBGL_PH_ASSERTMsg((pBlock->fu32Flags & VBGL_PH_BF_ALLOCATED) == 0,
                         ("pBlock = %p, pBlock->fu32Flags = %08X\n", pBlock, pBlock->fu32Flags));

        /* We have a free block, either found or allocated. */

        if (pBlock->cbDataSize > 2*(cbSize + sizeof (VBGLPHYSHEAPBLOCK)))
        {
            /* Data will occupy less than a half of the block,
             * split off the tail end into a new free list entry.
             */
            pIter = (VBGLPHYSHEAPBLOCK *)((char *)pBlock + sizeof (VBGLPHYSHEAPBLOCK) + cbSize);

            /* Init the new 'pIter' block, initialized blocks are always marked as free. */
            vbglPhysHeapInitBlock (pIter, pBlock->pChunk, pBlock->cbDataSize - cbSize - sizeof (VBGLPHYSHEAPBLOCK));

            pBlock->cbDataSize = cbSize;

            /* Insert the new 'pIter' block after the 'pBlock' in the free list */
            vbglPhysHeapInsertBlock (pBlock, pIter);
        }

        /* Exclude pBlock from free list */
        vbglPhysHeapExcludeBlock (pBlock);

        /* Mark as allocated */
        pBlock->fu32Flags |= VBGL_PH_BF_ALLOCATED;

        /* Insert to allocated list */
        vbglPhysHeapInsertBlock (NULL, pBlock);

        /* Adjust the chunk allocated blocks counter */
        pBlock->pChunk->cAllocatedBlocks++;
    }

    dumpheap ("post alloc");

    vbglPhysHeapLeave ();
    VBGL_PH_dprintf(("VbglR0PhysHeapAlloc %x size %x\n", vbglPhysHeapBlock2Data (pBlock), pBlock->cbDataSize));

    return vbglPhysHeapBlock2Data (pBlock);
}

DECLR0VBGL(uint32_t) VbglR0PhysHeapGetPhysAddr (void *p)
{
    uint32_t physAddr = 0;
    VBGLPHYSHEAPBLOCK *pBlock = vbglPhysHeapData2Block (p);

    if (pBlock)
    {
        VBGL_PH_ASSERTMsg((pBlock->fu32Flags & VBGL_PH_BF_ALLOCATED) != 0,
                         ("pBlock = %p, pBlock->fu32Flags = %08X\n", pBlock, pBlock->fu32Flags));

        if (pBlock->fu32Flags & VBGL_PH_BF_ALLOCATED)
            physAddr = pBlock->pChunk->physAddr + (uint32_t)((uintptr_t)p - (uintptr_t)pBlock->pChunk);
    }

    return physAddr;
}

DECLR0VBGL(void) VbglR0PhysHeapFree(void *p)
{
    VBGLPHYSHEAPBLOCK *pBlock;
    VBGLPHYSHEAPBLOCK *pNeighbour;

    int rc = vbglPhysHeapEnter ();
    if (RT_FAILURE(rc))
        return;

    dumpheap ("pre free");

    pBlock = vbglPhysHeapData2Block (p);

    if (!pBlock)
    {
        vbglPhysHeapLeave ();
        return;
    }

    VBGL_PH_ASSERTMsg((pBlock->fu32Flags & VBGL_PH_BF_ALLOCATED) != 0,
                     ("pBlock = %p, pBlock->fu32Flags = %08X\n", pBlock, pBlock->fu32Flags));

    /* Exclude from allocated list */
    vbglPhysHeapExcludeBlock (pBlock);

    dumpheap ("post exclude");

    VBGL_PH_dprintf(("VbglR0PhysHeapFree %x size %x\n", p, pBlock->cbDataSize));

    /* Mark as free */
    pBlock->fu32Flags &= ~VBGL_PH_BF_ALLOCATED;

    /* Insert to free list */
    vbglPhysHeapInsertBlock (NULL, pBlock);

    dumpheap ("post insert");

    /* Adjust the chunk allocated blocks counter */
    pBlock->pChunk->cAllocatedBlocks--;

    VBGL_PH_ASSERT(pBlock->pChunk->cAllocatedBlocks >= 0);

    /* Check if we can merge 2 free blocks. To simplify heap maintenance,
     * we will look at block after the just freed one.
     * This will not prevent us from detecting free memory chunks.
     * Also in most cases blocks are deallocated in reverse allocation order
     * and in that case the merging will work.
     */

    pNeighbour = (VBGLPHYSHEAPBLOCK *)((char *)p + pBlock->cbDataSize);

    if ((char *)pNeighbour < (char *)pBlock->pChunk + pBlock->pChunk->cbSize
        && (pNeighbour->fu32Flags & VBGL_PH_BF_ALLOCATED) == 0)
    {
        /* The next block is free as well. */

        /* Adjust size of current memory block */
        pBlock->cbDataSize += pNeighbour->cbDataSize + sizeof (VBGLPHYSHEAPBLOCK);

        /* Exclude the next neighbour */
        vbglPhysHeapExcludeBlock (pNeighbour);
    }

    dumpheap ("post merge");

    /* now check if there are 2 or more free chunks */
    if (pBlock->pChunk->cAllocatedBlocks == 0)
    {
        VBGLPHYSHEAPCHUNK *pChunk = g_vbgldata.pChunkHead;

        uint32_t u32FreeChunks = 0;

        while (pChunk)
        {
            if (pChunk->cAllocatedBlocks == 0)
            {
                u32FreeChunks++;
            }

            pChunk = pChunk->pNext;
        }

        if (u32FreeChunks > 1)
        {
            /* Delete current chunk, it will also exclude all free blocks
             * remaining in the chunk from the free list, so the pBlock
             * will also be invalid after this.
             */
            vbglPhysHeapChunkDelete (pBlock->pChunk);
        }
    }

    dumpheap ("post free");

    vbglPhysHeapLeave ();
}

DECLR0VBGL(int) VbglR0PhysHeapInit (void)
{
    int rc = VINF_SUCCESS;

    /* Allocate the first chunk of the heap. */
    VBGLPHYSHEAPBLOCK *pBlock = vbglPhysHeapChunkAlloc (0);

    if (!pBlock)
        rc = VERR_NO_MEMORY;

    RTSemFastMutexCreate(&g_vbgldata.mutexHeap);

    return rc;
}

DECLR0VBGL(void) VbglR0PhysHeapTerminate (void)
{
    while (g_vbgldata.pChunkHead)
    {
        vbglPhysHeapChunkDelete (g_vbgldata.pChunkHead);
    }

    RTSemFastMutexDestroy(g_vbgldata.mutexHeap);
}

