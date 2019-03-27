/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "string_ext.h"
#include "error_ext.h"
#include "std_ext.h"
#include "part_ext.h"
#include "xx_ext.h"

#include "mm.h"




/**********************************************************************
 *                     MM internal routines set                       *
 **********************************************************************/

/****************************************************************
 *  Routine:   CreateBusyBlock
 *
 *  Description:
 *      Initializes a new busy block of "size" bytes and started
 *      rom "base" address. Each busy block has a name that
 *      specified the purpose of the memory allocation.
 *
 *  Arguments:
 *      base      - base address of the busy block
 *      size      - size of the busy block
 *      name      - name that specified the busy block
 *
 *  Return value:
 *      A pointer to new created structure returned on success;
 *      Otherwise, NULL.
 ****************************************************************/
static t_BusyBlock * CreateBusyBlock(uint64_t base, uint64_t size, char *name)
{
    t_BusyBlock *p_BusyBlock;
    uint32_t    n;

    p_BusyBlock = (t_BusyBlock *)XX_Malloc(sizeof(t_BusyBlock));
    if ( !p_BusyBlock )
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, NO_MSG);
        return NULL;
    }

    p_BusyBlock->base = base;
    p_BusyBlock->end = base + size;

    n = strlen(name);
    if (n >= MM_MAX_NAME_LEN)
        n = MM_MAX_NAME_LEN - 1;
    strncpy(p_BusyBlock->name, name, MM_MAX_NAME_LEN-1);
    p_BusyBlock->name[n] = '\0';
    p_BusyBlock->p_Next = 0;

    return p_BusyBlock;
}

/****************************************************************
 *  Routine:   CreateNewBlock
 *
 *  Description:
 *      Initializes a new memory block of "size" bytes and started
 *      from "base" address.
 *
 *  Arguments:
 *      base    - base address of the memory block
 *      size    - size of the memory block
 *
 *  Return value:
 *      A pointer to new created structure returned on success;
 *      Otherwise, NULL.
 ****************************************************************/
static t_MemBlock * CreateNewBlock(uint64_t base, uint64_t size)
{
    t_MemBlock *p_MemBlock;

    p_MemBlock = (t_MemBlock *)XX_Malloc(sizeof(t_MemBlock));
    if ( !p_MemBlock )
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, NO_MSG);
        return NULL;
    }

    p_MemBlock->base = base;
    p_MemBlock->end = base+size;
    p_MemBlock->p_Next = 0;

    return p_MemBlock;
}

/****************************************************************
 *  Routine:   CreateFreeBlock
 *
 *  Description:
 *      Initializes a new free block of of "size" bytes and
 *      started from "base" address.
 *
 *  Arguments:
 *      base      - base address of the free block
 *      size      - size of the free block
 *
 *  Return value:
 *      A pointer to new created structure returned on success;
 *      Otherwise, NULL.
 ****************************************************************/
static t_FreeBlock * CreateFreeBlock(uint64_t base, uint64_t size)
{
    t_FreeBlock *p_FreeBlock;

    p_FreeBlock = (t_FreeBlock *)XX_Malloc(sizeof(t_FreeBlock));
    if ( !p_FreeBlock )
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, NO_MSG);
        return NULL;
    }

    p_FreeBlock->base = base;
    p_FreeBlock->end = base + size;
    p_FreeBlock->p_Next = 0;

    return p_FreeBlock;
}

/****************************************************************
 *  Routine:    AddFree
 *
 *  Description:
 *      Adds a new free block to the free lists. It updates each
 *      free list to include a new free block.
 *      Note, that all free block in each free list are ordered
 *      by their base address.
 *
 *  Arguments:
 *      p_MM  - pointer to the MM object
 *      base  - base address of a given free block
 *      end   - end address of a given free block
 *
 *  Return value:
 *
 *
 ****************************************************************/
