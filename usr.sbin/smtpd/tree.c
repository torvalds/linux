/*	$OpenBSD: tree.c,v 1.8 2021/06/14 17:58:16 eric Exp $	*/

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

#include <sys/tree.h>

#include <inttypes.h>
#include <stdlib.h>

#include "tree.h"
#include "log.h"

struct treeentry {
	SPLAY_ENTRY(treeentry)	 entry;
	uint64_t		 id;
	void			*data;
};

static int treeentry_cmp(struct treeentry *, struct treeentry *);

SPLAY_PROTOTYPE(_tree, treeentry, entry, treeentry_cmp);

int
tree_check(struct tree *t, uint64_t id)
{
	struct treeentry	key;

	key.id = id;
	return (SPLAY_FIND(_tree, &t->tree, &key) != NULL);
}

void *
tree_set(struct tree *t, uint64_t id, void *data)
{
	struct treeentry	*entry, key;
	char			*old;

	key.id = id;
	if ((entry = SPLAY_FIND(_tree, &t->tree, &key)) == NULL) {
		if ((entry = malloc(sizeof *entry)) == NULL)
			fatal("tree_set: malloc");
		entry->id = id;
		SPLAY_INSERT(_tree, &t->tree, entry);
		old = NULL;
		t->count += 1;
	} else
		old = entry->data;

	entry->data = data;

	return (old);
}

void
tree_xset(struct tree *t, uint64_t id, void *data)
{
	struct treeentry	*entry;

	if ((entry = malloc(sizeof *entry)) == NULL)
		fatal("tree_xset: malloc");
	entry->id = id;
	entry->data = data;
	if (SPLAY_INSERT(_tree, &t->tree, entry))
		fatalx("tree_xset(%p, 0x%016"PRIx64 ")", t, id);
	t->count += 1;
}

void *
tree_get(struct tree *t, uint64_t id)
{
	struct treeentry	key, *entry;

	key.id = id;
	if ((entry = SPLAY_FIND(_tree, &t->tree, &key)) == NULL)
		return (NULL);

	return (entry->data);
}

void *
tree_xget(struct tree *t, uint64_t id)
{
	struct treeentry	key, *entry;

	key.id = id;
	if ((entry = SPLAY_FIND(_tree, &t->tree, &key)) == NULL)
		fatalx("tree_get(%p, 0x%016"PRIx64 ")", t, id);

	return (entry->data);
}

void *
tree_pop(struct tree *t, uint64_t id)
{
	struct treeentry	key, *entry;
	void			*data;

	key.id = id;
	if ((entry = SPLAY_FIND(_tree, &t->tree, &key)) == NULL)
		return (NULL);

	data = entry->data;
	SPLAY_REMOVE(_tree, &t->tree, entry);
	free(entry);
	t->count -= 1;

	return (data);
}

void *
tree_xpop(struct tree *t, uint64_t id)
{
	struct treeentry	key, *entry;
	void			*data;

	key.id = id;
	if ((entry = SPLAY_FIND(_tree, &t->tree, &key)) == NULL)
		fatalx("tree_xpop(%p, 0x%016" PRIx64 ")", t, id);

	data = entry->data;
	SPLAY_REMOVE(_tree, &t->tree, entry);
	free(entry);
	t->count -= 1;

	return (data);
}

int
tree_poproot(struct tree *t, uint64_t *id, void **data)
{
	struct treeentry	*entry;

	entry = SPLAY_ROOT(&t->tree);
	if (entry == NULL)
		return (0);
	if (id)
		*id = entry->id;
	if (data)
		*data = entry->data;
	SPLAY_REMOVE(_tree, &t->tree, entry);
	free(entry);
	t->count -= 1;

	return (1);
}

int
tree_root(struct tree *t, uint64_t *id, void **data)
{
	struct treeentry	*entry;

	entry = SPLAY_ROOT(&t->tree);
	if (entry == NULL)
		return (0);
	if (id)
		*id = entry->id;
	if (data)
		*data = entry->data;
	return (1);
}

int
tree_iter(struct tree *t, void **hdl, uint64_t *id, void **data)
{
	struct treeentry *curr = *hdl;

	if (curr == NULL)
		curr = SPLAY_MIN(_tree, &t->tree);
	else
		curr = SPLAY_NEXT(_tree, &t->tree, curr);

	if (curr) {
		*hdl = curr;
		if (id)
			*id = curr->id;
		if (data)
			*data = curr->data;
		return (1);
	}

	return (0);
}

int
tree_iterfrom(struct tree *t, void **hdl, uint64_t k, uint64_t *id, void **data)
{
	struct treeentry *curr = *hdl, key;

	if (curr == NULL) {
		if (k == 0)
			curr = SPLAY_MIN(_tree, &t->tree);
		else {
			key.id = k;
			curr = SPLAY_FIND(_tree, &t->tree, &key);
			if (curr == NULL) {
				SPLAY_INSERT(_tree, &t->tree, &key);
				curr = SPLAY_NEXT(_tree, &t->tree, &key);
				SPLAY_REMOVE(_tree, &t->tree, &key);
			}
		}
	} else
		curr = SPLAY_NEXT(_tree, &t->tree, curr);

	if (curr) {
		*hdl = curr;
		if (id)
			*id = curr->id;
		if (data)
			*data = curr->data;
		return (1);
	}

	return (0);
}

void
tree_merge(struct tree *dst, struct tree *src)
{
	struct treeentry	*entry;

	while (!SPLAY_EMPTY(&src->tree)) {
		entry = SPLAY_ROOT(&src->tree);
		SPLAY_REMOVE(_tree, &src->tree, entry);
		if (SPLAY_INSERT(_tree, &dst->tree, entry))
			fatalx("tree_merge: duplicate");
	}
	dst->count += src->count;
	src->count = 0;
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

SPLAY_GENERATE(_tree, treeentry, entry, treeentry_cmp);
