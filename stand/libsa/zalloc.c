/*
 * This module derived from code donated to the FreeBSD Project by 
 * Matthew Dillon <dillon@backplane.com>
 *
 * Copyright (c) 1998 The FreeBSD Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * LIB/MEMORY/ZALLOC.C	- self contained low-overhead memory pool/allocation 
 *			  subsystem
 *
 *	This subsystem implements memory pools and memory allocation 
 *	routines.
 *
 *	Pools are managed via a linked list of 'free' areas.  Allocating
 *	memory creates holes in the freelist, freeing memory fills them.
 *	Since the freelist consists only of free memory areas, it is possible
 *	to allocate the entire pool without incuring any structural overhead.
 *
 *	The system works best when allocating similarly-sized chunks of
 *	memory.  Care must be taken to avoid fragmentation when 
 *	allocating/deallocating dissimilar chunks.
 *
 *	When a memory pool is first allocated, the entire pool is marked as
 *	allocated.  This is done mainly because we do not want to modify any
 *	portion of a pool's data area until we are given permission.  The
 *	caller must explicitly deallocate portions of the pool to make them
 *	available.
 *
 *	z[n]xalloc() works like z[n]alloc() but the allocation is made from
 *	within the specified address range.  If the segment could not be 
 *	allocated, NULL is returned.  WARNING!  The address range will be
 *	aligned to an 8 or 16 byte boundry depending on the cpu so if you
 *	give an unaligned address range, unexpected results may occur.
 *
 *	If a standard allocation fails, the reclaim function will be called
 *	to recover some space.  This usually causes other portions of the
 *	same pool to be released.  Memory allocations at this low level
 *	should not block but you can do that too in your reclaim function
 *	if you want.  Reclaim does not function when z[n]xalloc() is used,
 *	only for z[n]alloc().
 *
 *	Allocation and frees of 0 bytes are valid operations.
 */

#include "zalloc_defs.h"

/*
 * Objects in the pool must be aligned to at least the size of struct MemNode.
 * They must also be aligned to MALLOCALIGN, which should normally be larger
 * than the struct, so assert that to be so at compile time.
 */
typedef char assert_align[(sizeof(struct MemNode) <= MALLOCALIGN) ? 1 : -1];

#define	MEMNODE_SIZE_MASK	MALLOCALIGN_MASK

/*
 * znalloc() -	allocate memory (without zeroing) from pool.  Call reclaim
 *		and retry if appropriate, return NULL if unable to allocate
 *		memory.
 */

void *
znalloc(MemPool *mp, uintptr_t bytes)
{
    /*
     * align according to pool object size (can be 0).  This is
     * inclusive of the MEMNODE_SIZE_MASK minimum alignment.
     *
     */
    bytes = (bytes + MEMNODE_SIZE_MASK) & ~MEMNODE_SIZE_MASK;

    if (bytes == 0)
	return((void *)-1);

    /*
     * locate freelist entry big enough to hold the object.  If all objects
     * are the same size, this is a constant-time function.
     */

    if (bytes <= mp->mp_Size - mp->mp_Used) {
	MemNode **pmn;
	MemNode *mn;

	for (pmn = &mp->mp_First; (mn=*pmn) != NULL; pmn = &mn->mr_Next) {
	    if (bytes > mn->mr_Bytes)
		continue;

	    /*
	     *  Cut a chunk of memory out of the beginning of this
	     *  block and fixup the link appropriately.
	     */

	    {
		char *ptr = (char *)mn;

		if (mn->mr_Bytes == bytes) {
		    *pmn = mn->mr_Next;
		} else {
		    mn = (MemNode *)((char *)mn + bytes);
		    mn->mr_Next  = ((MemNode *)ptr)->mr_Next;
		    mn->mr_Bytes = ((MemNode *)ptr)->mr_Bytes - bytes;
		    *pmn = mn;
		}
		mp->mp_Used += bytes;
		return(ptr);
	    }
	}
    }

    /*
     * Memory pool is full, return NULL.
     */

    return(NULL);
}

/*
 * zfree() - free previously allocated memory
 */