static t_Error AddFree(t_MM *p_MM, uint64_t base, uint64_t end)
{
    t_FreeBlock *p_PrevB, *p_CurrB, *p_NewB;
    uint64_t    alignment;
    uint64_t    alignBase;
    int         i;

    /* Updates free lists to include  a just released block */
    for (i=0; i <= MM_MAX_ALIGNMENT; i++)
    {
        p_PrevB = p_NewB = 0;
        p_CurrB = p_MM->freeBlocks[i];

        alignment = (uint64_t)(0x1 << i);
        alignBase = MAKE_ALIGNED(base, alignment);

        /* Goes to the next free list if there is no block to free */
        if (alignBase >= end)
            continue;

        /* Looks for a free block that should be updated */
        while ( p_CurrB )
        {
            if ( alignBase <= p_CurrB->end )
            {
                if ( end > p_CurrB->end )
                {
                    t_FreeBlock *p_NextB;
                    while ( p_CurrB->p_Next && end > p_CurrB->p_Next->end )
                    {
                        p_NextB = p_CurrB->p_Next;
                        p_CurrB->p_Next = p_CurrB->p_Next->p_Next;
                        XX_Free(p_NextB);
                    }

                    p_NextB = p_CurrB->p_Next;
                    if ( !p_NextB || (p_NextB && end < p_NextB->base) )
                    {
                        p_CurrB->end = end;
                    }
                    else
                    {
                        p_CurrB->end = p_NextB->end;
                        p_CurrB->p_Next = p_NextB->p_Next;
                        XX_Free(p_NextB);
                    }
                }
                else if ( (end < p_CurrB->base) && ((end-alignBase) >= alignment) )
                {
                    if ((p_NewB = CreateFreeBlock(alignBase, end-alignBase)) == NULL)
                        RETURN_ERROR(MAJOR, E_NO_MEMORY, NO_MSG);

                    p_NewB->p_Next = p_CurrB;
                    if (p_PrevB)
                        p_PrevB->p_Next = p_NewB;
                    else
                        p_MM->freeBlocks[i] = p_NewB;
                    break;
                }

                if ((alignBase < p_CurrB->base) && (end >= p_CurrB->base))
                {
                    p_CurrB->base = alignBase;
                }

                /* if size of the free block is less then alignment
                 * deletes that free block from the free list. */
                if ( (p_CurrB->end - p_CurrB->base) < alignment)
                {
                    if ( p_PrevB )
                        p_PrevB->p_Next = p_CurrB->p_Next;
                    else
                        p_MM->freeBlocks[i] = p_CurrB->p_Next;
                    XX_Free(p_CurrB);
                    p_CurrB = NULL;
                }
                break;
            }
            else
            {
                p_PrevB = p_CurrB;
                p_CurrB = p_CurrB->p_Next;
            }
        }

        /* If no free block found to be updated, insert a new free block
         * to the end of the free list.
         */
        if ( !p_CurrB && ((((uint64_t)(end-base)) & ((uint64_t)(alignment-1))) == 0) )
        {
            if ((p_NewB = CreateFreeBlock(alignBase, end-base)) == NULL)
                RETURN_ERROR(MAJOR, E_NO_MEMORY, NO_MSG);

            if (p_PrevB)
                p_PrevB->p_Next = p_NewB;
            else
                p_MM->freeBlocks[i] = p_NewB;
        }

        /* Update boundaries of the new free block */
        if ((alignment == 1) && !p_NewB)
        {
            if ( p_CurrB && base > p_CurrB->base )
                base = p_CurrB->base;
            if ( p_CurrB && end < p_CurrB->end )
                end = p_CurrB->end;
        }
    }

    return (E_OK);
}

/****************************************************************
 *  Routine:      CutFree
 *
 *  Description:
 *      Cuts a free block from holdBase to holdEnd from the free lists.
 *      That is, it updates all free lists of the MM object do
 *      not include a block of memory from holdBase to holdEnd.
 *      For each free lists it seek for a free block that holds
 *      either holdBase or holdEnd. If such block is found it updates it.
 *
 *  Arguments:
 *      p_MM            - pointer to the MM object
 *      holdBase        - base address of the allocated block
 *      holdEnd         - end address of the allocated block
 *
 *  Return value:
 *      E_OK is returned on success,
 *      otherwise returns an error code.
 *
 ****************************************************************/
