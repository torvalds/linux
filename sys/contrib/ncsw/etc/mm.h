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


/****************************************************************
 *
 * File:  mm.h
 *
 *
 * Description:
 *  MM (Memory Management) object definitions.
 *  It also includes definitions of the Free Block, Busy Block
 *  and Memory Block structures used by the MM object.
 *
 ****************************************************************/

#ifndef __MM_H
#define __MM_H


#include "mm_ext.h"

#define __ERR_MODULE__  MODULE_MM


#define MAKE_ALIGNED(addr, align)    \
    (((uint64_t)(addr) + ((align) - 1)) & (~(((uint64_t)align) - 1)))


/* t_MemBlock data structure defines parameters of the Memory Block */
typedef struct t_MemBlock
{
    struct t_MemBlock *p_Next;      /* Pointer to the next memory block */

    uint64_t  base;                 /* Base address of the memory block */
    uint64_t  end;                  /* End address of the memory block */
} t_MemBlock;


/* t_FreeBlock data structure defines parameters of the Free Block */
typedef struct t_FreeBlock
{
    struct t_FreeBlock *p_Next;     /* Pointer to the next free block */

    uint64_t  base;                 /* Base address of the block */
    uint64_t  end;                  /* End address of the block */
} t_FreeBlock;


/* t_BusyBlock data structure defines parameters of the Busy Block  */
typedef struct t_BusyBlock
{
    struct t_BusyBlock *p_Next;         /* Pointer to the next free block */

    uint64_t    base;                   /* Base address of the block */
    uint64_t    end;                    /* End address of the block */
    char        name[MM_MAX_NAME_LEN];  /* That block of memory was allocated for
                                           something specified by the Name */
} t_BusyBlock;


/* t_MM data structure defines parameters of the MM object */
typedef struct t_MM
{
    t_Handle        h_Spinlock;

    t_MemBlock      *memBlocks;     /* List of memory blocks (Memory list) */
    t_BusyBlock     *busyBlocks;    /* List of busy blocks (Busy list) */
    t_FreeBlock     *freeBlocks[MM_MAX_ALIGNMENT + 1];
                                    /* Alignment lists of free blocks (Free lists) */

    uint64_t        freeMemSize;    /* Total size of free memory (in bytes) */
} t_MM;


#endif /* __MM_H */
