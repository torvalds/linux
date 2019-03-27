/******************************************************************************

 © 1995-2003, 2004, 2005-2011 Freescale Semiconductor, Inc.
 All rights reserved.

 This is proprietary source code of Freescale Semiconductor Inc.,
 and its use is subject to the NetComm Device Drivers EULA.
 The copyright notice above does not evidence any actual or intended
 publication of such source code.

 ALTERNATIVELY, redistribution and use in source and binary forms, with
 or without modification, are permitted provided that the following
 conditions are met:
     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
     * Neither the name of Freescale Semiconductor nor the
       names of its contributors may be used to endorse or promote products
       derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *

 **************************************************************************/
#include "error_ext.h"
#include "part_ext.h"
#include "std_ext.h"
#include "string_ext.h"
#include "mem_ext.h"
#include "mem.h"
#include "xx_ext.h"


#if 0
#define PAD_ALIGNMENT(align, x) (((x)%(align)) ? ((align)-((x)%(align))) : 0)

#define ALIGN_BLOCK(p_Block, prefixSize, alignment)                 \
    do {                                                            \
        p_Block += (prefixSize);                                    \
        p_Block += PAD_ALIGNMENT((alignment), (uintptr_t)(p_Block)); \
    } while (0)

#if defined(__GNUC__)
#define GET_CALLER_ADDR \
    __asm__ ("mflr  %0" : "=r" (callerAddr))
#elif defined(__MWERKS__)
/* NOTE: This implementation is only valid for CodeWarrior for PowerPC */
#define GET_CALLER_ADDR \
    __asm__("add  %0, 0, %0" : : "r" (callerAddr))
#endif /* defined(__GNUC__) */


/*****************************************************************************/
static __inline__ void * MemGet(t_MemorySegment *p_Mem)
{
    uint8_t *p_Block;

    /* check if there is an available block */
    if (p_Mem->current == p_Mem->num)
    {
        p_Mem->getFailures++;
        return NULL;
    }

    /* get the block */
    p_Block = p_Mem->p_BlocksStack[p_Mem->current];
#ifdef DEBUG
    p_Mem->p_BlocksStack[p_Mem->current] = NULL;
#endif /* DEBUG */
    /* advance current index */
    p_Mem->current++;

    return (void *)p_Block;
}

/*****************************************************************************/
static __inline__ t_Error MemPut(t_MemorySegment *p_Mem, void *p_Block)
{
    /* check if blocks stack is full */
    if (p_Mem->current > 0)
    {
        /* decrease current index */
        p_Mem->current--;
        /* put the block */
        p_Mem->p_BlocksStack[p_Mem->current] = (uint8_t *)p_Block;
        return E_OK;
    }

    RETURN_ERROR(MAJOR, E_FULL, NO_MSG);
}


#ifdef DEBUG_MEM_LEAKS

/*****************************************************************************/
static t_Error InitMemDebugDatabase(t_MemorySegment *p_Mem)
{
    p_Mem->p_MemDbg = (void *)XX_Malloc(sizeof(t_MemDbg) * p_Mem->num);
    if (!p_Mem->p_MemDbg)
    {
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Memory debug object"));
    }

    memset(p_Mem->p_MemDbg, ILLEGAL_BASE, sizeof(t_MemDbg) * p_Mem->num);

    return E_OK;
}


/*****************************************************************************/
static t_Error DebugMemGet(t_Handle h_Mem, void *p_Block, uintptr_t ownerAddress)
{
    t_MemorySegment *p_Mem = (t_MemorySegment *)h_Mem;
    t_MemDbg        *p_MemDbg = (t_MemDbg *)p_Mem->p_MemDbg;
    uint32_t        blockIndex;

    ASSERT_COND(ownerAddress != ILLEGAL_BASE);

    /* Find block num */
    if (p_Mem->consecutiveMem)
    {
        blockIndex =
            (((uint8_t *)p_Block - (p_Mem->p_Bases[0] + p_Mem->blockOffset)) / p_Mem->blockSize);
    }
    else
    {
        blockIndex = *(uint32_t *)((uint8_t *)p_Block - 4);
    }

    ASSERT_COND(blockIndex < p_Mem->num);
    ASSERT_COND(p_MemDbg[blockIndex].ownerAddress == ILLEGAL_BASE);

    p_MemDbg[blockIndex].ownerAddress = ownerAddress;

    return E_OK;
}