static t_Error CutFree(t_MM *p_MM, uint64_t holdBase, uint64_t holdEnd)
{
    t_FreeBlock *p_PrevB, *p_CurrB, *p_NewB;
    uint64_t    alignBase, base, end;
    uint64_t    alignment;
    int         i;

    for (i=0; i <= MM_MAX_ALIGNMENT; i++)
    {
        p_PrevB = p_NewB = 0;
        p_CurrB = p_MM->freeBlocks[i];

        alignment = (uint64_t)(0x1 << i);
        alignBase = MAKE_ALIGNED(holdEnd, alignment);

        while ( p_CurrB )
        {
            base = p_CurrB->base;
            end = p_CurrB->end;

            if ( (holdBase <= base) && (holdEnd <= end) && (holdEnd > base) )
            {
                if ( alignBase >= end ||
                     (alignBase < end && ((end-alignBase) < alignment)) )
                {
                    if (p_PrevB)
                        p_PrevB->p_Next = p_CurrB->p_Next;
                    else
                        p_MM->freeBlocks[i] = p_CurrB->p_Next;
                    XX_Free(p_CurrB);
                }
                else
                {
                    p_CurrB->base = alignBase;
                }
                break;
            }
            else if ( (holdBase > base) && (holdEnd <= end) )
            {
                if ( (holdBase-base) >= alignment )
                {
                    if ( (alignBase < end) && ((end-alignBase) >= alignment) )
                    {
                        if ((p_NewB = CreateFreeBlock(alignBase, end-alignBase)) == NULL)
                            RETURN_ERROR(MAJOR, E_NO_MEMORY, NO_MSG);
                        p_NewB->p_Next = p_CurrB->p_Next;
                        p_CurrB->p_Next = p_NewB;
                    }
                    p_CurrB->end = holdBase;
                }
                else if ( (alignBase < end) && ((end-alignBase) >= alignment) )
                {
                    p_CurrB->base = alignBase;
                }
                else
                {
                    if (p_PrevB)
                        p_PrevB->p_Next = p_CurrB->p_Next;
                    else
                        p_MM->freeBlocks[i] = p_CurrB->p_Next;
                    XX_Free(p_CurrB);
                }
                break;
            }
            else
            {
                p_PrevB = p_CurrB;
                p_CurrB = p_CurrB->p_Next;
            }
        }
    }

    return (E_OK);
}

/****************************************************************
 *  Routine:     AddBusy
 *
 *  Description:
 *      Adds a new busy block to the list of busy blocks. Note,
 *      that all busy blocks are ordered by their base address in
 *      the busy list.
 *
 *  Arguments:
 *      MM              - handler to the MM object
 *      p_NewBusyB      - pointer to the a busy block
 *
 *  Return value:
 *      None.
 *
 ****************************************************************/
static void AddBusy(t_MM *p_MM, t_BusyBlock *p_NewBusyB)
{
    t_BusyBlock *p_CurrBusyB, *p_PrevBusyB;

    /* finds a place of a new busy block in the list of busy blocks */
    p_PrevBusyB = 0;
    p_CurrBusyB = p_MM->busyBlocks;

    while ( p_CurrBusyB && p_NewBusyB->base > p_CurrBusyB->base )
    {
        p_PrevBusyB = p_CurrBusyB;
        p_CurrBusyB = p_CurrBusyB->p_Next;
    }

    /* insert the new busy block into the list of busy blocks */
    if ( p_CurrBusyB )
        p_NewBusyB->p_Next = p_CurrBusyB;
    if ( p_PrevBusyB )
        p_PrevBusyB->p_Next = p_NewBusyB;
    else
        p_MM->busyBlocks = p_NewBusyB;
}

/****************************************************************
 *  Routine:    CutBusy
 *
 *  Description:
 *      Cuts a block from base to end from the list of busy blocks.
 *      This is done by updating the list of busy blocks do not
 *      include a given block, that block is going to be free. If a
 *      given block is a part of some other busy block, so that
 *      busy block is updated. If there are number of busy blocks
 *      included in the given block, so all that blocks are removed
 *      from the busy list and the end blocks are updated.
 *      If the given block devides some block into two parts, a new
 *      busy block is added to the busy list.
 *
 *  Arguments:
 *      p_MM  - pointer to the MM object
 *      base  - base address of a given busy block
 *      end   - end address of a given busy block
 *
 *  Return value:
 *      E_OK on success, E_NOMEMORY otherwise.
 *
 ****************************************************************/
