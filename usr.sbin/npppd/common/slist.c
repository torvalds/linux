/*	$OpenBSD: slist.c,v 1.8 2021/03/29 03:54:39 yasuoka Exp $ */
/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
/**@file
 * provide list accesses against any pointer
 */
/*
 *	void **list;
 *	list_size;	// allocated size for the list
 *	last_idx;	// The last index
 *	first_idx;	// The first index
 *
 * - first_idx == last_idx means empty.
 * - 0 <= (fist_idx and last_idx) <= (list_size - 1)
 * - Allocated size is (last_idx - first_idx) % list_size.
 *   To make the code for checking empty and full simple, we use only
 *   list_size-1 items instead of using the full size.
 * - XXX Wnen itr_curr is removed...
 */
#include <sys/types.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "slist.h"

#define	GROW_SIZE	256
#define	PTR_SIZE	(sizeof(intptr_t))

#ifdef	SLIST_DEBUG
#include <stdio.h>
#define	SLIST_ASSERT(cond)			\
	if (!(cond)) {							\
		fprintf(stderr,						\
		    "\nAssertion failure("#cond") at (%s):%s:%d\n",	\
		    __func__, __FILE__, __LINE__);			\
	}
#else
#define	SLIST_ASSERT(cond)
#endif

/**
 * Returns 1 if a index is in the valid range, otherwise returns 0.
 */
#define	VALID_IDX(_list, _idx)					\
	  (((_list)->first_idx <= (_list)->last_idx)			\
	? (((_list)->first_idx <= (_idx) && (_idx) < (_list)->last_idx)? 1 : 0)\
	: (((_list)->first_idx <= (_idx) || (_idx) < (_list)->last_idx)? 1 : 0))

/** Convert an index into the internal index */
#define	REAL_IDX(_list, _idx)						\
	(((_list)->first_idx + (_idx)) % (_list)->list_size)

/** Convert a virtual index into the index */
#define	VIRT_IDX(_list, _idx)	(((_list)->first_idx <= (_idx))	\
	? (_idx) - (_list)->first_idx				\
	: (_list)->list_size - (_list)->first_idx + (_idx))

/** Decrement an index */
#define	DECR_IDX(_list, _memb)						\
	(_list)->_memb = ((_list)->list_size + --((_list)->_memb))	\
	    % (_list)->list_size
/** Increment an index */
#define	INCR_IDX(_list, _memb)						\
	(_list)->_memb = (++((_list)->_memb)) % (_list)->list_size

static int          slist_grow (slist *);
static int          slist_grow0 (slist *, int);
static __inline void  slist_swap0 (slist *, int, int);
static __inline void  slist_qsort0(slist *, int (*)(const void *, const void *), int, int);

#define	itr_is_valid(list)	((list)->itr_next >= 0)
#define	itr_invalidate(list)	((list)->itr_next = -1)

/** Initialize a slist */
void
slist_init(slist *list)
{
	memset(list, 0, sizeof(slist));
	itr_invalidate(list);
}

/**
 * Specify the size of a list. The size must be specified with the size you
 * want to use +1. Extra 1 entry is for internal use. The size doesn't shrink.
 */
int
slist_set_size(slist *list, int size)
{
	if (size > list->list_size)
		return slist_grow0(list, size - list->list_size);

	return 0;
}

/** Finish using. Free the buffers and reinit. */
void
slist_fini(slist *list)
{
	free(list->list);
	slist_init(list);
}

/** The length of the list */
int
slist_length(slist *list)
{
	return
	      (list->first_idx <= list->last_idx)
	    ? (list->last_idx - list->first_idx)
	    : (list->list_size - list->first_idx + list->last_idx);
}

/** Extend the size. Used if the list is full. */
static int
slist_grow0(slist *list, int grow_sz)
{
	int size_new;
	void **list_new = NULL;

 	/* just return if it is possible to add one item */
	if (slist_length(list) + 1 < list->list_size)
		/* "+ 1" to avoid the situation list_size == slist_length() */
		return 0;

	size_new = list->list_size + grow_sz;
	if ((list_new = realloc(list->list, PTR_SIZE * size_new))
	    == NULL)
		return -1;

	memset(&list_new[list->list_size], 0,
	    PTR_SIZE * (size_new - list->list_size));

	list->list = list_new;
	if (list->last_idx < list->first_idx && list->last_idx >= 0) {

		/*
		 * space is created at the right side when center has space,
		 * so move left side to right side
		 */
		if (list->last_idx <= grow_sz) {
			/*
			 * The right side has enough space, so move the left
			 * side to right side.
			 */
			memmove(&list->list[list->list_size],
			    &list->list[0], PTR_SIZE * list->last_idx);
			list->last_idx = list->list_size + list->last_idx;
		} else {
			/*
			 * Copy the left side to right side as long as we
			 * can
			 */
			memmove(&list->list[list->list_size],
			    &list->list[0], PTR_SIZE * grow_sz);
			/* Shift the remain to left */
			memmove(&list->list[0], &list->list[grow_sz],
			    PTR_SIZE *(list->last_idx - grow_sz));

			list->last_idx -= grow_sz;
		}
	}
	list->list_size = size_new;

	return 0;
}

