/*	$NetBSD: linux_list_sort.c,v 1.2 2014/03/18 18:20:43 riastradh Exp $	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Taylor R. Campbell.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/limits.h>

#include <linux/list.h>

static struct list_head *
list_sort_merge(struct list_head *, struct list_head *,
		int (*)(void *, const struct list_head *,
		const struct list_head *), void *);
static void
list_sort_merge_into(struct list_head *,
		     struct list_head *, struct list_head *,
		     int (*)(void *, const struct list_head *,
		     const struct list_head *), void *);

void
list_sort(void *arg, struct list_head *list,
	  int (*compare)(void *, const struct list_head *, const struct list_head *))
{
	/*
	 * Array of sorted sublists, counting in binary: accum[i]
	 * is sorted, and either is NULL or has length 2^i.
	 */
	struct list_head *accum[64];

	/* Indices into accum.  */
	unsigned int logn, max_logn = 0;

	/* The sorted list we're currently working on.  */
	struct list_head *sorted;

	/* The remainder of the unsorted list.  */
	struct list_head *next;

	/* Make sure we can't possibly have more than 2^64-element lists.  */
	CTASSERT((CHAR_BIT * sizeof(struct list_head *)) <= 64);

	for (logn = 0; logn < nitems(accum); logn++)
		accum[logn] = NULL;

	list_for_each_safe(sorted, next, list) {
		/* Pick off a single element, always sorted.  */
		sorted->next = NULL;

		/* Add one and propagate the carry.  */
		for (logn = 0; accum[logn] != NULL; logn++) {
			/*
			 * Merge, preferring previously accumulated
			 * elements to make the sort stable.
			 */
			sorted = list_sort_merge(accum[logn], sorted, compare, arg);
			accum[logn] = NULL;
			KASSERT((logn + 1) < nitems(accum));
		}

		/* Remember the highest index seen so far.  */
		if (logn > max_logn)
			max_logn = logn;

		/*
		 * logn = log_2(length(sorted)), and accum[logn]
		 * is now empty, so save the sorted sublist there.
		 */
		accum[logn] = sorted;
	}

	/*
	 * Merge ~half of everything we have accumulated.
	 */
	sorted = NULL;
	for (logn = 0; logn < max_logn; logn++)
		sorted = list_sort_merge(accum[logn], sorted, compare, arg);

	/*
	 * Merge the last ~halves back into the list, and fix the back
	 * pointers.
	 */
	list_sort_merge_into(list, accum[max_logn], sorted, compare, arg);
}

/*
 * Merge the NULL-terminated lists starting at nodes `a' and `b',
 * breaking ties by choosing nodes in `a' first, and returning
 * whichever node has the least element.
 */
static struct list_head *
list_sort_merge(struct list_head *a, struct list_head *b,
		int (*compare)(void *, const struct list_head *,
		const struct list_head *), void *arg)
{
	struct list_head head, *tail = &head;

	/*
	 * Merge while elements in both remain.
	 */
	while ((a != NULL) && (b != NULL)) {
		struct list_head **const first = ((*compare)(arg, a, b) <= 0?
		    &a : &b);

		tail = tail->next = *first;
		*first = (*first)->next;
	}

	/*
	 * Attach whatever remains.
	 */
	tail->next = (a != NULL? a : b);
	return head.next;
}

/*
 * Merge the NULL-terminated lists starting at nodes `a' and `b' into
 * the (uninitialized) list head `list', breaking ties by choosing
 * nodes in `a' first, and setting the `prev' pointers as we go.
 */
static void
list_sort_merge_into(struct list_head *list,
		     struct list_head *a, struct list_head *b,
		     int (*compare)(void *, const struct list_head *,
		     const struct list_head *), void *arg)
{
	struct list_head *prev = list;

	/*
	 * Merge while elements in both remain.
	 */
	while ((a != NULL) && (b != NULL)) {
		struct list_head **const first = (
			(*compare)(arg, a, b) <= 0 ? &a : &b);

		(*first)->prev = prev;
		prev = prev->next = *first;
		*first = (*first)->next;
	}

	/*
	 * Attach whichever of a and b remains, and fix up the prev
	 * pointers all the way down the rest of the list.
	 */
	struct list_head *tail = (a == NULL? b : a);
	while (tail != NULL) {
		prev->next = tail;
		tail->prev = prev;
		prev = prev->next;
		tail = tail->next;
	}

	/*
	 * Finally, finish the cycle.
	 */
	prev->next = list;
	list->prev = prev;
}