static t_Error CutBusy(t_MM *p_MM, uint64_t base, uint64_t end)
{
    t_BusyBlock  *p_CurrB, *p_PrevB, *p_NewB;

    p_CurrB = p_MM->busyBlocks;
    p_PrevB = p_NewB = 0;

    while ( p_CurrB )
    {
        if ( base < p_CurrB->end )
        {
            if ( end > p_CurrB->end )
            {
                t_BusyBlock *p_NextB;
                while ( p_CurrB->p_Next && end >= p_CurrB->p_Next->end )
                {
                    p_NextB = p_CurrB->p_Next;
                    p_CurrB->p_Next = p_CurrB->p_Next->p_Next;
                    XX_Free(p_NextB);
                }

                p_NextB = p_CurrB->p_Next;
                if ( p_NextB && end > p_NextB->base )
                {
                    p_NextB->base = end;
                }
            }

            if ( base <= p_CurrB->base )
            {
                if ( end < p_CurrB->end && end > p_CurrB->base )
                {
                    p_CurrB->base = end;
                }
                else if ( end >= p_CurrB->end )
                {
                    if ( p_PrevB )
                        p_PrevB->p_Next = p_CurrB->p_Next;
                    else
                        p_MM->busyBlocks = p_CurrB->p_Next;
                    XX_Free(p_CurrB);
                }
            }
            else
            {
                if ( end < p_CurrB->end && end > p_CurrB->base )
                {
                    if ((p_NewB = CreateBusyBlock(end,
                                                  p_CurrB->end-end,
                                                  p_CurrB->name)) == NULL)
                        RETURN_ERROR(MAJOR, E_NO_MEMORY, NO_MSG);
                    p_NewB->p_Next = p_CurrB->p_Next;
                    p_CurrB->p_Next = p_NewB;
                }
                p_CurrB->end = base;
            }
            break;
        }
        else
        {
            p_PrevB = p_CurrB;
            p_CurrB = p_CurrB->p_Next;
        }
    }

    return (E_OK);
}

/****************************************************************
 *  Routine:     MmGetGreaterAlignment
 *
 *  Description:
 *      Allocates a block of memory according to the given size
 *      and the alignment. That routine is called from the MM_Get
 *      routine if the required alignment is greater then MM_MAX_ALIGNMENT.
 *      In that case, it goes over free blocks of 64 byte align list
 *      and checks if it has the required size of bytes of the required
 *      alignment. If no blocks found returns ILLEGAL_BASE.
 *      After the block is found and data is allocated, it calls
 *      the internal CutFree routine to update all free lists
 *      do not include a just allocated block. Of course, each
 *      free list contains a free blocks with the same alignment.
 *      It is also creates a busy block that holds
 *      information about an allocated block.
 *
 *  Arguments:
 *      MM              - handle to the MM object
 *      size            - size of the MM
 *      alignment       - index as a power of two defines
 *                        a required alignment that is greater then 64.
 *      name            - the name that specifies an allocated block.
 *
 *  Return value:
 *      base address of an allocated block.
 *      ILLEGAL_BASE if can't allocate a block
 *
 ****************************************************************/
static uint64_t MmGetGreaterAlignment(t_MM *p_MM, uint64_t size, uint64_t alignment, char* name)
{
    t_FreeBlock *p_FreeB;
    t_BusyBlock *p_NewBusyB;
    uint64_t    holdBase, holdEnd, alignBase = 0;

    /* goes over free blocks of the 64 byte alignment list
       and look for a block of the suitable size and
       base address according to the alignment. */
    p_FreeB = p_MM->freeBlocks[MM_MAX_ALIGNMENT];

    while ( p_FreeB )
    {
        alignBase = MAKE_ALIGNED(p_FreeB->base, alignment);

        /* the block is found if the aligned base inside the block
         * and has the anough size. */
        if ( alignBase >= p_FreeB->base &&
             alignBase < p_FreeB->end &&
             size <= (p_FreeB->end - alignBase) )
            break;
        else
            p_FreeB = p_FreeB->p_Next;
    }

    /* If such block isn't found */
    if ( !p_FreeB )
        return (uint64_t)(ILLEGAL_BASE);

    holdBase = alignBase;
    holdEnd = alignBase + size;

    /* init a new busy block */
    if ((p_NewBusyB = CreateBusyBlock(holdBase, size, name)) == NULL)
        return (uint64_t)(ILLEGAL_BASE);

    /* calls Update routine to update a lists of free blocks */
    if ( CutFree ( p_MM, holdBase, holdEnd ) != E_OK )
    {
        XX_Free(p_NewBusyB);
        return (uint64_t)(ILLEGAL_BASE);
    }

    /* insert the new busy block into the list of busy blocks */
    AddBusy ( p_MM, p_NewBusyB );

    return (holdBase);
}


