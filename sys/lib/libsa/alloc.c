/*	$OpenBSD: alloc.c,v 1.13 2018/12/16 08:31:50 otto Exp $	*/
/*	$NetBSD: alloc.c,v 1.6 1997/02/04 18:36:33 thorpej Exp $	*/

/*
 * Copyright (c) 1997 Christopher G. Demetriou.  All rights reserved.
 * Copyright (c) 1996
 *	Matthias Drochner.  All rights reserved.
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)alloc.c	8.1 (Berkeley) 6/11/93
 *
 *
 * Copyright (c) 1989, 1990, 1991 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Author: Alessandro Forin
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 * Dynamic memory allocator.
 *
 * Compile options:
 *
 *	ALLOC_TRACE	enable tracing of allocations/deallocations
 *
 *	ALLOC_FIRST_FIT	use a first-fit allocation algorithm, rather than
 *			the default best-fit algorithm.
 *
 *	HEAP_LIMIT	heap limit address (defaults to "no limit").
 *
 *	HEAP_START	start address of heap (defaults to '&end').
 *
 *	NEEDS_HEAP_H	needs to #include "heap.h" to declare things
 *			needed by HEAP_LIMIT and/or HEAP_START.
 *
 *	NEEDS_HEAP_INIT	needs to invoke heap_init() to initialize
 *			heap boundaries.
 *
 *	DEBUG		enable debugging sanity checks.
 */

#include <sys/param.h>

/*
 * Each block actually has ALIGN(unsigned) + ALIGN(size) bytes allocated
 * to it, as follows:
 *
 * 0 ... (sizeof(unsigned) - 1)
 *	allocated or unallocated: holds size of user-data part of block.
 *
 * sizeof(unsigned) ... (ALIGN(sizeof(unsigned)) - 1)
 *	allocated: unused
 *	unallocated: depends on packing of struct fl
 *
 * ALIGN(sizeof(unsigned)) ... (ALIGN(sizeof(unsigned)) + ALIGN(data size) - 1)
 *	allocated: user data
 *	unallocated: depends on packing of struct fl
 *
 * 'next' is only used when the block is unallocated (i.e. on the free list).
 * However, note that ALIGN(sizeof(unsigned)) + ALIGN(data size) must
 * be at least 'sizeof(struct fl)', so that blocks can be used as structures
 * when on the free list.
 */

#include "stand.h"

struct fl {
	unsigned	size;
	struct fl	*next;
} *freelist = NULL;

#ifdef NEEDS_HEAP_H
#include "heap.h"
#endif
#ifndef NEEDS_HEAP_INIT
#ifdef HEAP_START
static char *top = (char *)HEAP_START;
#else
extern char end[];
static char *top = end;
#endif
#endif

void *
alloc(unsigned int size)
{
	struct fl **f = &freelist, **bestf = NULL;
#ifndef ALLOC_FIRST_FIT
	unsigned bestsize = 0xffffffff;	/* greater than any real size */
#endif
	char *help;
	int failed;

#ifdef NEEDS_HEAP_INIT
	heap_init();
#endif

#ifdef ALLOC_TRACE
	printf("alloc(%u)", size);
#endif

#ifdef ALLOC_FIRST_FIT
	while (*f != NULL && (*f)->size < size)
		f = &((*f)->next);
	bestf = f;
	failed = (*bestf == NULL);
#else
	/* scan freelist */
	while (*f) {
		if ((*f)->size >= size) {
			if ((*f)->size == size) /* exact match */
				goto found;

			if ((*f)->size < bestsize) {
				/* keep best fit */
				bestf = f;
				bestsize = (*f)->size;
			}
		}
		f = &((*f)->next);
	}

	/* no match in freelist if bestsize unchanged */
	failed = (bestsize == 0xffffffff || bestsize >= size * 2);
#endif

	if (failed) { /* nothing found */
		/*
		 * allocate from heap, keep chunk len in
		 * first word
		 */
		help = top;

		/* make _sure_ the region can hold a struct fl. */
		if (size < ALIGN(sizeof (struct fl *)))
			size = ALIGN(sizeof (struct fl *));
		top += ALIGN(sizeof(unsigned)) + ALIGN(size);
#ifdef HEAP_LIMIT
		if (top > (char *)HEAP_LIMIT)
			panic("heap full (0x%lx+%u)", help, size);
#endif
		*(unsigned *)help = ALIGN(size);
#ifdef ALLOC_TRACE
		printf("=%p\n", help + ALIGN(sizeof(unsigned)));
#endif
		return(help + ALIGN(sizeof(unsigned)));
	}

	/* we take the best fit */
	f = bestf;

#ifndef ALLOC_FIRST_FIT
found:
#endif
	/* remove from freelist */
	help = (char *)*f;
	*f = (*f)->next;
#ifdef ALLOC_TRACE
	printf("=%p (origsize %u)\n", help + ALIGN(sizeof(unsigned)),
	    *(unsigned *)help);
#endif
	return(help + ALIGN(sizeof(unsigned)));
}

void
free(void *ptr, unsigned int size)
{
	struct fl *f;

	if (ptr == NULL)
		return;

	f = (struct fl *)((char *)ptr - ALIGN(sizeof(unsigned)));

#ifdef ALLOC_TRACE
	printf("free(%p, %u) (origsize %u)\n", ptr, size, f->size);
#endif
#ifdef DEBUG
	if (size > f->size)
		printf("free %u bytes @%p, should be <=%u\n",
		    size, ptr, f->size);
#ifdef HEAP_START
	if (ptr < (void *)HEAP_START)
#else
	if (ptr < (void *)end)
#endif
		printf("free: %lx before start of heap.\n", (u_long)ptr);

#ifdef HEAP_LIMIT
	if (ptr > (void *)HEAP_LIMIT)
		printf("free: %lx beyond end of heap.\n", (u_long)ptr);
#endif
#endif /* DEBUG */
	/* put into freelist */
	f->next = freelist;
	freelist = f;
}