/*****************************************************************************/
static t_Error DebugMemPut(t_Handle h_Mem, void *p_Block)
{
    t_MemorySegment *p_Mem = (t_MemorySegment *)h_Mem;
    t_MemDbg        *p_MemDbg = (t_MemDbg *)p_Mem->p_MemDbg;
    uint32_t        blockIndex;
    uint8_t         *p_Temp;

    /* Find block num */
    if (p_Mem->consecutiveMem)
    {
        blockIndex =
            (((uint8_t *)p_Block - (p_Mem->p_Bases[0] + p_Mem->blockOffset)) / p_Mem->blockSize);

        if (blockIndex >= p_Mem->num)
        {
            RETURN_ERROR(MAJOR, E_INVALID_ADDRESS,
                         ("Freed address (0x%08x) does not belong to this pool", p_Block));
        }
    }
    else
    {
        blockIndex = *(uint32_t *)((uint8_t *)p_Block - 4);

        if (blockIndex >= p_Mem->num)
        {
            RETURN_ERROR(MAJOR, E_INVALID_ADDRESS,
                         ("Freed address (0x%08x) does not belong to this pool", p_Block));
        }

        /* Verify that the block matches the corresponding base */
        p_Temp = p_Mem->p_Bases[blockIndex];

        ALIGN_BLOCK(p_Temp, p_Mem->prefixSize, p_Mem->alignment);

        if (p_Temp == p_Mem->p_Bases[blockIndex])
            p_Temp += p_Mem->alignment;

        if (p_Temp != p_Block)
        {
            RETURN_ERROR(MAJOR, E_INVALID_ADDRESS,
                         ("Freed address (0x%08x) does not belong to this pool", p_Block));
        }
    }

    if (p_MemDbg[blockIndex].ownerAddress == ILLEGAL_BASE)
    {
        RETURN_ERROR(MAJOR, E_ALREADY_FREE,
                     ("Attempt to free unallocated address (0x%08x)", p_Block));
    }

    p_MemDbg[blockIndex].ownerAddress = (uintptr_t)ILLEGAL_BASE;

    return E_OK;
}

#endif /* DEBUG_MEM_LEAKS */


/*****************************************************************************/
uint32_t MEM_ComputePartitionSize(uint32_t num,
                                  uint16_t dataSize,
                                  uint16_t prefixSize,
                                  uint16_t postfixSize,
                                  uint16_t alignment)
{
    uint32_t  blockSize = 0, pad1 = 0, pad2 = 0;

    /* Make sure that the alignment is at least 4 */
    if (alignment < 4)
    {
        alignment = 4;
    }

    pad1 = (uint32_t)PAD_ALIGNMENT(4, prefixSize);
    /* Block size not including 2nd padding */
    blockSize = pad1 + prefixSize + dataSize + postfixSize;
    pad2 = PAD_ALIGNMENT(alignment, blockSize);
    /* Block size including 2nd padding */
    blockSize += pad2;

    return ((num * blockSize) + alignment);
}

/*****************************************************************************/
t_Error MEM_Init(char       name[],
                 t_Handle   *p_Handle,
                 uint32_t   num,
                 uint16_t   dataSize,
                 uint16_t   prefixSize,
                 uint16_t   postfixSize,
                 uint16_t   alignment)
{
    uint8_t     *p_Memory;
    uint32_t    allocSize;
    t_Error     errCode;

    allocSize = MEM_ComputePartitionSize(num,
                                         dataSize,
                                         prefixSize,
                                         postfixSize,
                                         alignment);

    p_Memory = (uint8_t *)XX_Malloc(allocSize);
    if (!p_Memory)
    {
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Memory segment"));
    }

    errCode = MEM_InitByAddress(name,
                                p_Handle,
                                num,
                                dataSize,
                                prefixSize,
                                postfixSize,
                                alignment,
                                p_Memory);
    if (errCode != E_OK)
    {
        RETURN_ERROR(MAJOR, errCode, NO_MSG);
    }

    ((t_MemorySegment *)(*p_Handle))->allocOwner = e_MEM_ALLOC_OWNER_LOCAL;

    return E_OK;
}