/**********************************************************************
 *                     MM API routines set                            *
 **********************************************************************/

/*****************************************************************************/
t_Error MM_Init(t_Handle *h_MM, uint64_t base, uint64_t size)
{
    t_MM        *p_MM;
    uint64_t    newBase, newSize;
    int         i;

    if (!size)
    {
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Size (should be positive)"));
    }

    /* Initializes a new MM object */
    p_MM = (t_MM *)XX_Malloc(sizeof(t_MM));
    if (!p_MM)
    {
        RETURN_ERROR(MAJOR, E_NO_MEMORY, NO_MSG);
    }

    p_MM->h_Spinlock = XX_InitSpinlock();
    if (!p_MM->h_Spinlock)
    {
        XX_Free(p_MM);
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("MM spinlock!"));
    }

    /* Initializes counter of free memory to total size */
    p_MM->freeMemSize = size;

    /* A busy list is empty */
    p_MM->busyBlocks = 0;

    /* Initializes a new memory block */
    if ((p_MM->memBlocks = CreateNewBlock(base, size)) == NULL)
    {
        MM_Free(p_MM);
        RETURN_ERROR(MAJOR, E_NO_MEMORY, NO_MSG);
    }

    /* Initializes a new free block for each free list*/
    for (i=0; i <= MM_MAX_ALIGNMENT; i++)
    {
        newBase = MAKE_ALIGNED( base, (0x1 << i) );
        newSize = size - (newBase - base);

        if ((p_MM->freeBlocks[i] = CreateFreeBlock(newBase, newSize)) == NULL)
        {
            MM_Free(p_MM);
            RETURN_ERROR(MAJOR, E_NO_MEMORY, NO_MSG);
        }
    }

    *h_MM = p_MM;

    return (E_OK);
}

/*****************************************************************************/
void MM_Free(t_Handle h_MM)
{
    t_MM        *p_MM = (t_MM *)h_MM;
    t_MemBlock  *p_MemBlock;
    t_BusyBlock *p_BusyBlock;
    t_FreeBlock *p_FreeBlock;
    void        *p_Block;
    int         i;

    ASSERT_COND(p_MM);

    /* release memory allocated for busy blocks */
    p_BusyBlock = p_MM->busyBlocks;
    while ( p_BusyBlock )
    {
        p_Block = p_BusyBlock;
        p_BusyBlock = p_BusyBlock->p_Next;
        XX_Free(p_Block);
    }

    /* release memory allocated for free blocks */
    for (i=0; i <= MM_MAX_ALIGNMENT; i++)
    {
        p_FreeBlock = p_MM->freeBlocks[i];
        while ( p_FreeBlock )
        {
            p_Block = p_FreeBlock;
            p_FreeBlock = p_FreeBlock->p_Next;
            XX_Free(p_Block);
        }
    }

    /* release memory allocated for memory blocks */
    p_MemBlock = p_MM->memBlocks;
    while ( p_MemBlock )
    {
        p_Block = p_MemBlock;
        p_MemBlock = p_MemBlock->p_Next;
        XX_Free(p_Block);
    }

    if (p_MM->h_Spinlock)
        XX_FreeSpinlock(p_MM->h_Spinlock);

    /* release memory allocated for MM object itself */
    XX_Free(p_MM);
}