void
zfree(MemPool *mp, void *ptr, uintptr_t bytes)
{
    /*
     * align according to pool object size (can be 0).  This is
     * inclusive of the MEMNODE_SIZE_MASK minimum alignment.
     */
    bytes = (bytes + MEMNODE_SIZE_MASK) & ~MEMNODE_SIZE_MASK;

    if (bytes == 0)
	return;

    /*
     * panic if illegal pointer
     */

    if ((char *)ptr < (char *)mp->mp_Base || 
	(char *)ptr + bytes > (char *)mp->mp_End ||
	((uintptr_t)ptr & MEMNODE_SIZE_MASK) != 0)
	panic("zfree(%p,%ju): wild pointer", ptr, (uintmax_t)bytes);

    /*
     * free the segment
     */

    {
	MemNode **pmn;
	MemNode *mn;

	mp->mp_Used -= bytes;

	for (pmn = &mp->mp_First; (mn = *pmn) != NULL; pmn = &mn->mr_Next) {
	    /*
	     * If area between last node and current node
	     *  - check range
	     *  - check merge with next area
	     *  - check merge with previous area
	     */
	    if ((char *)ptr <= (char *)mn) {
		/*
		 * range check
		 */
		if ((char *)ptr + bytes > (char *)mn) {
		    panic("zfree(%p,%ju): corrupt memlist1", ptr,
			(uintmax_t)bytes);
		}

		/*
		 * merge against next area or create independant area
		 */

		if ((char *)ptr + bytes == (char *)mn) {
		    ((MemNode *)ptr)->mr_Next = mn->mr_Next;
		    ((MemNode *)ptr)->mr_Bytes= bytes + mn->mr_Bytes;
		} else {
		    ((MemNode *)ptr)->mr_Next = mn;
		    ((MemNode *)ptr)->mr_Bytes= bytes;
		}
		*pmn = mn = (MemNode *)ptr;

		/*
		 * merge against previous area (if there is a previous
		 * area).
		 */

		if (pmn != &mp->mp_First) {
		    if ((char*)pmn + ((MemNode*)pmn)->mr_Bytes == (char*)ptr) {
			((MemNode *)pmn)->mr_Next = mn->mr_Next;
			((MemNode *)pmn)->mr_Bytes += mn->mr_Bytes;
			mn = (MemNode *)pmn;
		    }
		}
		return;
		/* NOT REACHED */
	    }
	    if ((char *)ptr < (char *)mn + mn->mr_Bytes) {
		panic("zfree(%p,%ju): corrupt memlist2", ptr,
		    (uintmax_t)bytes);
	    }
	}
	/*
	 * We are beyond the last MemNode, append new MemNode.  Merge against
	 * previous area if possible.
	 */
	if (pmn == &mp->mp_First || 
	    (char *)pmn + ((MemNode *)pmn)->mr_Bytes != (char *)ptr
	) {
	    ((MemNode *)ptr)->mr_Next = NULL;
	    ((MemNode *)ptr)->mr_Bytes = bytes;
	    *pmn = (MemNode *)ptr;
	    mn = (MemNode *)ptr;
	} else {
	    ((MemNode *)pmn)->mr_Bytes += bytes;
	    mn = (MemNode *)pmn;
	}
    }
}

/*
 * zextendPool() - extend memory pool to cover additional space.
 *
 *		   Note: the added memory starts out as allocated, you
 *		   must free it to make it available to the memory subsystem.
 *
 *		   Note: mp_Size may not reflect (mp_End - mp_Base) range
 *		   due to other parts of the system doing their own sbrk()
 *		   calls.
 */

void
zextendPool(MemPool *mp, void *base, uintptr_t bytes)
{
    if (mp->mp_Size == 0) {
	mp->mp_Base = base;
	mp->mp_Used = bytes;
	mp->mp_End = (char *)base + bytes;
	mp->mp_Size = bytes;
    } else {
	void *pend = (char *)mp->mp_Base + mp->mp_Size;

	if (base < mp->mp_Base) {
	    mp->mp_Size += (char *)mp->mp_Base - (char *)base;
	    mp->mp_Used += (char *)mp->mp_Base - (char *)base;
	    mp->mp_Base = base;
	}
	base = (char *)base + bytes;
	if (base > pend) {
	    mp->mp_Size += (char *)base - (char *)pend;
	    mp->mp_Used += (char *)base - (char *)pend;
	    mp->mp_End = (char *)base;
	}
    }
}

#ifdef ZALLOCDEBUG

void
zallocstats(MemPool *mp)
{
    int abytes = 0;
    int hbytes = 0;
    int fcount = 0;
    MemNode *mn;

    printf("%d bytes reserved", (int) mp->mp_Size);

    mn = mp->mp_First;

    if ((void *)mn != (void *)mp->mp_Base) {
	abytes += (char *)mn - (char *)mp->mp_Base;
    }

    while (mn) {
	if ((char *)mn + mn->mr_Bytes != mp->mp_End) {
	    hbytes += mn->mr_Bytes;
	    ++fcount;
	}
	if (mn->mr_Next)
	    abytes += (char *)mn->mr_Next - ((char *)mn + mn->mr_Bytes);
	mn = mn->mr_Next;
    }
    printf(" %d bytes allocated\n%d fragments (%d bytes fragmented)\n",
	abytes,
	fcount,
	hbytes
    );
}

#endif

