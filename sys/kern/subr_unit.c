/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Poul-Henning Kamp
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
 *
 * $FreeBSD$
 *
 *
 * Unit number allocation functions.
 *
 * These functions implement a mixed run-length/bitmap management of unit
 * number spaces in a very memory efficient manner.
 *
 * Allocation policy is always lowest free number first.
 *
 * A return value of -1 signals that no more unit numbers are available.
 *
 * There is no cost associated with the range of unitnumbers, so unless
 * the resource really is finite, specify INT_MAX to new_unrhdr() and
 * forget about checking the return value.
 *
 * If a mutex is not provided when the unit number space is created, a
 * default global mutex is used.  The advantage to passing a mutex in, is
 * that the alloc_unrl() function can be called with the mutex already
 * held (it will not be released by alloc_unrl()).
 *
 * The allocation function alloc_unr{l}() never sleeps (but it may block on
 * the mutex of course).
 *
 * Freeing a unit number may require allocating memory, and can therefore
 * sleep so the free_unr() function does not come in a pre-locked variant.
 *
 * A userland test program is included.
 *
 * Memory usage is a very complex function of the exact allocation
 * pattern, but always very compact:
 *    * For the very typical case where a single unbroken run of unit
 *      numbers are allocated 44 bytes are used on i386.
 *    * For a unit number space of 1000 units and the random pattern
 *      in the usermode test program included, the worst case usage
 *	was 252 bytes on i386 for 500 allocated and 500 free units.
 *    * For a unit number space of 10000 units and the random pattern
 *      in the usermode test program included, the worst case usage
 *	was 798 bytes on i386 for 5000 allocated and 5000 free units.
 *    * The worst case is where every other unit number is allocated and
 *	the rest are free.  In that case 44 + N/4 bytes are used where
 *	N is the number of the highest unit allocated.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/_unrhdr.h>

#ifdef _KERNEL

#include <sys/bitstring.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>

/*
 * In theory it would be smarter to allocate the individual blocks
 * with the zone allocator, but at this time the expectation is that
 * there will typically not even be enough allocations to fill a single
 * page, so we stick with malloc for now.
 */
static MALLOC_DEFINE(M_UNIT, "Unitno", "Unit number allocation");

#define Malloc(foo) malloc(foo, M_UNIT, M_WAITOK | M_ZERO)
#define Free(foo) free(foo, M_UNIT)

static struct mtx unitmtx;

MTX_SYSINIT(unit, &unitmtx, "unit# allocation", MTX_DEF);

#ifdef UNR64_LOCKED
uint64_t
alloc_unr64(struct unrhdr64 *unr64)
{
	uint64_t item;

	mtx_lock(&unitmtx);
	item = unr64->counter++;
	mtx_unlock(&unitmtx);
	return (item);
}
#endif

#else /* ...USERLAND */

#include <bitstring.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KASSERT(cond, arg) \
	do { \
		if (!(cond)) { \
			printf arg; \
			abort(); \
		} \
	} while (0)

static int no_alloc;
#define Malloc(foo) _Malloc(foo, __LINE__)
static void *
_Malloc(size_t foo, int line)
{

	KASSERT(no_alloc == 0, ("malloc in wrong place() line %d", line));
	return (calloc(foo, 1));
}
#define Free(foo) free(foo)

struct unrhdr;


struct mtx {
	int	state;
} unitmtx;

static void
mtx_lock(struct mtx *mp)
{
	KASSERT(mp->state == 0, ("mutex already locked"));
	mp->state = 1;
}

static void
mtx_unlock(struct mtx *mp)
{
	KASSERT(mp->state == 1, ("mutex not locked"));
	mp->state = 0;
}

#define MA_OWNED	9

static void
mtx_assert(struct mtx *mp, int flag)
{
	if (flag == MA_OWNED) {
		KASSERT(mp->state == 1, ("mtx_assert(MA_OWNED) not true"));
	}
}

#define CTASSERT(foo)
#define WITNESS_WARN(flags, lock, fmt, ...)	(void)0

#endif /* USERLAND */