/*****************************************************************************/
uint64_t MM_Get(t_Handle h_MM, uint64_t size, uint64_t alignment, char* name)
{
    t_MM        *p_MM = (t_MM *)h_MM;
    t_FreeBlock *p_FreeB;
    t_BusyBlock *p_NewBusyB;
    uint64_t    holdBase, holdEnd, j, i = 0;
    uint32_t    intFlags;

    SANITY_CHECK_RETURN_VALUE(p_MM, E_INVALID_HANDLE, (uint64_t)ILLEGAL_BASE);

    /* checks that alignment value is greater then zero */
    if (alignment == 0)
    {
        alignment = 1;
    }

    j = alignment;

    /* checks if alignment is a power of two, if it correct and if the
       required size is multiple of the given alignment. */
    while ((j & 0x1) == 0)
    {
        i++;
        j = j >> 1;
    }

    /* if the given alignment isn't power of two, returns an error */
    if (j != 1)
    {
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("alignment (should be power of 2)"));
        return (uint64_t)ILLEGAL_BASE;
    }

    if (i > MM_MAX_ALIGNMENT)
    {
        return (MmGetGreaterAlignment(p_MM, size, alignment, name));
    }

    intFlags = XX_LockIntrSpinlock(p_MM->h_Spinlock);
    /* look for a block of the size greater or equal to the required size. */
    p_FreeB = p_MM->freeBlocks[i];
    while ( p_FreeB && (p_FreeB->end - p_FreeB->base) < size )
        p_FreeB = p_FreeB->p_Next;

    /* If such block is found */
    if ( !p_FreeB )
    {
        XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
        return (uint64_t)(ILLEGAL_BASE);
    }

    holdBase = p_FreeB->base;
    holdEnd = holdBase + size;

    /* init a new busy block */
    if ((p_NewBusyB = CreateBusyBlock(holdBase, size, name)) == NULL)
    {
        XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
        return (uint64_t)(ILLEGAL_BASE);
    }

    /* calls Update routine to update a lists of free blocks */
    if ( CutFree ( p_MM, holdBase, holdEnd ) != E_OK )
    {
        XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
        XX_Free(p_NewBusyB);
        return (uint64_t)(ILLEGAL_BASE);
    }

    /* Decreasing the allocated memory size from free memory size */
    p_MM->freeMemSize -= size;

    /* insert the new busy block into the list of busy blocks */
    AddBusy ( p_MM, p_NewBusyB );
    XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);

    return (holdBase);
}

/*****************************************************************************/
uint64_t MM_GetForce(t_Handle h_MM, uint64_t base, uint64_t size, char* name)
{
    t_MM        *p_MM = (t_MM *)h_MM;
    t_FreeBlock *p_FreeB;
    t_BusyBlock *p_NewBusyB;
    uint32_t    intFlags;
    bool        blockIsFree = FALSE;

    ASSERT_COND(p_MM);

    intFlags = XX_LockIntrSpinlock(p_MM->h_Spinlock);
    p_FreeB = p_MM->freeBlocks[0]; /* The biggest free blocks are in the
                                      free list with alignment 1 */

    while ( p_FreeB )
    {
        if ( base >= p_FreeB->base && (base+size) <= p_FreeB->end )
        {
            blockIsFree = TRUE;
            break;
        }
        else
            p_FreeB = p_FreeB->p_Next;
    }

    if ( !blockIsFree )
    {
        XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
        return (uint64_t)(ILLEGAL_BASE);
    }

    /* init a new busy block */
    if ((p_NewBusyB = CreateBusyBlock(base, size, name)) == NULL)
    {
        XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
        return (uint64_t)(ILLEGAL_BASE);
    }

    /* calls Update routine to update a lists of free blocks */
    if ( CutFree ( p_MM, base, base+size ) != E_OK )
    {
        XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
        XX_Free(p_NewBusyB);
        return (uint64_t)(ILLEGAL_BASE);
    }

    /* Decreasing the allocated memory size from free memory size */
    p_MM->freeMemSize -= size;

    /* insert the new busy block into the list of busy blocks */
    AddBusy ( p_MM, p_NewBusyB );
    XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);

    return (base);
}

