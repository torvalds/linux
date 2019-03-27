/* Copyright (c) 2008-2012 Freescale Semiconductor, Inc
 * All rights reserved.
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


/**************************************************************************//**

 @File          mem_ext.h

 @Description   External prototypes for the memory manager object
*//***************************************************************************/

#ifndef __MEM_EXT_H
#define __MEM_EXT_H

#include "std_ext.h"
#include "part_ext.h"


/**************************************************************************//**
 @Group         etc_id   Utility Library Application Programming Interface

 @Description   External routines.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         mem_id   Slab Memory Manager

 @Description   Slab Memory Manager module functions, definitions and enums.

 @{
*//***************************************************************************/

/* Each block is of the following structure:
 *
 *
 *  +-----------+----------+---------------------------+-----------+-----------+
 *  | Alignment |  Prefix  | Data                      | Postfix   | Alignment |
 *  |  field    |   field  |  field                    |   field   | Padding   |
 *  |           |          |                           |           |           |
 *  +-----------+----------+---------------------------+-----------+-----------+
 *  and at the beginning of all bytes, an additional optional padding might reside
 *  to ensure that the first blocks data field is aligned as requested.
 */


#define MEM_MAX_NAME_LENGTH     8

/**************************************************************************//*
 @Description   Memory Segment structure
*//***************************************************************************/

typedef struct
{
    char        name[MEM_MAX_NAME_LENGTH];
                                    /* The segment's name */
    uint8_t     **p_Bases;          /* Base addresses of the segments */
    uint8_t     **p_BlocksStack;    /* Array of pointers to blocks */
    t_Handle    h_Spinlock;
    uint16_t    dataSize;           /* Size of each data block */
    uint16_t    prefixSize;         /* How many bytes to reserve before the data */
    uint16_t    postfixSize;        /* How many bytes to reserve after the data */
    uint16_t    alignment;          /* Requested alignment for the data field */
    int         allocOwner;         /* Memory allocation owner */
    uint32_t    getFailures;        /* Number of times get failed */
    uint32_t    num;                /* Number of blocks in segment */
    uint32_t    current;            /* Current block */
    bool        consecutiveMem;     /* Allocate consecutive data blocks memory */
#ifdef DEBUG_MEM_LEAKS
    void        *p_MemDbg;          /* MEM debug database (MEM leaks detection) */
    uint32_t    blockOffset;
    uint32_t    blockSize;
#endif /* DEBUG_MEM_LEAKS */
} t_MemorySegment;



/**************************************************************************//**
 @Function      MEM_Init

 @Description   Create a new memory segment.

 @Param[in]     name        - Name of memory partition.
 @Param[in]     p_Handle    - Handle to new segment is returned through here.
 @Param[in]     num         - Number of blocks in new segment.
 @Param[in]     dataSize    - Size of blocks in segment.
 @Param[in]     prefixSize  - How many bytes to allocate before the data.
 @Param[in]     postfixSize - How many bytes to allocate after the data.
 @Param[in]     alignment   - Requested alignment for data field (in bytes).

 @Return        E_OK - success, E_NO_MEMORY - out of memory.
*//***************************************************************************/
t_Error MEM_Init(char     name[],
                 t_Handle *p_Handle,
                 uint32_t num,
                 uint16_t dataSize,
                 uint16_t prefixSize,
                 uint16_t postfixSize,
                 uint16_t alignment);

/**************************************************************************//**
 @Function      MEM_InitSmart

 @Description   Create a new memory segment.

 @Param[in]     name            - Name of memory partition.
 @Param[in]     p_Handle        - Handle to new segment is returned through here.
 @Param[in]     num             - Number of blocks in new segment.
 @Param[in]     dataSize        - Size of blocks in segment.
 @Param[in]     prefixSize      - How many bytes to allocate before the data.
 @Param[in]     postfixSize     - How many bytes to allocate after the data.
 @Param[in]     alignment       - Requested alignment for data field (in bytes).
 @Param[in]     memPartitionId  - Memory partition ID for allocation.
 @Param[in]     consecutiveMem  - Whether to allocate the memory blocks
                                  continuously or not.

 @Return        E_OK - success, E_NO_MEMORY - out of memory.
*//***************************************************************************/
t_Error MEM_InitSmart(char      name[],
                      t_Handle  *p_Handle,
                      uint32_t  num,
                      uint16_t  dataSize,
                      uint16_t  prefixSize,
                      uint16_t  postfixSize,
                      uint16_t  alignment,
                      uint8_t   memPartitionId,
                      bool      consecutiveMem);

/**************************************************************************//**
 @Function      MEM_InitByAddress

 @Description   Create a new memory segment with a specified base address.

 @Param[in]     name        - Name of memory partition.
 @Param[in]     p_Handle    - Handle to new segment is returned through here.
 @Param[in]     num         - Number of blocks in new segment.
 @Param[in]     dataSize    - Size of blocks in segment.
 @Param[in]     prefixSize  - How many bytes to allocate before the data.
 @Param[in]     postfixSize - How many bytes to allocate after the data.
 @Param[in]     alignment   - Requested alignment for data field (in bytes).
 @Param[in]     address     - The required base address.

 @Return        E_OK - success, E_NO_MEMORY - out of memory.
 *//***************************************************************************/
