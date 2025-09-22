/*	$OpenBSD: page.h,v 1.6 2003/06/02 20:18:34 millert Exp $	*/

/*-
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Margo Seltzer.
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
 *	@(#)page.h	8.2 (Berkeley) 5/31/94
 */

/*
 * Definitions for hashing page file format.
 */

/*
 * routines dealing with a data page
 *
 * page format:
 *	+------------------------------+
 * p	| n | keyoff | datoff | keyoff |
 * 	+------------+--------+--------+
 *	| datoff | free  |  ptr  | --> |
 *	+--------+---------------------+
 *	|	 F R E E A R E A       |
 *	+--------------+---------------+
 *	|  <---- - - - | data	       |
 *	+--------+-----+----+----------+
 *	|  key   | data     | key      |
 *	+--------+----------+----------+
 *
 * Pointer to the free space is always:  p[p[0] + 2]
 * Amount of free space on the page is:  p[p[0] + 1]
 */

/*
 * How many bytes required for this pair?
 *	2 shorts in the table at the top of the page + room for the
 *	key and room for the data
 *
 * We prohibit entering a pair on a page unless there is also room to append
 * an overflow page. The reason for this it that you can get in a situation
 * where a single key/data pair fits on a page, but you can't append an
 * overflow page and later you'd have to split the key/data and handle like
 * a big pair.
 * You might as well do this up front.
 */

#define	PAIRSIZE(K,D)	(2*sizeof(u_int16_t) + (K)->size + (D)->size)
#define BIGOVERHEAD	(4*sizeof(u_int16_t))
#define KEYSIZE(K)	(4*sizeof(u_int16_t) + (K)->size);
#define OVFLSIZE	(2*sizeof(u_int16_t))
#define FREESPACE(P)	((P)[(P)[0]+1])
#define	OFFSET(P)	((P)[(P)[0]+2])
#define PAIRFITS(P,K,D) \
	(((P)[2] >= REAL_KEY) && \
	    (PAIRSIZE((K),(D)) + OVFLSIZE) <= FREESPACE((P)))
#define PAGE_META(N)	(((N)+3) * sizeof(u_int16_t))

typedef struct {
	BUFHEAD *newp;
	BUFHEAD *oldp;
	BUFHEAD *nextp;
	u_int16_t next_addr;
}       SPLIT_RETURN;