/*****************************************************************************/
t_Error MEM_InitByAddress(char      name[],
                          t_Handle  *p_Handle,
                          uint32_t  num,
                          uint16_t  dataSize,
                          uint16_t  prefixSize,
                          uint16_t  postfixSize,
                          uint16_t  alignment,
                          uint8_t   *p_Memory)
{
    t_MemorySegment *p_Mem;
    uint32_t        i, blockSize;
    uint16_t        alignPad, endPad;
    uint8_t         *p_Blocks;

     /* prepare in case of error */
    *p_Handle = NULL;

    if (!p_Memory)
    {
        RETURN_ERROR(MAJOR, E_NULL_POINTER, ("Memory blocks"));
    }

    p_Blocks = p_Memory;

    /* make sure that the alignment is at least 4 and power of 2 */
    if (alignment < 4)
    {
        alignment = 4;
    }
    else if (!POWER_OF_2(alignment))
    {
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Alignment (should be power of 2)"));
    }

    /* first allocate the segment descriptor */
    p_Mem = (t_MemorySegment *)XX_Malloc(sizeof(t_MemorySegment));
    if (!p_Mem)
    {
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Memory segment structure"));
    }

    /* allocate the blocks stack */
    p_Mem->p_BlocksStack = (uint8_t **)XX_Malloc(num * sizeof(uint8_t*));
    if (!p_Mem->p_BlocksStack)
    {
        XX_Free(p_Mem);
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Memory segment block pointers stack"));
    }

    /* allocate the blocks bases array */
    p_Mem->p_Bases = (uint8_t **)XX_Malloc(sizeof(uint8_t*));
    if (!p_Mem->p_Bases)
    {
        MEM_Free(p_Mem);
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Memory segment base pointers array"));
    }
    memset(p_Mem->p_Bases, 0, sizeof(uint8_t*));

    /* store info about this segment */
    p_Mem->num = num;
    p_Mem->current = 0;
    p_Mem->dataSize = dataSize;
    p_Mem->p_Bases[0] = p_Blocks;
    p_Mem->getFailures = 0;
    p_Mem->allocOwner = e_MEM_ALLOC_OWNER_EXTERNAL;
    p_Mem->consecutiveMem = TRUE;
    p_Mem->prefixSize = prefixSize;
    p_Mem->postfixSize = postfixSize;
    p_Mem->alignment = alignment;
    /* store name */
    strncpy(p_Mem->name, name, MEM_MAX_NAME_LENGTH-1);

    p_Mem->h_Spinlock = XX_InitSpinlock();
    if (!p_Mem->h_Spinlock)
    {
        MEM_Free(p_Mem);
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Can't create spinlock!"));
    }

    alignPad = (uint16_t)PAD_ALIGNMENT(4, prefixSize);
    /* Make sure the entire size is a multiple of alignment */
    endPad = (uint16_t)PAD_ALIGNMENT(alignment, (alignPad + prefixSize + dataSize + postfixSize));

    /* The following manipulation places the data of block[0] in an aligned address,
       since block size is aligned the following block datas will all be aligned */
    ALIGN_BLOCK(p_Blocks, prefixSize, alignment);

    blockSize = (uint32_t)(alignPad + prefixSize + dataSize + postfixSize + endPad);

    /* initialize the blocks */
    for (i=0; i < num; i++)
    {
        p_Mem->p_BlocksStack[i] = p_Blocks;
        p_Blocks += blockSize;
    }

    /* return handle to caller */
    *p_Handle = (t_Handle)p_Mem;

#ifdef DEBUG_MEM_LEAKS
    {
        t_Error errCode = InitMemDebugDatabase(p_Mem);

        if (errCode != E_OK)
            RETURN_ERROR(MAJOR, errCode, NO_MSG);

        p_Mem->blockOffset = (uint32_t)(p_Mem->p_BlocksStack[0] - p_Mem->p_Bases[0]);
        p_Mem->blockSize = blockSize;
    }
#endif /* DEBUG_MEM_LEAKS */

    return E_OK;
}