/*
 * This is our basic building block.
 *
 * It can be used in three different ways depending on the value of the ptr
 * element:
 *     If ptr is NULL, it represents a run of free items.
 *     If ptr points to the unrhdr it represents a run of allocated items.
 *     Otherwise it points to a bitstring of allocated items.
 *
 * For runs the len field is the length of the run.
 * For bitmaps the len field represents the number of allocated items.
 *
 * The bitmap is the same size as struct unr to optimize memory management.
 */
struct unr {
	TAILQ_ENTRY(unr)	list;
	u_int			len;
	void			*ptr;
};

struct unrb {
	bitstr_t		map[sizeof(struct unr) / sizeof(bitstr_t)];
};

CTASSERT((sizeof(struct unr) % sizeof(bitstr_t)) == 0);

/* Number of bits we can store in the bitmap */
#define NBITS (8 * sizeof(((struct unrb*)NULL)->map))

/* Is the unrb empty in at least the first len bits? */
static inline bool
ub_empty(struct unrb *ub, int len) {
	int first_set;

	bit_ffs(ub->map, len, &first_set);
	return (first_set == -1);
}

/* Is the unrb full?  That is, is the number of set elements equal to len? */
static inline bool
ub_full(struct unrb *ub, int len)
{
	int first_clear;

	bit_ffc(ub->map, len, &first_clear);
	return (first_clear == -1);
}


#if defined(DIAGNOSTIC) || !defined(_KERNEL)
/*
 * Consistency check function.
 *
 * Checks the internal consistency as well as we can.
 *
 * Called at all boundaries of this API.
 */
static void
check_unrhdr(struct unrhdr *uh, int line)
{
	struct unr *up;
	struct unrb *ub;
	int w;
	u_int y, z;

	y = uh->first;
	z = 0;
	TAILQ_FOREACH(up, &uh->head, list) {
		z++;
		if (up->ptr != uh && up->ptr != NULL) {
			ub = up->ptr;
			KASSERT (up->len <= NBITS,
			    ("UNR inconsistency: len %u max %zd (line %d)\n",
			    up->len, NBITS, line));
			z++;
			w = 0;
			bit_count(ub->map, 0, up->len, &w);
			y += w;
		} else if (up->ptr != NULL)
			y += up->len;
	}
	KASSERT (y == uh->busy,
	    ("UNR inconsistency: items %u found %u (line %d)\n",
	    uh->busy, y, line));
	KASSERT (z == uh->alloc,
	    ("UNR inconsistency: chunks %u found %u (line %d)\n",
	    uh->alloc, z, line));
}

#else

static __inline void
check_unrhdr(struct unrhdr *uh __unused, int line __unused)
{

}

#endif


/*
 * Userland memory management.  Just use calloc and keep track of how
 * many elements we have allocated for check_unrhdr().
 */

static __inline void *
new_unr(struct unrhdr *uh, void **p1, void **p2)
{
	void *p;

	uh->alloc++;
	KASSERT(*p1 != NULL || *p2 != NULL, ("Out of cached memory"));
	if (*p1 != NULL) {
		p = *p1;
		*p1 = NULL;
		return (p);
	} else {
		p = *p2;
		*p2 = NULL;
		return (p);
	}
}

static __inline void
delete_unr(struct unrhdr *uh, void *ptr)
{
	struct unr *up;

	uh->alloc--;
	up = ptr;
	TAILQ_INSERT_TAIL(&uh->ppfree, up, list);
}

void
clean_unrhdrl(struct unrhdr *uh)
{
	struct unr *up;

	mtx_assert(uh->mtx, MA_OWNED);
	while ((up = TAILQ_FIRST(&uh->ppfree)) != NULL) {
		TAILQ_REMOVE(&uh->ppfree, up, list);
		mtx_unlock(uh->mtx);
		Free(up);
		mtx_lock(uh->mtx);
	}

}

void
clean_unrhdr(struct unrhdr *uh)
{

	mtx_lock(uh->mtx);
	clean_unrhdrl(uh);
	mtx_unlock(uh->mtx);
}