static int
slist_grow(slist *list)
{
	return slist_grow0(list, GROW_SIZE);
}

/** Add an item to a list */
void *
slist_add(slist *list, void *item)
{
	if (slist_grow(list) != 0)
		return NULL;

	list->list[list->last_idx] = item;

	if (list->itr_next == -2) {
		/* the iterator points the last, update it. */
		list->itr_next = list->last_idx;
	}

	INCR_IDX(list, last_idx);

	return item;
}

#define slist_get0(list_, idx)	((list_)->list[REAL_IDX((list_), (idx))])

/** Add all items in add_items to a list. */
int
slist_add_all(slist *list, slist *add_items)
{
	int i, n;

	n = slist_length(add_items);
	for (i = 0; i < n; i++) {
		if (slist_add(list, slist_get0(add_items, i)) ==  NULL)
			return 1;
	}

	return 0;
}

/** Return "idx"th item. */
void *
slist_get(slist *list, int idx)
{
	SLIST_ASSERT(idx >= 0);
	SLIST_ASSERT(slist_length(list) > idx);

	if (idx < 0 || slist_length(list) <= idx)
		return NULL;

	return slist_get0(list, idx);
}

/** Store a value in "idx"th item */
int
slist_set(slist *list, int idx, void *item)
{
	SLIST_ASSERT(idx >= 0);
	SLIST_ASSERT(slist_length(list) > idx);

	if (idx < 0 || slist_length(list) <= idx)
		return -1;

	list->list[REAL_IDX(list, idx)] = item;

	return 0;
}

/** Remove the 1st entry and return it. */
void *
slist_remove_first(slist *list)
{
	void *oldVal;

	if (slist_length(list) <= 0)
		return NULL;

	oldVal = list->list[list->first_idx];

	if (itr_is_valid(list) && list->itr_next == list->first_idx)
		INCR_IDX(list, itr_next);

	if (!VALID_IDX(list, list->itr_next))
		itr_invalidate(list);

	INCR_IDX(list, first_idx);

	return oldVal;
}

/** Remove the last entry and return it */
void *
slist_remove_last(slist *list)
{
	if (slist_length(list) <= 0)
		return NULL;

	DECR_IDX(list, last_idx);
	if (!VALID_IDX(list, list->itr_next))
		itr_invalidate(list);

	return list->list[list->last_idx];
}

/** Remove all entries */
void
slist_remove_all(slist *list)
{
	void **list0 = list->list;

	slist_init(list);

	list->list = list0;
}

/* Swap items. This doesn't check boundary. */
static __inline void
slist_swap0(slist *list, int m, int n)
{
	void *m0;

	itr_invalidate(list);	/* Invalidate iterator */

	m0 = list->list[REAL_IDX(list, m)];
	list->list[REAL_IDX(list, m)] = list->list[REAL_IDX(list, n)];
	list->list[REAL_IDX(list, n)] = m0;
}

/** Swap between mth and nth */
void
slist_swap(slist *list, int m, int n)
{
	int len;

	len = slist_length(list);
	SLIST_ASSERT(m >= 0);
	SLIST_ASSERT(n >= 0);
	SLIST_ASSERT(len > m);
	SLIST_ASSERT(len > n);

	if (m < 0 || n < 0)
		return;
	if (m >= len || n >= len)
		return;

	slist_swap0(list, m, n);
}

