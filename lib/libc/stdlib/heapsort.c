/*	$OpenBSD: heapsort.c,v 1.11 2017/05/20 12:48:56 millert Exp $ */
/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ronnie Kon at Mindcraft Inc., Kevin Lew and Elmer Yglesias.
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
 */

#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>

/*
 * Swap two areas of size number of bytes.  Although qsort(3) permits random
 * blocks of memory to be sorted, sorting pointers is almost certainly the
 * common case (and, were it not, could easily be made so).  Regardless, it
 * isn't worth optimizing; the SWAP's get sped up by the cache, and pointer
 * arithmetic gets lost in the time required for comparison function calls.
 */
#define	SWAP(a, b, count, size, tmp) { \
	count = size; \
	do { \
		tmp = *a; \
		*a++ = *b; \
		*b++ = tmp; \
	} while (--count); \
}

/* Copy one block of size size to another. */
#define COPY(a, b, count, size, tmp1, tmp2) { \
	count = size; \
	tmp1 = a; \
	tmp2 = b; \
	do { \
		*tmp1++ = *tmp2++; \
	} while (--count); \
}

/*
 * Build the list into a heap, where a heap is defined such that for
 * the records K1 ... KN, Kj/2 >= Kj for 1 <= j/2 <= j <= N.
 *
 * There are two cases.  If j == nmemb, select largest of Ki and Kj.  If
 * j < nmemb, select largest of Ki, Kj and Kj+1.
 */
#define CREATE(initval, nmemb, par_i, child_i, par, child, size, count, tmp) { \
	for (par_i = initval; (child_i = par_i * 2) <= nmemb; \
	    par_i = child_i) { \
		child = base + child_i * size; \
		if (child_i < nmemb && compar(child, child + size) < 0) { \
			child += size; \
			++child_i; \
		} \
		par = base + par_i * size; \
		if (compar(child, par) <= 0) \
			break; \
		SWAP(par, child, count, size, tmp); \
	} \
}

/*
 * Select the top of the heap and 'heapify'.  Since by far the most expensive
 * action is the call to the compar function, a considerable optimization
 * in the average case can be achieved due to the fact that k, the displaced
 * element, is usually quite small, so it would be preferable to first
 * heapify, always maintaining the invariant that the larger child is copied
 * over its parent's record.
 *
 * Then, starting from the *bottom* of the heap, finding k's correct place,
 * again maintaining the invariant.  As a result of the invariant no element
 * is 'lost' when k is assigned its correct place in the heap.
 *
 * The time savings from this optimization are on the order of 15-20% for the
 * average case. See Knuth, Vol. 3, page 158, problem 18.
 *
 * XXX Don't break the #define SELECT line, below.  Reiser cpp gets upset.
 */
#define SELECT(par_i, child_i, nmemb, par, child, size, k, count, tmp1, tmp2) { \
	for (par_i = 1; (child_i = par_i * 2) <= nmemb; par_i = child_i) { \
		child = base + child_i * size; \
		if (child_i < nmemb && compar(child, child + size) < 0) { \
			child += size; \
			++child_i; \
		} \
		par = base + par_i * size; \
		COPY(par, child, count, size, tmp1, tmp2); \
	} \
	for (;;) { \
		child_i = par_i; \
		par_i = child_i / 2; \
		child = base + child_i * size; \
		par = base + par_i * size; \
		if (child_i == 1 || compar(k, par) < 0) { \
			COPY(child, k, count, size, tmp1, tmp2); \
			break; \
		} \
		COPY(child, par, count, size, tmp1, tmp2); \
	} \
}

/*
 * Heapsort -- Knuth, Vol. 3, page 145.  Runs in O (N lg N), both average
 * and worst.  While heapsort is faster than the worst case of quicksort,
 * the BSD quicksort does median selection so that the chance of finding
 * a data set that will trigger the worst case is nonexistent.  Heapsort's
 * only advantage over quicksort is that it requires little additional memory.
 */
int
heapsort(void *vbase, size_t nmemb, size_t size,
    int (*compar)(const void *, const void *))
{
	size_t cnt, i, j, l;
	char tmp, *tmp1, *tmp2;
	char *base, *k, *p, *t;

	if (nmemb <= 1)
		return (0);

	if (!size) {
		errno = EINVAL;
		return (-1);
	}

	if ((k = malloc(size)) == NULL)
		return (-1);

	/*
	 * Items are numbered from 1 to nmemb, so offset from size bytes
	 * below the starting address.
	 */
	base = (char *)vbase - size;

	for (l = nmemb / 2 + 1; --l;)
		CREATE(l, nmemb, i, j, t, p, size, cnt, tmp);

	/*
	 * For each element of the heap, save the largest element into its
	 * final slot, save the displaced element (k), then recreate the
	 * heap.
	 */
	while (nmemb > 1) {
		COPY(k, base + nmemb * size, cnt, size, tmp1, tmp2);
		COPY(base + nmemb * size, base + size, cnt, size, tmp1, tmp2);
		--nmemb;
		SELECT(i, j, nmemb, t, p, size, k, cnt, tmp1, tmp2);
	}
	free(k);
	return (0);
}
DEF_WEAK(heapsort);