void
init_unrhdr(struct unrhdr *uh, int low, int high, struct mtx *mutex)
{

	KASSERT(low >= 0 && low <= high,
	    ("UNR: use error: new_unrhdr(%d, %d)", low, high));
	if (mutex != NULL)
		uh->mtx = mutex;
	else
		uh->mtx = &unitmtx;
	TAILQ_INIT(&uh->head);
	TAILQ_INIT(&uh->ppfree);
	uh->low = low;
	uh->high = high;
	uh->first = 0;
	uh->last = 1 + (high - low);
	check_unrhdr(uh, __LINE__);
}

/*
 * Allocate a new unrheader set.
 *
 * Highest and lowest valid values given as parameters.
 */

struct unrhdr *
new_unrhdr(int low, int high, struct mtx *mutex)
{
	struct unrhdr *uh;

	uh = Malloc(sizeof *uh);
	init_unrhdr(uh, low, high, mutex);
	return (uh);
}

void
delete_unrhdr(struct unrhdr *uh)
{

	check_unrhdr(uh, __LINE__);
	KASSERT(uh->busy == 0, ("unrhdr has %u allocations", uh->busy));
	KASSERT(uh->alloc == 0, ("UNR memory leak in delete_unrhdr"));
	KASSERT(TAILQ_FIRST(&uh->ppfree) == NULL,
	    ("unrhdr has postponed item for free"));
	Free(uh);
}

void
clear_unrhdr(struct unrhdr *uh)
{
	struct unr *up, *uq;

	KASSERT(TAILQ_EMPTY(&uh->ppfree),
	    ("unrhdr has postponed item for free"));
	TAILQ_FOREACH_SAFE(up, &uh->head, list, uq) {
		if (up->ptr != uh) {
			Free(up->ptr);
		}
		Free(up);
	}
	uh->busy = 0;
	uh->alloc = 0;
	init_unrhdr(uh, uh->low, uh->high, uh->mtx);

	check_unrhdr(uh, __LINE__);
}

static __inline int
is_bitmap(struct unrhdr *uh, struct unr *up)
{
	return (up->ptr != uh && up->ptr != NULL);
}

/*
 * Look for sequence of items which can be combined into a bitmap, if
 * multiple are present, take the one which saves most memory.
 *
 * Return (1) if a sequence was found to indicate that another call
 * might be able to do more.  Return (0) if we found no suitable sequence.
 *
 * NB: called from alloc_unr(), no new memory allocation allowed.
 */
static int
optimize_unr(struct unrhdr *uh)
{
	struct unr *up, *uf, *us;
	struct unrb *ub, *ubf;
	u_int a, l, ba;

	/*
	 * Look for the run of items (if any) which when collapsed into
	 * a bitmap would save most memory.
	 */
	us = NULL;
	ba = 0;
	TAILQ_FOREACH(uf, &uh->head, list) {
		if (uf->len >= NBITS)
			continue;
		a = 1;
		if (is_bitmap(uh, uf))
			a++;
		l = uf->len;
		up = uf;
		while (1) {
			up = TAILQ_NEXT(up, list);
			if (up == NULL)
				break;
			if ((up->len + l) > NBITS)
				break;
			a++;
			if (is_bitmap(uh, up))
				a++;
			l += up->len;
		}
		if (a > ba) {
			ba = a;
			us = uf;
		}
	}
	if (ba < 3)
		return (0);

	/*
	 * If the first element is not a bitmap, make it one.
	 * Trying to do so without allocating more memory complicates things
	 * a bit
	 */
	if (!is_bitmap(uh, us)) {
		uf = TAILQ_NEXT(us, list);
		TAILQ_REMOVE(&uh->head, us, list);
		a = us->len;
		l = us->ptr == uh ? 1 : 0;
		ub = (void *)us;
		bit_nclear(ub->map, 0, NBITS - 1);
		if (l)
			bit_nset(ub->map, 0, a);
		if (!is_bitmap(uh, uf)) {
			if (uf->ptr == NULL)
				bit_nclear(ub->map, a, a + uf->len - 1);
			else
				bit_nset(ub->map, a, a + uf->len - 1);
			uf->ptr = ub;
			uf->len += a;
			us = uf;
		} else {
			ubf = uf->ptr;
			for (l = 0; l < uf->len; l++, a++) {
				if (bit_test(ubf->map, l))
					bit_set(ub->map, a);
				else
					bit_clear(ub->map, a);
			}
			uf->len = a;
			delete_unr(uh, uf->ptr);
			uf->ptr = ub;
			us = uf;
		}
	}
	ub = us->ptr;
	while (1) {
		uf = TAILQ_NEXT(us, list);
		if (uf == NULL)
			return (1);
		if (uf->len + us->len > NBITS)
			return (1);
		if (uf->ptr == NULL) {
			bit_nclear(ub->map, us->len, us->len + uf->len - 1);
			us->len += uf->len;
			TAILQ_REMOVE(&uh->head, uf, list);
			delete_unr(uh, uf);
		} else if (uf->ptr == uh) {
			bit_nset(ub->map, us->len, us->len + uf->len - 1);
			us->len += uf->len;
			TAILQ_REMOVE(&uh->head, uf, list);
			delete_unr(uh, uf);
		} else {
			ubf = uf->ptr;
			for (l = 0; l < uf->len; l++, us->len++) {
				if (bit_test(ubf->map, l))
					bit_set(ub->map, us->len);
				else
					bit_clear(ub->map, us->len);
			}
			TAILQ_REMOVE(&uh->head, uf, list);
			delete_unr(uh, ubf);
			delete_unr(uh, uf);
		}
	}
}

