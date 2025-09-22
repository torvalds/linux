/*	$OpenBSD: tree.c,v 1.2 2014/02/05 20:13:58 syl Exp $	*/

/*
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

#include <stdlib.h>
#include <errno.h>

#include "fuse_private.h"

struct treeentry {
	SPLAY_ENTRY(treeentry)	 entry;
	uint64_t		 id;
	void			*data;
};

static int treeentry_cmp(struct treeentry *, struct treeentry *);

SPLAY_PROTOTYPE(tree, treeentry, entry, treeentry_cmp);

int
tree_check(struct tree *t, uint64_t id)
{
	struct treeentry	key;

	key.id = id;
	return (SPLAY_FIND(tree, t, &key) != NULL);
}

void *
tree_set(struct tree *t, uint64_t id, void *data)
{
	struct treeentry	*entry, key;

	key.id = id;
	if ((entry = SPLAY_FIND(tree, t, &key)) == NULL) {
		entry = malloc(sizeof *entry);
		if (entry == NULL)
			return (NULL);
		entry->id = id;
		SPLAY_INSERT(tree, t, entry);
	}

	entry->data = data;

	return (entry);
}

void *
tree_get(struct tree *t, uint64_t id)
{
	struct treeentry	key, *entry;

	key.id = id;
	if ((entry = SPLAY_FIND(tree, t, &key)) == NULL) {
		errno = ENOENT;
		return (NULL);
	}

	return (entry->data);
}

void *
tree_pop(struct tree *t, uint64_t id)
{
	struct treeentry	key, *entry;
	void			*data;

	key.id = id;
	if ((entry = SPLAY_FIND(tree, t, &key)) == NULL)
		return (NULL);

	data = entry->data;
	SPLAY_REMOVE(tree, t, entry);
	free(entry);

	return (data);
}

static int
treeentry_cmp(struct treeentry *a, struct treeentry *b)
{
	if (a->id < b->id)
		return (-1);
	if (a->id > b->id)
		return (1);
	return (0);
}

SPLAY_GENERATE(tree, treeentry, entry, treeentry_cmp);