/*****************************************************************************/
uint64_t MM_GetForceMin(t_Handle h_MM, uint64_t size, uint64_t alignment, uint64_t min, char* name)
{
    t_MM        *p_MM = (t_MM *)h_MM;
    t_FreeBlock *p_FreeB;
    t_BusyBlock *p_NewBusyB;
    uint64_t    holdBase, holdEnd, j = alignment, i=0;
    uint32_t    intFlags;

    ASSERT_COND(p_MM);

    /* checks if alignment is a power of two, if it correct and if the
       required size is multiple of the given alignment. */
    while ((j & 0x1) == 0)
    {
        i++;
        j = j >> 1;
    }

    if ( (j != 1) || (i > MM_MAX_ALIGNMENT) )
    {
        return (uint64_t)(ILLEGAL_BASE);
    }

    intFlags = XX_LockIntrSpinlock(p_MM->h_Spinlock);
    p_FreeB = p_MM->freeBlocks[i];

    /* look for the first block that contains the minimum
       base address. If the whole required size may be fit
       into it, use that block, otherwise look for the next
       block of size greater or equal to the required size. */
    while ( p_FreeB && (min >= p_FreeB->end))
            p_FreeB = p_FreeB->p_Next;

    /* If such block is found */
    if ( !p_FreeB )
    {
        XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
        return (uint64_t)(ILLEGAL_BASE);
    }

    /* if this block is large enough, use this block */
    holdBase = ( min <= p_FreeB->base ) ? p_FreeB->base : min;
    if ((holdBase + size) <= p_FreeB->end )
    {
        holdEnd = holdBase + size;
    }
    else
    {
        p_FreeB = p_FreeB->p_Next;
        while ( p_FreeB && ((p_FreeB->end - p_FreeB->base) < size) )
            p_FreeB = p_FreeB->p_Next;

        /* If such block is found */
        if ( !p_FreeB )
        {
            XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
            return (uint64_t)(ILLEGAL_BASE);
        }

        holdBase = p_FreeB->base;
        holdEnd = holdBase + size;
    }

    /* init a new busy block */
    if ((p_NewBusyB = CreateBusyBlock(holdBase, size, name)) == NULL)
    {
        XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
        return (uint64_t)(ILLEGAL_BASE);
    }

    /* calls Update routine to update a lists of free blocks */
    if ( CutFree( p_MM, holdBase, holdEnd ) != E_OK )
    {
        XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
        XX_Free(p_NewBusyB);
        return (uint64_t)(ILLEGAL_BASE);
    }

    /* Decreasing the allocated memory size from free memory size */
    p_MM->freeMemSize -= size;

    /* insert the new busy block into the list of busy blocks */
    AddBusy( p_MM, p_NewBusyB );
    XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);

    return (holdBase);
}

/*****************************************************************************/
uint64_t MM_Put(t_Handle h_MM, uint64_t base)
{
    t_MM        *p_MM = (t_MM *)h_MM;
    t_BusyBlock *p_BusyB, *p_PrevBusyB;
    uint64_t    size;
    uint32_t    intFlags;

    ASSERT_COND(p_MM);

    /* Look for a busy block that have the given base value.
     * That block will be returned back to the memory.
     */
    p_PrevBusyB = 0;

    intFlags = XX_LockIntrSpinlock(p_MM->h_Spinlock);
    p_BusyB = p_MM->busyBlocks;
    while ( p_BusyB && base != p_BusyB->base )
    {
        p_PrevBusyB = p_BusyB;
        p_BusyB = p_BusyB->p_Next;
    }

    if ( !p_BusyB )
    {
        XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
        return (uint64_t)(0);
    }

    if ( AddFree( p_MM, p_BusyB->base, p_BusyB->end ) != E_OK )
    {
        XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
        return (uint64_t)(0);
    }

    /* removes a busy block form the list of busy blocks */
    if ( p_PrevBusyB )
        p_PrevBusyB->p_Next = p_BusyB->p_Next;
    else
        p_MM->busyBlocks = p_BusyB->p_Next;

    size = p_BusyB->end - p_BusyB->base;

    /* Adding the deallocated memory size to free memory size */
    p_MM->freeMemSize += size;

    XX_Free(p_BusyB);
    XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);

    return (size);
}

/*****************************************************************************/
uint64_t MM_PutForce(t_Handle h_MM, uint64_t base, uint64_t size)
{
    t_MM        *p_MM = (t_MM *)h_MM;
    uint64_t    end = base + size;
    uint32_t    intFlags;

    ASSERT_COND(p_MM);

    intFlags = XX_LockIntrSpinlock(p_MM->h_Spinlock);

    if ( CutBusy( p_MM, base, end ) != E_OK )
    {
        XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
        return (uint64_t)(0);
    }

    if ( AddFree ( p_MM, base, end ) != E_OK )
    {
        XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
        return (uint64_t)(0);
    }

    /* Adding the deallocated memory size to free memory size */
    p_MM->freeMemSize += size;

    XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);

    return (size);
}