/*
 * See if a given unr should be collapsed with a neighbor.
 *
 * NB: called from alloc_unr(), no new memory allocation allowed.
 */
static void
collapse_unr(struct unrhdr *uh, struct unr *up)
{
	struct unr *upp;
	struct unrb *ub;

	/* If bitmap is all set or clear, change it to runlength */
	if (is_bitmap(uh, up)) {
		ub = up->ptr;
		if (ub_full(ub, up->len)) {
			delete_unr(uh, up->ptr);
			up->ptr = uh;
		} else if (ub_empty(ub, up->len)) {
			delete_unr(uh, up->ptr);
			up->ptr = NULL;
		}
	}

	/* If nothing left in runlength, delete it */
	if (up->len == 0) {
		upp = TAILQ_PREV(up, unrhd, list);
		if (upp == NULL)
			upp = TAILQ_NEXT(up, list);
		TAILQ_REMOVE(&uh->head, up, list);
		delete_unr(uh, up);
		up = upp;
	}

	/* If we have "hot-spot" still, merge with neighbor if possible */
	if (up != NULL) {
		upp = TAILQ_PREV(up, unrhd, list);
		if (upp != NULL && up->ptr == upp->ptr) {
			up->len += upp->len;
			TAILQ_REMOVE(&uh->head, upp, list);
			delete_unr(uh, upp);
			}
		upp = TAILQ_NEXT(up, list);
		if (upp != NULL && up->ptr == upp->ptr) {
			up->len += upp->len;
			TAILQ_REMOVE(&uh->head, upp, list);
			delete_unr(uh, upp);
		}
	}

	/* Merge into ->first if possible */
	upp = TAILQ_FIRST(&uh->head);
	if (upp != NULL && upp->ptr == uh) {
		uh->first += upp->len;
		TAILQ_REMOVE(&uh->head, upp, list);
		delete_unr(uh, upp);
		if (up == upp)
			up = NULL;
	}

	/* Merge into ->last if possible */
	upp = TAILQ_LAST(&uh->head, unrhd);
	if (upp != NULL && upp->ptr == NULL) {
		uh->last += upp->len;
		TAILQ_REMOVE(&uh->head, upp, list);
		delete_unr(uh, upp);
		if (up == upp)
			up = NULL;
	}

	/* Try to make bitmaps */
	while (optimize_unr(uh))
		continue;
}

/*
 * Allocate a free unr.
 */
