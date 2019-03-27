/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Tim J. Robbins.
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
/*
 * "Character map" ADT. Stores mappings between pairs of characters in a
 * splay tree, with a lookup table cache to simplify looking up the first
 * bunch of characters (which are presumably more common than others).
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wchar.h>
#include "cmap.h"

static struct cmapnode *cmap_splay(struct cmapnode *, wint_t);

/*
 * cmap_alloc --
 *	Allocate a character map.
 */
struct cmap *
cmap_alloc(void)
{
	struct cmap *cm;

	cm = malloc(sizeof(*cm));
	if (cm == NULL)
		return (NULL);
	cm->cm_root = NULL;
	cm->cm_def = CM_DEF_SELF;
	cm->cm_havecache = false;
	cm->cm_min = cm->cm_max = 0;
	return (cm);
}

/*
 * cmap_add --
 *	Add a mapping from "from" to "to" to the map.
 */
bool
cmap_add(struct cmap *cm, wint_t from, wint_t to)
{
	struct cmapnode *cmn, *ncmn;

	cm->cm_havecache = false;

	if (cm->cm_root == NULL) {
		cmn = malloc(sizeof(*cmn));
		if (cmn == NULL)
			return (false);
		cmn->cmn_from = from;
		cmn->cmn_to = to;
		cmn->cmn_left = cmn->cmn_right = NULL;
		cm->cm_root = cmn;
		cm->cm_min = cm->cm_max = from;
		return (true);
	}

	cmn = cm->cm_root = cmap_splay(cm->cm_root, from);

	if (cmn->cmn_from == from) {
		cmn->cmn_to = to;
		return (true);
	}

	ncmn = malloc(sizeof(*ncmn));
	if (ncmn == NULL)
		return (false);
	ncmn->cmn_from = from;
	ncmn->cmn_to = to;
	if (from < cmn->cmn_from) {
		ncmn->cmn_left = cmn->cmn_left;
		ncmn->cmn_right = cmn;
		cmn->cmn_left = NULL;
	} else {
		ncmn->cmn_right = cmn->cmn_right;
		ncmn->cmn_left = cmn;
		cmn->cmn_right = NULL;
	}
	if (from < cm->cm_min)
		cm->cm_min = from;
	if (from > cm->cm_max)
		cm->cm_max = from;
        cm->cm_root = ncmn;

	return (true);
}

/*
 * cmap_lookup_hard --
 *	Look up the mapping for a character without using the cache.
 */
wint_t
cmap_lookup_hard(struct cmap *cm, wint_t ch)
{

	if (cm->cm_root != NULL) {
		cm->cm_root = cmap_splay(cm->cm_root, ch);
		if (cm->cm_root->cmn_from == ch)
			return (cm->cm_root->cmn_to);
	}
	return (cm->cm_def == CM_DEF_SELF ? ch : cm->cm_def);
}

/*
 * cmap_cache --
 *	Update the cache.
 */
void
cmap_cache(struct cmap *cm)
{
	wint_t ch;

	for (ch = 0; ch < CM_CACHE_SIZE; ch++)
		cm->cm_cache[ch] = cmap_lookup_hard(cm, ch);

	cm->cm_havecache = true;
}

/*
 * cmap_default --
 *	Change the value that characters without mappings map to, and
 *	return the old value. The special character value CM_MAP_SELF
 *	means characters map to themselves.
 */
wint_t
cmap_default(struct cmap *cm, wint_t def)
{
	wint_t old;

	old = cm->cm_def;
	cm->cm_def = def;
	cm->cm_havecache = false;
	return (old);
}

static struct cmapnode *
cmap_splay(struct cmapnode *t, wint_t ch)
{
	struct cmapnode N, *l, *r, *y;

	/*
	 * Based on public domain code from Sleator.
	 */

	assert(t != NULL);

	N.cmn_left = N.cmn_right = NULL;
	l = r = &N;
	for (;;) {
		if (ch < t->cmn_from) {
			if (t->cmn_left != NULL &&
			    ch < t->cmn_left->cmn_from) {
				y = t->cmn_left;
				t->cmn_left = y->cmn_right;
				y->cmn_right = t;
				t = y;
			}
			if (t->cmn_left == NULL)
				break;
			r->cmn_left = t;
			r = t;
			t = t->cmn_left;
		} else if (ch > t->cmn_from) {
			if (t->cmn_right != NULL &&
			    ch > t->cmn_right->cmn_from) {
				y = t->cmn_right;
				t->cmn_right = y->cmn_left;
				y->cmn_left = t;
				t = y;
			}
			if (t->cmn_right == NULL)
				break;
			l->cmn_right = t;
			l = t;
			t = t->cmn_right;
		} else
			break;
	}
	l->cmn_right = t->cmn_left;
	r->cmn_left = t->cmn_right;
	t->cmn_left = N.cmn_right;
	t->cmn_right = N.cmn_left;
	return (t);
}
