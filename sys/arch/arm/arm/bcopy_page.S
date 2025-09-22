/*	$OpenBSD: bcopy_page.S,v 1.3 2018/08/06 18:39:13 kettenis Exp $	*/
/*	$NetBSD: bcopy_page.S,v 1.7 2003/10/13 21:03:13 scw Exp $	*/


/*
 * Copyright (c) 1995 Scott Stevens
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Scott Stevens.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * bcopy_page.S
 *
 * page optimised bcopy and bzero routines
 *
 * Created      : 08/04/95
 */

#include "assym.h"

#include <machine/asm.h>

/* #define BIG_LOOPS */

/*
 * bcopy_page(src, dest)
 *
 * Optimised copy page routine.
 *
 * On entry:
 *   r0 - src address
 *   r1 - dest address
 *
 * Requires:
 *   number of bytes per page (PAGE_SIZE) is a multiple of 512 (BIG_LOOPS), 128
 *   otherwise.
 */

#define	CHUNK_SIZE	32

#define	PREFETCH_FIRST_CHUNK	/* nothing */
#define	PREFETCH_NEXT_CHUNK	/* nothing */

#ifndef COPY_CHUNK
#define	COPY_CHUNK \
	PREFETCH_NEXT_CHUNK ; \
	ldmia	r0!, {r3-r8,ip,lr} ; \
	stmia	r1!, {r3-r8,ip,lr}
#endif /* ! COPY_CHUNK */

#ifndef SAVE_REGS
#define	SAVE_REGS	stmfd	sp!, {r4-r8, lr}
#define	RESTORE_REGS	ldmfd	sp!, {r4-r8, pc}
#endif

ENTRY(bcopy_page)
	PREFETCH_FIRST_CHUNK
	SAVE_REGS
#ifdef BIG_LOOPS
	mov	r2, #(PAGE_SIZE >> 9)
#else
	mov	r2, #(PAGE_SIZE >> 7)
#endif

1:
	COPY_CHUNK
	COPY_CHUNK
	COPY_CHUNK
	COPY_CHUNK

#ifdef BIG_LOOPS
	/* There is little point making the loop any larger; unless we are
	   running with the cache off, the load/store overheads will
	   completely dominate this loop.  */
	COPY_CHUNK
	COPY_CHUNK
	COPY_CHUNK
	COPY_CHUNK

	COPY_CHUNK
	COPY_CHUNK
	COPY_CHUNK
	COPY_CHUNK

	COPY_CHUNK
	COPY_CHUNK
	COPY_CHUNK
	COPY_CHUNK
#endif
	subs	r2, r2, #1
	bne	1b

	RESTORE_REGS		/* ...and return. */

/*
 * bzero_page(dest)
 *
 * Optimised zero page routine.
 *
 * On entry:
 *   r0 - dest address
 *
 * Requires:
 *   number of bytes per page (PAGE_SIZE) is a multiple of 512 (BIG_LOOPS), 128
 *   otherwise
 */

ENTRY(bzero_page)
	stmfd	sp!, {r4-r8, lr}
#ifdef BIG_LOOPS
	mov	r2, #(PAGE_SIZE >> 9)
#else
	mov	r2, #(PAGE_SIZE >> 7)
#endif
	mov	r3, #0
	mov	r4, #0
	mov	r5, #0
	mov	r6, #0
	mov	r7, #0
	mov	r8, #0
	mov	ip, #0
	mov	lr, #0

1:
	stmia	r0!, {r3-r8,ip,lr}
	stmia	r0!, {r3-r8,ip,lr}
	stmia	r0!, {r3-r8,ip,lr}
	stmia	r0!, {r3-r8,ip,lr}

#ifdef BIG_LOOPS
	/* There is little point making the loop any larger; unless we are
	   running with the cache off, the load/store overheads will
	   completely dominate this loop.  */
	stmia	r0!, {r3-r8,ip,lr}
	stmia	r0!, {r3-r8,ip,lr}
	stmia	r0!, {r3-r8,ip,lr}
	stmia	r0!, {r3-r8,ip,lr}

	stmia	r0!, {r3-r8,ip,lr}
	stmia	r0!, {r3-r8,ip,lr}
	stmia	r0!, {r3-r8,ip,lr}
	stmia	r0!, {r3-r8,ip,lr}

	stmia	r0!, {r3-r8,ip,lr}
	stmia	r0!, {r3-r8,ip,lr}
	stmia	r0!, {r3-r8,ip,lr}
	stmia	r0!, {r3-r8,ip,lr}

#endif

	subs	r2, r2, #1
	bne	1b

	ldmfd	sp!, {r4-r8, pc}