int
alloc_unrl(struct unrhdr *uh)
{
	struct unr *up;
	struct unrb *ub;
	u_int x;
	int y;

	mtx_assert(uh->mtx, MA_OWNED);
	check_unrhdr(uh, __LINE__);
	x = uh->low + uh->first;

	up = TAILQ_FIRST(&uh->head);

	/*
	 * If we have an ideal split, just adjust the first+last
	 */
	if (up == NULL && uh->last > 0) {
		uh->first++;
		uh->last--;
		uh->busy++;
		return (x);
	}

	/*
	 * We can always allocate from the first list element, so if we have
	 * nothing on the list, we must have run out of unit numbers.
	 */
	if (up == NULL)
		return (-1);

	KASSERT(up->ptr != uh, ("UNR first element is allocated"));

	if (up->ptr == NULL) {	/* free run */
		uh->first++;
		up->len--;
	} else {		/* bitmap */
		ub = up->ptr;
		bit_ffc(ub->map, up->len, &y);
		KASSERT(y != -1, ("UNR corruption: No clear bit in bitmap."));
		bit_set(ub->map, y);
		x += y;
	}
	uh->busy++;
	collapse_unr(uh, up);
	return (x);
}

int
alloc_unr(struct unrhdr *uh)
{
	int i;

	mtx_lock(uh->mtx);
	i = alloc_unrl(uh);
	clean_unrhdrl(uh);
	mtx_unlock(uh->mtx);
	return (i);
}

static int
alloc_unr_specificl(struct unrhdr *uh, u_int item, void **p1, void **p2)
{
	struct unr *up, *upn;
	struct unrb *ub;
	u_int i, last, tl;

	mtx_assert(uh->mtx, MA_OWNED);

	if (item < uh->low + uh->first || item > uh->high)
		return (-1);

	up = TAILQ_FIRST(&uh->head);
	/* Ideal split. */
	if (up == NULL && item - uh->low == uh->first) {
		uh->first++;
		uh->last--;
		uh->busy++;
		check_unrhdr(uh, __LINE__);
		return (item);
	}

	i = item - uh->low - uh->first;

	if (up == NULL) {
		up = new_unr(uh, p1, p2);
		up->ptr = NULL;
		up->len = i;
		TAILQ_INSERT_TAIL(&uh->head, up, list);
		up = new_unr(uh, p1, p2);
		up->ptr = uh;
		up->len = 1;
		TAILQ_INSERT_TAIL(&uh->head, up, list);
		uh->last = uh->high - uh->low - i;
		uh->busy++;
		check_unrhdr(uh, __LINE__);
		return (item);
	} else {
		/* Find the item which contains the unit we want to allocate. */
		TAILQ_FOREACH(up, &uh->head, list) {
			if (up->len > i)
				break;
			i -= up->len;
		}
	}

	if (up == NULL) {
		if (i > 0) {
			up = new_unr(uh, p1, p2);
			up->ptr = NULL;
			up->len = i;
			TAILQ_INSERT_TAIL(&uh->head, up, list);
		}
		up = new_unr(uh, p1, p2);
		up->ptr = uh;
		up->len = 1;
		TAILQ_INSERT_TAIL(&uh->head, up, list);
		goto done;
	}

	if (is_bitmap(uh, up)) {
		ub = up->ptr;
		if (bit_test(ub->map, i) == 0) {
			bit_set(ub->map, i);
			goto done;
		} else
			return (-1);
	} else if (up->ptr == uh)
		return (-1);

	KASSERT(up->ptr == NULL,
	    ("alloc_unr_specificl: up->ptr != NULL (up=%p)", up));

	/* Split off the tail end, if any. */
	tl = up->len - (1 + i);
	if (tl > 0) {
		upn = new_unr(uh, p1, p2);
		upn->ptr = NULL;
		upn->len = tl;
		TAILQ_INSERT_AFTER(&uh->head, up, upn, list);
	}

	/* Split off head end, if any */
	if (i > 0) {
		upn = new_unr(uh, p1, p2);
		upn->len = i;
		upn->ptr = NULL;
		TAILQ_INSERT_BEFORE(up, upn, list);
	}
	up->len = 1;
	up->ptr = uh;

done:
	last = uh->high - uh->low - (item - uh->low);
	if (uh->last > last)
		uh->last = last;
	uh->busy++;
	collapse_unr(uh, up);
	check_unrhdr(uh, __LINE__);
	return (item);
}

int
alloc_unr_specific(struct unrhdr *uh, u_int item)
{
	void *p1, *p2;
	int i;

	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL, "alloc_unr_specific");

	p1 = Malloc(sizeof(struct unr));
	p2 = Malloc(sizeof(struct unr));

	mtx_lock(uh->mtx);
	i = alloc_unr_specificl(uh, item, &p1, &p2);
	mtx_unlock(uh->mtx);

	if (p1 != NULL)
		Free(p1);
	if (p2 != NULL)
		Free(p2);

	return (i);
}

