/*	$OpenBSD: dict.c,v 1.2 2014/04/28 13:08:34 syl Exp $	*/

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

#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "fuse_private.h"

#define	MAX_DICTKEY_SIZE	NAME_MAX
struct dictentry {
	SPLAY_ENTRY(dictentry)	entry;
	char			key[MAX_DICTKEY_SIZE + 1];
	void		       *data;
};

static int dictentry_cmp(struct dictentry *, struct dictentry *);

SPLAY_PROTOTYPE(dict, dictentry, entry, dictentry_cmp);

int
dict_check(struct dict *d, const char *k)
{
	struct dictentry	key;

	if (strlcpy(key.key, k, sizeof key.key) >= sizeof key.key)
		errx(1, "dict_check(%p, %s): key too large", d, k);

	return (SPLAY_FIND(dict, d, &key) != NULL);
}

void *
dict_set(struct dict *d, const char *k, void *data)
{
	struct dictentry	*entry, key;

	if (strlcpy(key.key, k, sizeof key.key) >= sizeof key.key)
		errx(1, "dict_set(%p, %s): key too large", d, k);
	if ((entry = SPLAY_FIND(dict, d, &key)) == NULL) {
		entry = malloc(sizeof *entry);
		if (entry == NULL)
			return (NULL);

		strlcpy(entry->key, k, sizeof entry->key);
		SPLAY_INSERT(dict, d, entry);
	}

	entry->data = data;

	return (entry);
}

void *
dict_get(struct dict *d, const char *k)
{
	struct dictentry	key, *entry;

	if (strlcpy(key.key, k, sizeof key.key) >= sizeof key.key)
		errx(1, "dict_get(%p, %s): key too large", d, k);
	if ((entry = SPLAY_FIND(dict, d, &key)) == NULL)
		return (NULL);

	return (entry->data);
}

void *
dict_pop(struct dict *d, const char *k)
{
	struct dictentry	key, *entry;
	void			*data;

	if (strlcpy(key.key, k, sizeof key.key) >= sizeof key.key)
		errx(1, "dict_pop(%p, %s): key too large", d, k);
	if ((entry = SPLAY_FIND(dict, d, &key)) == NULL)
		return (NULL);

	data = entry->data;
	SPLAY_REMOVE(dict, d, entry);
	free(entry);

	return (data);
}

static int
dictentry_cmp(struct dictentry *a, struct dictentry *b)
{
	return strcmp(a->key, b->key);
}

SPLAY_GENERATE(dict, dictentry, entry, dictentry_cmp);