/*****************************************************************************/
t_Error MEM_InitSmart(char      name[],
                      t_Handle  *p_Handle,
                      uint32_t  num,
                      uint16_t  dataSize,
                      uint16_t  prefixSize,
                      uint16_t  postfixSize,
                      uint16_t  alignment,
                      uint8_t   memPartitionId,
                      bool      consecutiveMem)
{
    t_MemorySegment *p_Mem;
    uint32_t        i, blockSize;
    uint16_t        alignPad, endPad;

    /* prepare in case of error */
    *p_Handle = NULL;

    /* make sure that size is always a multiple of 4 */
    if (dataSize & 3)
    {
        dataSize &= ~3;
        dataSize += 4;
    }

    /* make sure that the alignment is at least 4 and power of 2 */
    if (alignment < 4)
    {
        alignment = 4;
    }
    else if (!POWER_OF_2(alignment))
    {
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Alignment (should be power of 2)"));
    }

    /* first allocate the segment descriptor */
    p_Mem = (t_MemorySegment *)XX_Malloc(sizeof(t_MemorySegment));
    if (!p_Mem)
    {
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Memory segment structure"));
    }

    /* allocate the blocks stack */
    p_Mem->p_BlocksStack = (uint8_t **)XX_Malloc(num * sizeof(uint8_t*));
    if (!p_Mem->p_BlocksStack)
    {
        MEM_Free(p_Mem);
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Memory segment block pointers stack"));
    }

    /* allocate the blocks bases array */
    p_Mem->p_Bases = (uint8_t **)XX_Malloc((consecutiveMem ? 1 : num) * sizeof(uint8_t*));
    if (!p_Mem->p_Bases)
    {
        MEM_Free(p_Mem);
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Memory segment base pointers array"));
    }
    memset(p_Mem->p_Bases, 0, (consecutiveMem ? 1 : num) * sizeof(uint8_t*));

    /* store info about this segment */
    p_Mem->num = num;
    p_Mem->current = 0;
    p_Mem->dataSize = dataSize;
    p_Mem->getFailures = 0;
    p_Mem->allocOwner = e_MEM_ALLOC_OWNER_LOCAL_SMART;
    p_Mem->consecutiveMem = consecutiveMem;
    p_Mem->prefixSize = prefixSize;
    p_Mem->postfixSize = postfixSize;
    p_Mem->alignment = alignment;

    p_Mem->h_Spinlock = XX_InitSpinlock();
    if (!p_Mem->h_Spinlock)
    {
        MEM_Free(p_Mem);
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Can't create spinlock!"));
    }

    alignPad = (uint16_t)PAD_ALIGNMENT(4, prefixSize);
    /* Make sure the entire size is a multiple of alignment */
    endPad = (uint16_t)PAD_ALIGNMENT(alignment, alignPad + prefixSize + dataSize + postfixSize);

    /* Calculate blockSize */
    blockSize = (uint32_t)(alignPad + prefixSize + dataSize + postfixSize + endPad);

    /* Now allocate the blocks */
    if (p_Mem->consecutiveMem)
    {
        /* |alignment - 1| bytes at most will be discarded in the beginning of the
           received segment for alignment reasons, therefore the allocation is of:
           (alignment + (num * block size)). */
        uint8_t *p_Blocks = (uint8_t *)
            XX_MallocSmart((uint32_t)((num * blockSize) + alignment), memPartitionId, 1);
        if (!p_Blocks)
        {
            MEM_Free(p_Mem);
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Memory segment blocks"));
        }

        /* Store the memory segment address */
        p_Mem->p_Bases[0] = p_Blocks;

        /* The following manipulation places the data of block[0] in an aligned address,
           since block size is aligned the following block datas will all be aligned.*/
        ALIGN_BLOCK(p_Blocks, prefixSize, alignment);

        /* initialize the blocks */
        for (i = 0; i < num; i++)
        {
            p_Mem->p_BlocksStack[i] = p_Blocks;
            p_Blocks += blockSize;
        }

#ifdef DEBUG_MEM_LEAKS
        p_Mem->blockOffset = (uint32_t)(p_Mem->p_BlocksStack[0] - p_Mem->p_Bases[0]);
        p_Mem->blockSize = blockSize;
#endif /* DEBUG_MEM_LEAKS */
    }
    else
    {
        /* |alignment - 1| bytes at most will be discarded in the beginning of the
           received segment for alignment reasons, therefore the allocation is of:
           (alignment + block size). */
        for (i = 0; i < num; i++)
        {
            uint8_t *p_Block = (uint8_t *)
                XX_MallocSmart((uint32_t)(blockSize + alignment), memPartitionId, 1);
            if (!p_Block)
            {
                MEM_Free(p_Mem);
                RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Memory segment blocks"));
            }

            /* Store the memory segment address */
            p_Mem->p_Bases[i] = p_Block;

            /* The following places the data of each block in an aligned address */
            ALIGN_BLOCK(p_Block, prefixSize, alignment);

#ifdef DEBUG_MEM_LEAKS
            /* Need 4 bytes before the meaningful bytes to store the block index.
               We know we have them because alignment is at least 4 bytes. */
            if (p_Block == p_Mem->p_Bases[i])
                p_Block += alignment;

            *(uint32_t *)(p_Block - 4) = i;
#endif /* DEBUG_MEM_LEAKS */

            p_Mem->p_BlocksStack[i] = p_Block;
        }
    }

    /* store name */
    strncpy(p_Mem->name, name, MEM_MAX_NAME_LENGTH-1);

    /* return handle to caller */
    *p_Handle = (t_Handle)p_Mem;

#ifdef DEBUG_MEM_LEAKS
    {
        t_Error errCode = InitMemDebugDatabase(p_Mem);

        if (errCode != E_OK)
            RETURN_ERROR(MAJOR, errCode, NO_MSG);
    }
#endif /* DEBUG_MEM_LEAKS */

    return E_OK;
}


