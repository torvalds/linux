/*	$OpenBSD: qsort.c,v 1.18 2017/05/30 14:54:09 millert Exp $ */
/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
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
#include <stdlib.h>

static __inline char	*med3(char *, char *, char *, int (*)(const void *, const void *));
static __inline void	 swapfunc(char *, char *, size_t, int);

#define min(a, b)	(a) < (b) ? a : b

/*
 * Qsort routine from Bentley & McIlroy's "Engineering a Sort Function".
 *
 * This version differs from Bentley & McIlroy in the following ways:
 *   1. The partition value is swapped into a[0] instead of being
 *	stored out of line.
 *
 *   2. The swap function can swap 32-bit aligned elements on 64-bit
 *	platforms instead of swapping them as byte-aligned.
 *
 *   3. It uses David Musser's introsort algorithm to fall back to
 *	heapsort(3) when the recursion depth reaches 2*lg(n + 1).
 *	This avoids quicksort's quadratic behavior for pathological
 *	input without appreciably changing the average run time.
 *
 *   4. Tail recursion is eliminated when sorting the larger of two
 *	subpartitions to save stack space.
 */
#define SWAPTYPE_BYTEV	1
#define SWAPTYPE_INTV	2
#define SWAPTYPE_LONGV	3
#define SWAPTYPE_INT	4
#define SWAPTYPE_LONG	5

#define TYPE_ALIGNED(TYPE, a, es)			\
	(((char *)a - (char *)0) % sizeof(TYPE) == 0 && es % sizeof(TYPE) == 0)

#define swapcode(TYPE, parmi, parmj, n) { 		\
	size_t i = (n) / sizeof (TYPE); 		\
	TYPE *pi = (TYPE *) (parmi); 			\
	TYPE *pj = (TYPE *) (parmj); 			\
	do { 						\
		TYPE	t = *pi;			\
		*pi++ = *pj;				\
		*pj++ = t;				\
        } while (--i > 0);				\
}

static __inline void
swapfunc(char *a, char *b, size_t n, int swaptype)
{
	switch (swaptype) {
	case SWAPTYPE_INT:
	case SWAPTYPE_INTV:
		swapcode(int, a, b, n);
		break;
	case SWAPTYPE_LONG:
	case SWAPTYPE_LONGV:
		swapcode(long, a, b, n);
		break;
	default:
		swapcode(char, a, b, n);
		break;
	}
}

#define swap(a, b)	do {				\
	switch (swaptype) {				\
	case SWAPTYPE_INT: {				\
		int t = *(int *)(a);			\
		*(int *)(a) = *(int *)(b);		\
		*(int *)(b) = t;			\
		break;					\
	    }						\
	case SWAPTYPE_LONG: {				\
		long t = *(long *)(a);			\
		*(long *)(a) = *(long *)(b);		\
		*(long *)(b) = t;			\
		break;					\
	    }						\
	default:					\
		swapfunc(a, b, es, swaptype);		\
	}						\
} while (0)

#define vecswap(a, b, n) 	if ((n) > 0) swapfunc(a, b, n, swaptype)

static __inline char *
med3(char *a, char *b, char *c, int (*cmp)(const void *, const void *))
{
	return cmp(a, b) < 0 ?
	       (cmp(b, c) < 0 ? b : (cmp(a, c) < 0 ? c : a ))
              :(cmp(b, c) > 0 ? b : (cmp(a, c) < 0 ? a : c ));
}

static void
introsort(char *a, size_t n, size_t es, size_t maxdepth, int swaptype,
    int (*cmp)(const void *, const void *))
{
	char *pa, *pb, *pc, *pd, *pl, *pm, *pn;
	int cmp_result;
	size_t r, s;

loop:	if (n < 7) {
		for (pm = a + es; pm < a + n * es; pm += es)
			for (pl = pm; pl > a && cmp(pl - es, pl) > 0;
			     pl -= es)
				swap(pl, pl - es);
		return;
	}
	if (maxdepth == 0) {
		if (heapsort(a, n, es, cmp) == 0)
			return;
	}
	maxdepth--;
	pm = a + (n / 2) * es;
	if (n > 7) {
		pl = a;
		pn = a + (n - 1) * es;
		if (n > 40) {
			s = (n / 8) * es;
			pl = med3(pl, pl + s, pl + 2 * s, cmp);
			pm = med3(pm - s, pm, pm + s, cmp);
			pn = med3(pn - 2 * s, pn - s, pn, cmp);
		}
		pm = med3(pl, pm, pn, cmp);
	}
	swap(a, pm);
	pa = pb = a + es;
	pc = pd = a + (n - 1) * es;
	for (;;) {
		while (pb <= pc && (cmp_result = cmp(pb, a)) <= 0) {
			if (cmp_result == 0) {
				swap(pa, pb);
				pa += es;
			}
			pb += es;
		}
		while (pb <= pc && (cmp_result = cmp(pc, a)) >= 0) {
			if (cmp_result == 0) {
				swap(pc, pd);
				pd -= es;
			}
			pc -= es;
		}
		if (pb > pc)
			break;
		swap(pb, pc);
		pb += es;
		pc -= es;
	}

	pn = a + n * es;
	r = min(pa - a, pb - pa);
	vecswap(a, pb - r, r);
	r = min(pd - pc, pn - pd - es);
	vecswap(pb, pn - r, r);
	/*
	 * To save stack space we sort the smaller side of the partition first
	 * using recursion and eliminate tail recursion for the larger side.
	 */
	r = pb - pa;
	s = pd - pc;
	if (r < s) {
		/* Recurse for 1st side, iterate for 2nd side. */
		if (s > es) {
			if (r > es) {
				introsort(a, r / es, es, maxdepth,
				    swaptype, cmp);
			}
			a = pn - s;
			n = s / es;
			goto loop;
		}
	} else {
		/* Recurse for 2nd side, iterate for 1st side. */
		if (r > es) {
			if (s > es) {
				introsort(pn - s, s / es, es, maxdepth,
				    swaptype, cmp);
			}
			n = r / es;
			goto loop;
		}
	}
}

void
qsort(void *a, size_t n, size_t es, int (*cmp)(const void *, const void *))
{
	size_t i, maxdepth = 0;
	int swaptype;

	/* Approximate 2*ceil(lg(n + 1)) */
	for (i = n; i > 0; i >>= 1)
		maxdepth++;
	maxdepth *= 2;

	if (TYPE_ALIGNED(long, a, es))
		swaptype = es == sizeof(long) ? SWAPTYPE_LONG : SWAPTYPE_LONGV;
	else if (sizeof(int) != sizeof(long) && TYPE_ALIGNED(int, a, es))
		swaptype = es == sizeof(int) ? SWAPTYPE_INT : SWAPTYPE_INTV;
	else
		swaptype = SWAPTYPE_BYTEV;

	introsort(a, n, es, maxdepth, swaptype, cmp);

}

DEF_STRONG(qsort);
