/*	$OpenBSD: dict.c,v 1.8 2021/06/14 17:58:15 eric Exp $	*/

/*
 * Copyright (c) 2012 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
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

#include <sys/tree.h>

#include <stdlib.h>
#include <string.h>

#include "dict.h"
#include "log.h"

struct dictentry {
	SPLAY_ENTRY(dictentry)	entry;
	const char	       *key;
	void		       *data;
};

static int dictentry_cmp(struct dictentry *, struct dictentry *);

SPLAY_PROTOTYPE(_dict, dictentry, entry, dictentry_cmp);

int
dict_check(struct dict *d, const char *k)
{
	struct dictentry	key;

	key.key = k;
	return (SPLAY_FIND(_dict, &d->dict, &key) != NULL);
}

static inline struct dictentry *
dict_alloc(const char *k, void *data)
{
	struct dictentry	*e;
	size_t			 s = strlen(k) + 1;
	void			*t;

	if ((e = malloc(sizeof(*e) + s)) == NULL)
		return NULL;

	e->key = t = (char*)(e) + sizeof(*e);
	e->data = data;
	memmove(t, k, s);

	return (e);
}

void *
dict_set(struct dict *d, const char *k, void *data)
{
	struct dictentry	*entry, key;
	char			*old;

	key.key = k;
	if ((entry = SPLAY_FIND(_dict, &d->dict, &key)) == NULL) {
		if ((entry = dict_alloc(k, data)) == NULL)
			fatal("dict_set: malloc");
		SPLAY_INSERT(_dict, &d->dict, entry);
		old = NULL;
		d->count += 1;
	} else {
		old = entry->data;
		entry->data = data;
	}

	return (old);
}

void
dict_xset(struct dict *d, const char * k, void *data)
{
	struct dictentry	*entry;

	if ((entry = dict_alloc(k, data)) == NULL)
		fatal("dict_xset: malloc");
	if (SPLAY_INSERT(_dict, &d->dict, entry))
		fatalx("dict_xset(%p, %s)", d, k);
	d->count += 1;
}

void *
dict_get(struct dict *d, const char *k)
{
	struct dictentry	key, *entry;

	key.key = k;
	if ((entry = SPLAY_FIND(_dict, &d->dict, &key)) == NULL)
		return (NULL);

	return (entry->data);
}

void *
dict_xget(struct dict *d, const char *k)
{
	struct dictentry	key, *entry;

	key.key = k;
	if ((entry = SPLAY_FIND(_dict, &d->dict, &key)) == NULL)
		fatalx("dict_xget(%p, %s)", d, k);

	return (entry->data);
}

void *
dict_pop(struct dict *d, const char *k)
{
	struct dictentry	key, *entry;
	void			*data;

	key.key = k;
	if ((entry = SPLAY_FIND(_dict, &d->dict, &key)) == NULL)
		return (NULL);

	data = entry->data;
	SPLAY_REMOVE(_dict, &d->dict, entry);
	free(entry);
	d->count -= 1;

	return (data);
}

void *
dict_xpop(struct dict *d, const char *k)
{
	struct dictentry	key, *entry;
	void			*data;

	key.key = k;
	if ((entry = SPLAY_FIND(_dict, &d->dict, &key)) == NULL)
		fatalx("dict_xpop(%p, %s)", d, k);

	data = entry->data;
	SPLAY_REMOVE(_dict, &d->dict, entry);
	free(entry);
	d->count -= 1;

	return (data);
}

int
dict_poproot(struct dict *d, void **data)
{
	struct dictentry	*entry;

	entry = SPLAY_ROOT(&d->dict);
	if (entry == NULL)
		return (0);
	if (data)
		*data = entry->data;
	SPLAY_REMOVE(_dict, &d->dict, entry);
	free(entry);
	d->count -= 1;

	return (1);
}

int
dict_root(struct dict *d, const char **k, void **data)
{
	struct dictentry	*entry;

	entry = SPLAY_ROOT(&d->dict);
	if (entry == NULL)
		return (0);
	if (k)
		*k = entry->key;
	if (data)
		*data = entry->data;
	return (1);
}

int
dict_iter(struct dict *d, void **hdl, const char **k, void **data)
{
	struct dictentry *curr = *hdl;

	if (curr == NULL)
		curr = SPLAY_MIN(_dict, &d->dict);
	else
		curr = SPLAY_NEXT(_dict, &d->dict, curr);

	if (curr) {
		*hdl = curr;
		if (k)
			*k = curr->key;
		if (data)
			*data = curr->data;
		return (1);
	}

	return (0);
}

int
dict_iterfrom(struct dict *d, void **hdl, const char *kfrom, const char **k,
    void **data)
{
	struct dictentry *curr = *hdl, key;

	if (curr == NULL) {
		if (kfrom == NULL)
			curr = SPLAY_MIN(_dict, &d->dict);
		else {
			key.key = kfrom;
			curr = SPLAY_FIND(_dict, &d->dict, &key);
			if (curr == NULL) {
				SPLAY_INSERT(_dict, &d->dict, &key);
				curr = SPLAY_NEXT(_dict, &d->dict, &key);
				SPLAY_REMOVE(_dict, &d->dict, &key);
			}
		}
	} else
		curr = SPLAY_NEXT(_dict, &d->dict, curr);

	if (curr) {
		*hdl = curr;
		if (k)
			*k = curr->key;
		if (data)
			*data = curr->data;
		return (1);
	}

	return (0);
}

void
dict_merge(struct dict *dst, struct dict *src)
{
	struct dictentry	*entry;

	while (!SPLAY_EMPTY(&src->dict)) {
		entry = SPLAY_ROOT(&src->dict);
		SPLAY_REMOVE(_dict, &src->dict, entry);
		if (SPLAY_INSERT(_dict, &dst->dict, entry))
			fatalx("dict_merge: duplicate");
	}
	dst->count += src->count;
	src->count = 0;
}

static int
dictentry_cmp(struct dictentry *a, struct dictentry *b)
{
	return strcmp(a->key, b->key);
}

SPLAY_GENERATE(_dict, dictentry, entry, dictentry_cmp);