/** Remove "idx"th item */
void *
slist_remove(slist *list, int idx)
{
	int first, last, idx0, reset_itr;
	void *oldVal;

	SLIST_ASSERT(idx >= 0);
	SLIST_ASSERT(slist_length(list) > idx);

	if (idx < 0 || slist_length(list) <= idx)
		return NULL;

	idx0 = REAL_IDX(list, idx);
	oldVal = list->list[idx0];
	reset_itr = 0;

	first = -1;
	last = -1;

	if (list->itr_next == idx0) {
		INCR_IDX(list, itr_next);
		if (!VALID_IDX(list, list->itr_next))
			list->itr_next = -2;	/* on the last item */
	}

	/* should we reduce the last side or the first side? */
	if (list->first_idx < list->last_idx) {
		/* take the smaller side */
		if (idx0 - list->first_idx < list->last_idx - idx0) {
			first = list->first_idx;
			INCR_IDX(list, first_idx);
		} else {
			last = list->last_idx;
			DECR_IDX(list, last_idx);
		}
	} else {
		/*
		 * 0 < last (unused) first < idx < size, so let's reduce the
		 * first.
		 */
		if (list->first_idx <= idx0) {
			first = list->first_idx;
			INCR_IDX(list, first_idx);
		} else {
			last = list->last_idx;
			DECR_IDX(list, last_idx);
		}
	}

	/* the last side */
	if (last != -1 && last != 0 && last != idx0) {

		/* move left the items that is from idx0 to the last */
		if (itr_is_valid(list) &&
		    idx0 <= list->itr_next && list->itr_next <= last) {
			DECR_IDX(list, itr_next);
			if (!VALID_IDX(list, list->itr_next))
				itr_invalidate(list);
		}

		memmove(&list->list[idx0], &list->list[idx0 + 1],
		    (PTR_SIZE) * (last - idx0));
	}
	/* the first side */
	if (first != -1 && first != idx0) {

		/* move right the items that is from first to the idx0 */
		if (itr_is_valid(list) &&
		    first <= list->itr_next && list->itr_next <= idx0) {
			INCR_IDX(list, itr_next);
			if (!VALID_IDX(list, list->itr_next))
				itr_invalidate(list);
		}

		memmove(&list->list[first + 1], &list->list[first],
		    (PTR_SIZE) * (idx0 - first));
	}
	if (list->first_idx == list->last_idx) {
		list->first_idx = 0;
		list->last_idx = 0;
	}

	return oldVal;
}

/**
 * Shuffle items.
 */
void
slist_shuffle(slist *list)
{
	int i, len;

	len = slist_length(list);
	for (i = len; i > 1; i--)
		slist_swap0(list, i - 1, (int)arc4random_uniform(i));
}

/** Init an iterator. Only one iterator exists.  */
void
slist_itr_first(slist *list)
{
	list->itr_next = list->first_idx;
	if (!VALID_IDX(list, list->itr_next))
		itr_invalidate(list);
}

/**
 * Return whether a iterator can go to the next item.
 * @return Return 1 if the iterator can return the next item.
 *	Return 0 it reaches the end of the list or the list is modified
 *	destructively.
 */
int
slist_itr_has_next(slist *list)
{
	if (list->itr_next < 0)
		return 0;
	return VALID_IDX(list, list->itr_next);
}

/** Return the next item and iterate to the next */
void *
slist_itr_next(slist *list)
{
	void *rval;

	if (!itr_is_valid(list))
		return NULL;
	SLIST_ASSERT(VALID_IDX(list, list->itr_next));

	if (list->list == NULL)
		return NULL;

	rval = list->list[list->itr_next];
	list->itr_curr = list->itr_next;
	INCR_IDX(list, itr_next);

	if (!VALID_IDX(list, list->itr_next))
		list->itr_next = -2;	/* on the last item */

	return rval;
}

/** Delete the current iterated item  */
void *
slist_itr_remove(slist *list)
{
	SLIST_ASSERT(list != NULL);

	return slist_remove(list, VIRT_IDX(list, list->itr_curr));
}

/** Sort the list items by quick sort algorithm using given compar */
void
slist_qsort(slist *list, int (*compar)(const void *, const void *))
{
	if (list->first_idx != list->last_idx)	/* is not empty */
		slist_qsort0(list, compar, 0, slist_length(list) - 1);
}

static __inline void
slist_qsort0(slist *list, int (*compar)(const void *, const void *), int l,
    int r)
{
	int i, j;
	void *p;

	i = l;
	j = r;
	p = slist_get0(list, (j + i) / 2);
	while (i <= j) {
		while (compar(slist_get0(list, i), p) < 0)
			i++;
		while (compar(slist_get0(list, j), p) > 0)
			j--;
		if (i <= j)
			slist_swap0(list, i++, j--);
	}
	if (l < j)
		slist_qsort0(list, compar, l, j);
	if (i < r)
		slist_qsort0(list, compar, i, r);
}