/*****************************************************************************/
t_Error MM_Add(t_Handle h_MM, uint64_t base, uint64_t size)
{
    t_MM        *p_MM = (t_MM *)h_MM;
    t_MemBlock  *p_MemB, *p_NewMemB;
    t_Error     errCode;
    uint32_t    intFlags;

    ASSERT_COND(p_MM);

    /* find a last block in the list of memory blocks to insert a new
     * memory block
     */
    intFlags = XX_LockIntrSpinlock(p_MM->h_Spinlock);

    p_MemB = p_MM->memBlocks;
    while ( p_MemB->p_Next )
    {
        if ( base >= p_MemB->base && base < p_MemB->end )
        {
        XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
            RETURN_ERROR(MAJOR, E_ALREADY_EXISTS, NO_MSG);
        }
        p_MemB = p_MemB->p_Next;
    }
    /* check for a last memory block */
    if ( base >= p_MemB->base && base < p_MemB->end )
    {
        XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
        RETURN_ERROR(MAJOR, E_ALREADY_EXISTS, NO_MSG);
    }

    /* create a new memory block */
    if ((p_NewMemB = CreateNewBlock(base, size)) == NULL)
    {
        XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
        RETURN_ERROR(MAJOR, E_NO_MEMORY, NO_MSG);
    }

    /* append a new memory block to the end of the list of memory blocks */
    p_MemB->p_Next = p_NewMemB;

    /* add a new free block to the free lists */
    errCode = AddFree(p_MM, base, base+size);
    if (errCode)
    {
        XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);
        p_MemB->p_Next = 0;
        XX_Free(p_NewMemB);
        return ((t_Error)errCode);
    }

    /* Adding the new block size to free memory size */
    p_MM->freeMemSize += size;

    XX_UnlockIntrSpinlock(p_MM->h_Spinlock, intFlags);

    return (E_OK);
}

/*****************************************************************************/
uint64_t MM_GetMemBlock(t_Handle h_MM, int index)
{
    t_MM       *p_MM = (t_MM*)h_MM;
    t_MemBlock *p_MemBlock;
    int         i;

    ASSERT_COND(p_MM);

    p_MemBlock = p_MM->memBlocks;
    for (i=0; i < index; i++)
        p_MemBlock = p_MemBlock->p_Next;

    if ( p_MemBlock )
        return (p_MemBlock->base);
    else
        return (uint64_t)ILLEGAL_BASE;
}

/*****************************************************************************/
uint64_t MM_GetBase(t_Handle h_MM)
{
    t_MM       *p_MM = (t_MM*)h_MM;
    t_MemBlock *p_MemBlock;

    ASSERT_COND(p_MM);

    p_MemBlock = p_MM->memBlocks;
    return  p_MemBlock->base;
}

/*****************************************************************************/
bool MM_InRange(t_Handle h_MM, uint64_t addr)
{
    t_MM       *p_MM = (t_MM*)h_MM;
    t_MemBlock *p_MemBlock;

    ASSERT_COND(p_MM);

    p_MemBlock = p_MM->memBlocks;

    if ((addr >= p_MemBlock->base) && (addr < p_MemBlock->end))
        return TRUE;
    else
        return FALSE;
}

/*****************************************************************************/
uint64_t MM_GetFreeMemSize(t_Handle h_MM)
{
    t_MM       *p_MM = (t_MM*)h_MM;

    ASSERT_COND(p_MM);

    return p_MM->freeMemSize;
}

/*****************************************************************************/
void MM_Dump(t_Handle h_MM)
{
    t_MM        *p_MM = (t_MM *)h_MM;
    t_FreeBlock *p_FreeB;
    t_BusyBlock *p_BusyB;
    int          i;

    p_BusyB = p_MM->busyBlocks;
    XX_Print("List of busy blocks:\n");
    while (p_BusyB)
    {
        XX_Print("\t0x%p: (%s: b=0x%llx, e=0x%llx)\n", p_BusyB, p_BusyB->name, p_BusyB->base, p_BusyB->end );
        p_BusyB = p_BusyB->p_Next;
    }

    XX_Print("\nLists of free blocks according to alignment:\n");
    for (i=0; i <= MM_MAX_ALIGNMENT; i++)
    {
        XX_Print("%d alignment:\n", (0x1 << i));
        p_FreeB = p_MM->freeBlocks[i];
        while (p_FreeB)
        {
            XX_Print("\t0x%p: (b=0x%llx, e=0x%llx)\n", p_FreeB, p_FreeB->base, p_FreeB->end);
            p_FreeB = p_FreeB->p_Next;
        }
        XX_Print("\n");
    }
}
