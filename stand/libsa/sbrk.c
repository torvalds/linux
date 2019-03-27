/*-
 * Copyright (c) 1998 Michael Smith
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
 * Minimal sbrk() emulation required for malloc support.
 */

#include <string.h>
#include "stand.h"
#include "zalloc_defs.h"

static size_t	maxheap, heapsize = 0;
static void	*heapbase;

void
setheap(void *base, void *top)
{
    /* Align start address for the malloc code.  Sigh. */
    heapbase = (void *)(((uintptr_t)base + MALLOCALIGN_MASK) & 
        ~MALLOCALIGN_MASK);
    maxheap = (char *)top - (char *)heapbase;
}

char *
sbrk(int incr)
{
    char	*ret;
    
    if (heapbase == 0)
	    panic("No heap setup");

    if ((heapsize + incr) <= maxheap) {
	ret = (char *)heapbase + heapsize;
	bzero(ret, incr);
	heapsize += incr;
	return(ret);
    }
    errno = ENOMEM;
    return((char *)-1);
}