t_Error MEM_InitByAddress(char        name[],
                          t_Handle    *p_Handle,
                          uint32_t    num,
                          uint16_t    dataSize,
                          uint16_t    prefixSize,
                          uint16_t    postfixSize,
                          uint16_t    alignment,
                          uint8_t     *address);

/**************************************************************************//**
 @Function      MEM_Free

 @Description   Free a specific memory segment.

 @Param[in]     h_Mem - Handle to memory segment.

 @Return        None.
*//***************************************************************************/
void MEM_Free(t_Handle h_Mem);

/**************************************************************************//**
 @Function      MEM_Get

 @Description   Get a block of memory from a segment.

 @Param[in]     h_Mem - Handle to memory segment.

 @Return        Pointer to new memory block on success,0 otherwise.
*//***************************************************************************/
void * MEM_Get(t_Handle h_Mem);

/**************************************************************************//**
 @Function      MEM_GetN

 @Description   Get up to N blocks of memory from a segment.

                The blocks are assumed to be of a fixed size (one size per segment).

 @Param[in]     h_Mem   - Handle to memory segment.
 @Param[in]     num     - Number of blocks to allocate.
 @Param[out]    array   - Array of at least num pointers to which the addresses
                          of the allocated blocks are written.

 @Return        The number of blocks actually allocated.

 @Cautions      Interrupts are disabled for all of the allocation loop.
                Although this loop is very short for each block (several machine
                instructions), you should not allocate a very large number
                of blocks via this routine.
*//***************************************************************************/
uint16_t MEM_GetN(t_Handle h_Mem, uint32_t num, void *array[]);

/**************************************************************************//**
 @Function      MEM_Put

 @Description   Put a block of memory back to a segment.

 @Param[in]     h_Mem   - Handle to memory segment.
 @Param[in]     p_Block - The block to return.

 @Return        Pointer to new memory block on success,0 otherwise.
*//***************************************************************************/
t_Error MEM_Put(t_Handle h_Mem, void *p_Block);

/**************************************************************************//**
 @Function      MEM_ComputePartitionSize

 @Description   calculate a tight upper boundary of the size of a partition with
                given attributes.

                The returned value is suitable if one wants to use MEM_InitByAddress().

 @Param[in]     num         - The number of blocks in the segment.
 @Param[in]     dataSize    - Size of block to get.
 @Param[in]     prefixSize  - The prefix size
 @Param         postfixSize - The postfix size
 @Param[in]     alignment   - The requested alignment value (in bytes)

 @Return        The memory block size a segment with the given attributes needs.
*//***************************************************************************/
uint32_t MEM_ComputePartitionSize(uint32_t num,
                                  uint16_t dataSize,
                                  uint16_t prefixSize,
                                  uint16_t postfixSize,
                                  uint16_t alignment);

#ifdef DEBUG_MEM_LEAKS
#if !((defined(__MWERKS__) || defined(__GNUC__)) && (__dest_os == __ppc_eabi))
#error  "Memory-Leaks-Debug option is supported only for freescale CodeWarrior"
#endif /* !(defined(__MWERKS__) && ... */

/**************************************************************************//**
 @Function      MEM_CheckLeaks

 @Description   Report MEM object leaks.

                This routine is automatically called by the MEM_Free() routine,
                but it can also be invoked while the MEM object is alive.

 @Param[in]     h_Mem - Handle to memory segment.

 @Return        None.
*//***************************************************************************/
void MEM_CheckLeaks(t_Handle h_Mem);

#else  /* not DEBUG_MEM_LEAKS */
#define MEM_CheckLeaks(h_Mem)
#endif /* not DEBUG_MEM_LEAKS */

/**************************************************************************//**
 @Description   Get base of MEM
*//***************************************************************************/
#define MEM_GetBase(h_Mem)             ((t_MemorySegment *)(h_Mem))->p_Bases[0]

/**************************************************************************//**
 @Description   Get size of MEM block
*//***************************************************************************/
#define MEM_GetSize(h_Mem)             ((t_MemorySegment *)(h_Mem))->dataSize

/**************************************************************************//**
 @Description   Get prefix size of MEM block
*//***************************************************************************/
#define MEM_GetPrefixSize(h_Mem)       ((t_MemorySegment *)(h_Mem))->prefixSize

/**************************************************************************//**
 @Description   Get postfix size of MEM block
*//***************************************************************************/
#define MEM_GetPostfixSize(h_Mem)      ((t_MemorySegment *)(h_Mem))->postfixSize

/**************************************************************************//**
 @Description   Get alignment of MEM block (in bytes)
*//***************************************************************************/
#define MEM_GetAlignment(h_Mem)        ((t_MemorySegment *)(h_Mem))->alignment

/**************************************************************************//**
 @Description   Get the number of blocks in the segment
*//***************************************************************************/
#define MEM_GetNumOfBlocks(h_Mem)             ((t_MemorySegment *)(h_Mem))->num

/** @} */ /* end of MEM group */
/** @} */ /* end of etc_id group */


#endif /* __MEM_EXT_H */
