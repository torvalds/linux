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
 * "Set of characters" ADT implemented as a splay tree of extents, with
 * a lookup table cache to simplify looking up the first bunch of
 * characters (which are presumably more common than others).
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "cset.h"

static struct csnode *	cset_delete(struct csnode *, wchar_t);
static __inline int	cset_rangecmp(struct csnode *, wchar_t);
static struct csnode *	cset_splay(struct csnode *, wchar_t);

/*
 * cset_alloc --
 *	Allocate a set of characters.
 */
struct cset *
cset_alloc(void)
{
	struct cset *cs;

	if ((cs = malloc(sizeof(*cs))) == NULL)
		return (NULL);
	cs->cs_root = NULL;
	cs->cs_classes = NULL;
	cs->cs_havecache = false;
	cs->cs_invert = false;
	return (cs);
}

/*
 * cset_add --
 *	Add a character to the set.
 */
bool
cset_add(struct cset *cs, wchar_t ch)
{
	struct csnode *csn, *ncsn;
	wchar_t oval;

	cs->cs_havecache = false;

	/*
	 * Inserting into empty tree; new item becomes the root.
	 */
	if (cs->cs_root == NULL) {
		csn = malloc(sizeof(*cs->cs_root));
		if (csn == NULL)
			return (false);
		csn->csn_left = csn->csn_right = NULL;
		csn->csn_min = csn->csn_max = ch;
		cs->cs_root = csn;
		return (true);
	}

	/*
	 * Splay to check whether the item already exists, and otherwise,
	 * where we should put it.
	 */
	csn = cs->cs_root = cset_splay(cs->cs_root, ch);

	/*
	 * Avoid adding duplicate nodes.
	 */
	if (cset_rangecmp(csn, ch) == 0)
		return (true);

	/*
	 * Allocate a new node and make it the new root.
	 */
	ncsn = malloc(sizeof(*ncsn));
	if (ncsn == NULL)
		return (false);
	ncsn->csn_min = ncsn->csn_max = ch;
	if (cset_rangecmp(csn, ch) < 0) {
		ncsn->csn_left = csn->csn_left;
		ncsn->csn_right = csn;
		csn->csn_left = NULL;
	} else {
		ncsn->csn_right = csn->csn_right;
		ncsn->csn_left = csn;
		csn->csn_right = NULL;
	}
	cs->cs_root = ncsn;

	/*
	 * Coalesce with left and right neighbours if possible.
	 */
	if (ncsn->csn_left != NULL) {
		ncsn->csn_left = cset_splay(ncsn->csn_left, ncsn->csn_min - 1);
		if (ncsn->csn_left->csn_max == ncsn->csn_min - 1) {
			oval = ncsn->csn_left->csn_min;
			ncsn->csn_left = cset_delete(ncsn->csn_left,
			    ncsn->csn_left->csn_min);
			ncsn->csn_min = oval;
		}
	}
	if (ncsn->csn_right != NULL) {
		ncsn->csn_right = cset_splay(ncsn->csn_right,
		    ncsn->csn_max + 1);
		if (ncsn->csn_right->csn_min == ncsn->csn_max + 1) {
			oval = ncsn->csn_right->csn_max;
			ncsn->csn_right = cset_delete(ncsn->csn_right,
			    ncsn->csn_right->csn_min);
			ncsn->csn_max = oval;
		}
	}

	return (true);
}

/*
 * cset_in_hard --
 *	Determine whether a character is in the set without using
 *	the cache.
 */
bool
cset_in_hard(struct cset *cs, wchar_t ch)
{
	struct csclass *csc;

	for (csc = cs->cs_classes; csc != NULL; csc = csc->csc_next)
		if (csc->csc_invert ^ (iswctype(ch, csc->csc_type) != 0))
			return (cs->cs_invert ^ true);
	if (cs->cs_root != NULL) {
		cs->cs_root = cset_splay(cs->cs_root, ch);
		return (cs->cs_invert ^ (cset_rangecmp(cs->cs_root, ch) == 0));
	}
	return (cs->cs_invert ^ false);
}

/*
 * cset_cache --
 *	Update the cache.
 */
void
cset_cache(struct cset *cs)
{
	wchar_t i;

	for (i = 0; i < CS_CACHE_SIZE; i++)
		cs->cs_cache[i] = cset_in_hard(cs, i);

	cs->cs_havecache = true;
}

/*
 * cset_invert --
 *	Invert the character set.
 */
void
cset_invert(struct cset *cs)
{

	cs->cs_invert ^= true;
	cs->cs_havecache = false;
}

/*
 * cset_addclass --
 *	Add a wctype()-style character class to the set, optionally
 *	inverting it.
 */
bool
cset_addclass(struct cset *cs, wctype_t type, bool invert)
{
	struct csclass *csc;

	csc = malloc(sizeof(*csc));
	if (csc == NULL)
		return (false);
	csc->csc_type = type;
	csc->csc_invert = invert;
	csc->csc_next = cs->cs_classes;
	cs->cs_classes = csc;
	cs->cs_havecache = false;
	return (true);
}

static __inline int
cset_rangecmp(struct csnode *t, wchar_t ch)
{

	if (ch < t->csn_min)
		return (-1);
	if (ch > t->csn_max)
		return (1);
	return (0);
}

static struct csnode *
cset_splay(struct csnode *t, wchar_t ch)
{
	struct csnode N, *l, *r, *y;

	/*
	 * Based on public domain code from Sleator.
	 */

	assert(t != NULL);

	N.csn_left = N.csn_right = NULL;
	l = r = &N;
	for (;;) {
		if (cset_rangecmp(t, ch) < 0) {
			if (t->csn_left != NULL &&
			    cset_rangecmp(t->csn_left, ch) < 0) {
				y = t->csn_left;
				t->csn_left = y->csn_right;
				y->csn_right = t;
				t = y;
			}
			if (t->csn_left == NULL)
				break;
			r->csn_left = t;
			r = t;
			t = t->csn_left;
		} else if (cset_rangecmp(t, ch) > 0) {
			if (t->csn_right != NULL &&
			    cset_rangecmp(t->csn_right, ch) > 0) {
				y = t->csn_right;
				t->csn_right = y->csn_left;
				y->csn_left = t;
				t = y;
			}
			if (t->csn_right == NULL)
				break;
			l->csn_right = t;
			l = t;
			t = t->csn_right;
		} else
			break;
	}
	l->csn_right = t->csn_left;
	r->csn_left = t->csn_right;
	t->csn_left = N.csn_right;
	t->csn_right = N.csn_left;
	return (t);
}

static struct csnode *
cset_delete(struct csnode *t, wchar_t ch)
{
	struct csnode *x;

	assert(t != NULL);
	t = cset_splay(t, ch);
	assert(cset_rangecmp(t, ch) == 0);
	if (t->csn_left == NULL)
		x = t->csn_right;
	else {
		x = cset_splay(t->csn_left, ch);
		x->csn_right = t->csn_right;
	}
	free(t);
	return x;
}