/*
 * Free a unr.
 *
 * If we can save unrs by using a bitmap, do so.
 */
static void
free_unrl(struct unrhdr *uh, u_int item, void **p1, void **p2)
{
	struct unr *up, *upp, *upn;
	struct unrb *ub;
	u_int pl;

	KASSERT(item >= uh->low && item <= uh->high,
	    ("UNR: free_unr(%u) out of range [%u...%u]",
	     item, uh->low, uh->high));
	check_unrhdr(uh, __LINE__);
	item -= uh->low;
	upp = TAILQ_FIRST(&uh->head);
	/*
	 * Freeing in the ideal split case
	 */
	if (item + 1 == uh->first && upp == NULL) {
		uh->last++;
		uh->first--;
		uh->busy--;
		check_unrhdr(uh, __LINE__);
		return;
	}
	/*
 	 * Freeing in the ->first section.  Create a run starting at the
	 * freed item.  The code below will subdivide it.
	 */
	if (item < uh->first) {
		up = new_unr(uh, p1, p2);
		up->ptr = uh;
		up->len = uh->first - item;
		TAILQ_INSERT_HEAD(&uh->head, up, list);
		uh->first -= up->len;
	}

	item -= uh->first;

	/* Find the item which contains the unit we want to free */
	TAILQ_FOREACH(up, &uh->head, list) {
		if (up->len > item)
			break;
		item -= up->len;
	}

	/* Handle bitmap items */
	if (is_bitmap(uh, up)) {
		ub = up->ptr;

		KASSERT(bit_test(ub->map, item) != 0,
		    ("UNR: Freeing free item %d (bitmap)\n", item));
		bit_clear(ub->map, item);
		uh->busy--;
		collapse_unr(uh, up);
		return;
	}

	KASSERT(up->ptr == uh, ("UNR Freeing free item %d (run))\n", item));

	/* Just this one left, reap it */
	if (up->len == 1) {
		up->ptr = NULL;
		uh->busy--;
		collapse_unr(uh, up);
		return;
	}

	/* Check if we can shift the item into the previous 'free' run */
	upp = TAILQ_PREV(up, unrhd, list);
	if (item == 0 && upp != NULL && upp->ptr == NULL) {
		upp->len++;
		up->len--;
		uh->busy--;
		collapse_unr(uh, up);
		return;
	}

	/* Check if we can shift the item to the next 'free' run */
	upn = TAILQ_NEXT(up, list);
	if (item == up->len - 1 && upn != NULL && upn->ptr == NULL) {
		upn->len++;
		up->len--;
		uh->busy--;
		collapse_unr(uh, up);
		return;
	}

	/* Split off the tail end, if any. */
	pl = up->len - (1 + item);
	if (pl > 0) {
		upp = new_unr(uh, p1, p2);
		upp->ptr = uh;
		upp->len = pl;
		TAILQ_INSERT_AFTER(&uh->head, up, upp, list);
	}

	/* Split off head end, if any */
	if (item > 0) {
		upp = new_unr(uh, p1, p2);
		upp->len = item;
		upp->ptr = uh;
		TAILQ_INSERT_BEFORE(up, upp, list);
	}
	up->len = 1;
	up->ptr = NULL;
	uh->busy--;
	collapse_unr(uh, up);
}

void
free_unr(struct unrhdr *uh, u_int item)
{
	void *p1, *p2;

	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL, "free_unr");
	p1 = Malloc(sizeof(struct unr));
	p2 = Malloc(sizeof(struct unr));
	mtx_lock(uh->mtx);
	free_unrl(uh, item, &p1, &p2);
	clean_unrhdrl(uh);
	mtx_unlock(uh->mtx);
	if (p1 != NULL)
		Free(p1);
	if (p2 != NULL)
		Free(p2);
}

#ifndef _KERNEL	/* USERLAND test driver */

/*
 * Simple stochastic test driver for the above functions.  The code resides
 * here so that it can access static functions and structures.
 */