/*****************************************************************************/
void MEM_Free(t_Handle h_Mem)
{
    t_MemorySegment *p_Mem = (t_MemorySegment*)h_Mem;
    uint32_t        num, i;

    /* Check MEM leaks */
    MEM_CheckLeaks(h_Mem);

    if (p_Mem)
    {
        num = p_Mem->consecutiveMem ? 1 : p_Mem->num;

        if (p_Mem->allocOwner == e_MEM_ALLOC_OWNER_LOCAL_SMART)
        {
            for (i=0; i < num; i++)
            {
                if (p_Mem->p_Bases[i])
                {
                    XX_FreeSmart(p_Mem->p_Bases[i]);
                }
            }
        }
        else if (p_Mem->allocOwner == e_MEM_ALLOC_OWNER_LOCAL)
        {
            for (i=0; i < num; i++)
            {
                if (p_Mem->p_Bases[i])
                {
                    XX_Free(p_Mem->p_Bases[i]);
                }
            }
        }

        if (p_Mem->h_Spinlock)
            XX_FreeSpinlock(p_Mem->h_Spinlock);

        if (p_Mem->p_Bases)
            XX_Free(p_Mem->p_Bases);

        if (p_Mem->p_BlocksStack)
            XX_Free(p_Mem->p_BlocksStack);

#ifdef DEBUG_MEM_LEAKS
        if (p_Mem->p_MemDbg)
            XX_Free(p_Mem->p_MemDbg);
#endif /* DEBUG_MEM_LEAKS */

       XX_Free(p_Mem);
    }
}


/*****************************************************************************/
void * MEM_Get(t_Handle h_Mem)
{
    t_MemorySegment *p_Mem = (t_MemorySegment *)h_Mem;
    uint8_t         *p_Block;
    uint32_t        intFlags;
#ifdef DEBUG_MEM_LEAKS
    uintptr_t       callerAddr = 0;

    GET_CALLER_ADDR;
#endif /* DEBUG_MEM_LEAKS */

    ASSERT_COND(h_Mem);

    intFlags = XX_LockIntrSpinlock(p_Mem->h_Spinlock);
    /* check if there is an available block */
    if ((p_Block = (uint8_t *)MemGet(p_Mem)) == NULL)
    {
        XX_UnlockIntrSpinlock(p_Mem->h_Spinlock, intFlags);
        return NULL;
    }

#ifdef DEBUG_MEM_LEAKS
    DebugMemGet(p_Mem, p_Block, callerAddr);
#endif /* DEBUG_MEM_LEAKS */
    XX_UnlockIntrSpinlock(p_Mem->h_Spinlock, intFlags);

    return (void *)p_Block;
}


