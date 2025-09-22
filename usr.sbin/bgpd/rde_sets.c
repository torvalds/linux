/*	$OpenBSD: rde_sets.c,v 1.13 2024/09/10 09:38:45 claudio Exp $ */

/*
 * Copyright (c) 2018 Claudio Jeker <claudio@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/types.h>
#include <sys/queue.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "rde.h"

struct set_table {
	void			*set;
	size_t			 nmemb;
	size_t			 size;
	size_t			 max;
};

struct as_set *
as_sets_new(struct as_set_head *as_sets, const char *name, size_t nmemb,
    size_t size)
{
	struct as_set *aset;
	size_t len;

	aset = calloc(1, sizeof(*aset));
	if (aset == NULL)
		return NULL;

	len = strlcpy(aset->name, name, sizeof(aset->name));
	assert(len < sizeof(aset->name));

	aset->set = set_new(nmemb, size);
	if (aset->set == NULL) {
		free(aset);
		return NULL;
	}

	SIMPLEQ_INSERT_TAIL(as_sets, aset, entry);
	return aset;
}

struct as_set *
as_sets_lookup(struct as_set_head *as_sets, const char *name)
{
	struct as_set *aset;

	SIMPLEQ_FOREACH(aset, as_sets, entry) {
		if (strcmp(aset->name, name) == 0)
			return aset;
	}
	return NULL;
}


void
as_sets_free(struct as_set_head *as_sets)
{
	struct as_set *aset;

	if (as_sets == NULL)
		return;
	while (!SIMPLEQ_EMPTY(as_sets)) {
		aset = SIMPLEQ_FIRST(as_sets);
		SIMPLEQ_REMOVE_HEAD(as_sets, entry);
		set_free(aset->set);
		free(aset);
	}
}

void
as_sets_mark_dirty(struct as_set_head *old, struct as_set_head *new)
{
	struct as_set	*n, *o;

	SIMPLEQ_FOREACH(n, new, entry) {
		if (old == NULL || (o = as_sets_lookup(old, n->name)) == NULL ||
		    !set_equal(n->set, o->set)) {
			n->dirty = 1;
			n->lastchange = getmonotime();
		} else
			n->lastchange = o->lastchange;
	}
}

int
as_set_match(const struct as_set *aset, uint32_t asnum)
{
	return set_match(aset->set, asnum) != NULL;
}

struct set_table *
set_new(size_t nmemb, size_t size)
{
	struct set_table *set;

	set = calloc(1, sizeof(*set));
	if (set == NULL)
		return NULL;

	if (nmemb == 0)
		nmemb = 4;

	set->size = size;
	set->max = nmemb;
	set->set = calloc(nmemb, set->size);
	if (set->set == NULL) {
		free(set);
		return NULL;
	}

	rdemem.aset_cnt++;
	rdemem.aset_size += sizeof(*set);
	rdemem.aset_size += set->size * set->max;
	return set;
}

void
set_free(struct set_table *set)
{
	if (set == NULL)
		return;
	rdemem.aset_cnt--;
	rdemem.aset_size -= sizeof(*set);
	rdemem.aset_size -= set->size * set->max;
	rdemem.aset_nmemb -= set->nmemb;
	free(set->set);
	free(set);
}

int
set_add(struct set_table *set, void *elms, size_t nelms)
{
	if (nelms == 0)		/* nothing todo */
		return 0;

	if (set->max < nelms || set->max - nelms < set->nmemb) {
		uint32_t *s;
		size_t new_size;

		if (set->nmemb >= SIZE_MAX - 4096 - nelms) {
			errno = ENOMEM;
			return -1;
		}
		for (new_size = set->max; new_size < set->nmemb + nelms; )
			new_size += (new_size < 4096 ? new_size : 4096);

		s = reallocarray(set->set, new_size, set->size);
		if (s == NULL)
			return -1;
		rdemem.aset_size += set->size * (new_size - set->max);
		set->set = s;
		set->max = new_size;
	}

	memcpy((uint8_t *)set->set + set->nmemb * set->size, elms,
	    nelms * set->size);
	set->nmemb += nelms;
	rdemem.aset_nmemb += nelms;

	return 0;
}

void *
set_get(struct set_table *set, size_t *nelms)
{
	*nelms = set->nmemb;
	return set->set;
}

static int
set_cmp(const void *ap, const void *bp)
{
	const uint32_t *a = ap;
	const uint32_t *b = bp;

	if (*a > *b)
		return 1;
	else if (*a < *b)
		return -1;
	return 0;
}

void
set_prep(struct set_table *set)
{
	if (set == NULL)
		return;
	qsort(set->set, set->nmemb, set->size, set_cmp);
}

void *
set_match(const struct set_table *a, uint32_t asnum)
{
	if (a == NULL)
		return NULL;
	return bsearch(&asnum, a->set, a->nmemb, a->size, set_cmp);
}

int
set_equal(const struct set_table *a, const struct set_table *b)
{
	/* allow NULL pointers to be passed */
	if (a == NULL && b == NULL)
		return 1;
	if (a == NULL || b == NULL)
		return 0;

	if (a->nmemb != b->nmemb)
		return 0;
	if (memcmp(a->set, b->set, a->nmemb * a->size) != 0)
		return 0;
	return 1;
}

size_t
set_nmemb(const struct set_table *set)
{
	return set->nmemb;
}