static bool verbose;
#define VPRINTF(...)	{if (verbose) printf(__VA_ARGS__);}

static void
print_unr(struct unrhdr *uh, struct unr *up)
{
	u_int x;
	struct unrb *ub;

	printf("  %p len = %5u ", up, up->len);
	if (up->ptr == NULL)
		printf("free\n");
	else if (up->ptr == uh)
		printf("alloc\n");
	else {
		ub = up->ptr;
		printf("bitmap [");
		for (x = 0; x < up->len; x++) {
			if (bit_test(ub->map, x))
				printf("#");
			else
				printf(" ");
		}
		printf("]\n");
	}
}

static void
print_unrhdr(struct unrhdr *uh)
{
	struct unr *up;
	u_int x;

	printf(
	    "%p low = %u high = %u first = %u last = %u busy %u chunks = %u\n",
	    uh, uh->low, uh->high, uh->first, uh->last, uh->busy, uh->alloc);
	x = uh->low + uh->first;
	TAILQ_FOREACH(up, &uh->head, list) {
		printf("  from = %5u", x);
		print_unr(uh, up);
		if (up->ptr == NULL || up->ptr == uh)
			x += up->len;
		else
			x += NBITS;
	}
}

static void
test_alloc_unr(struct unrhdr *uh, u_int i, char a[])
{
	int j;

	if (a[i]) {
		VPRINTF("F %u\n", i);
		free_unr(uh, i);
		a[i] = 0;
	} else {
		no_alloc = 1;
		j = alloc_unr(uh);
		if (j != -1) {
			a[j] = 1;
			VPRINTF("A %d\n", j);
		}
		no_alloc = 0;
	}
}

static void
test_alloc_unr_specific(struct unrhdr *uh, u_int i, char a[])
{
	int j;

	j = alloc_unr_specific(uh, i);
	if (j == -1) {
		VPRINTF("F %u\n", i);
		a[i] = 0;
		free_unr(uh, i);
	} else {
		a[i] = 1;
		VPRINTF("A %d\n", j);
	}
}

static void
usage(char** argv)
{
	printf("%s [-h] [-r REPETITIONS] [-v]\n", argv[0]);
}

int
main(int argc, char **argv)
{
	struct unrhdr *uh;
	char *a;
	long count = 10000;	/* Number of unrs to test */
	long reps = 1, m;
	int ch;
	u_int i, j;

	verbose = false;

	while ((ch = getopt(argc, argv, "hr:v")) != -1) {
		switch (ch) {
		case 'r':
			errno = 0;
			reps = strtol(optarg, NULL, 0);
			if (errno == ERANGE || errno == EINVAL) {
				usage(argv);
				exit(2);
			}

			break;
		case 'v':
			verbose = true;
			break;
		case 'h':
		default:
			usage(argv);
			exit(2);
		}


	}

	setbuf(stdout, NULL);
	uh = new_unrhdr(0, count - 1, NULL);
	print_unrhdr(uh);

	a = calloc(count, sizeof(char));
	if (a == NULL)
		err(1, "calloc failed");
	srandomdev();

	printf("sizeof(struct unr) %zu\n", sizeof(struct unr));
	printf("sizeof(struct unrb) %zu\n", sizeof(struct unrb));
	printf("sizeof(struct unrhdr) %zu\n", sizeof(struct unrhdr));
	printf("NBITS %lu\n", (unsigned long)NBITS);
	for (m = 0; m < count * reps; m++) {
		j = random();
		i = (j >> 1) % count;
#if 0
		if (a[i] && (j & 1))
			continue;
#endif
		if ((random() & 1) != 0)
			test_alloc_unr(uh, i, a);
		else
			test_alloc_unr_specific(uh, i, a);

		if (verbose)
			print_unrhdr(uh);
		check_unrhdr(uh, __LINE__);
	}
	for (i = 0; i < (u_int)count; i++) {
		if (a[i]) {
			if (verbose) {
				printf("C %u\n", i);
				print_unrhdr(uh);
			}
			free_unr(uh, i);
		}
	}
	print_unrhdr(uh);
	delete_unrhdr(uh);
	free(a);
	return (0);
}
#endif