/*****************************************************************************/
uint16_t MEM_GetN(t_Handle h_Mem, uint32_t num, void *array[])
{
    t_MemorySegment     *p_Mem = (t_MemorySegment *)h_Mem;
    uint32_t            availableBlocks;
    register uint32_t   i;
    uint32_t            intFlags;
#ifdef DEBUG_MEM_LEAKS
    uintptr_t           callerAddr = 0;

    GET_CALLER_ADDR;
#endif /* DEBUG_MEM_LEAKS */

    ASSERT_COND(h_Mem);

    intFlags = XX_LockIntrSpinlock(p_Mem->h_Spinlock);
    /* check how many blocks are available */
    availableBlocks = (uint32_t)(p_Mem->num - p_Mem->current);
    if (num > availableBlocks)
    {
        num = availableBlocks;
    }

    for (i=0; i < num; i++)
    {
        /* get pointer to block */
        if ((array[i] = MemGet(p_Mem)) == NULL)
        {
            break;
        }

#ifdef DEBUG_MEM_LEAKS
        DebugMemGet(p_Mem, array[i], callerAddr);
#endif /* DEBUG_MEM_LEAKS */
    }
    XX_UnlockIntrSpinlock(p_Mem->h_Spinlock, intFlags);

    return (uint16_t)i;
}


/*****************************************************************************/
t_Error MEM_Put(t_Handle h_Mem, void *p_Block)
{
    t_MemorySegment *p_Mem = (t_MemorySegment *)h_Mem;
    t_Error         rc;
    uint32_t        intFlags;

    ASSERT_COND(h_Mem);

    intFlags = XX_LockIntrSpinlock(p_Mem->h_Spinlock);
    /* check if blocks stack is full */
    if ((rc = MemPut(p_Mem, p_Block)) != E_OK)
    {
        XX_UnlockIntrSpinlock(p_Mem->h_Spinlock, intFlags);
        RETURN_ERROR(MAJOR, rc, NO_MSG);
    }

#ifdef DEBUG_MEM_LEAKS
    DebugMemPut(p_Mem, p_Block);
#endif /* DEBUG_MEM_LEAKS */
    XX_UnlockIntrSpinlock(p_Mem->h_Spinlock, intFlags);

    return E_OK;
}


#ifdef DEBUG_MEM_LEAKS

/*****************************************************************************/
void MEM_CheckLeaks(t_Handle h_Mem)
{
    t_MemorySegment *p_Mem = (t_MemorySegment *)h_Mem;
    t_MemDbg        *p_MemDbg = (t_MemDbg *)p_Mem->p_MemDbg;
    uint8_t         *p_Block;
    int             i;

    ASSERT_COND(h_Mem);

    if (p_Mem->consecutiveMem)
    {
        for (i=0; i < p_Mem->num; i++)
        {
            if (p_MemDbg[i].ownerAddress != ILLEGAL_BASE)
            {
                /* Find the block address */
                p_Block = ((p_Mem->p_Bases[0] + p_Mem->blockOffset) +
                           (i * p_Mem->blockSize));

                XX_Print("MEM leak: 0x%08x, Caller address: 0x%08x\n",
                         p_Block, p_MemDbg[i].ownerAddress);
            }
        }
    }
    else
    {
        for (i=0; i < p_Mem->num; i++)
        {
            if (p_MemDbg[i].ownerAddress != ILLEGAL_BASE)
            {
                /* Find the block address */
                p_Block = p_Mem->p_Bases[i];

                ALIGN_BLOCK(p_Block, p_Mem->prefixSize, p_Mem->alignment);

                if (p_Block == p_Mem->p_Bases[i])
                    p_Block += p_Mem->alignment;

                XX_Print("MEM leak: 0x%08x, Caller address: 0x%08x\n",
                         p_Block, p_MemDbg[i].ownerAddress);
            }
        }
    }
}

#endif /* DEBUG_MEM_LEAKS */


#endif
