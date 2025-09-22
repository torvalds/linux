/*	$OpenBSD: tree.c,v 1.18 2017/02/13 19:13:14 krw Exp $ */

/* Routines for manipulating parse trees... */

/*
 * Copyright (c) 1995, 1996, 1997 The Internet Software Consortium.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The Internet Software Consortium nor the names
 *    of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE INTERNET SOFTWARE CONSORTIUM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This software has been written for the Internet Software Consortium
 * by Ted Lemon <mellon@fugue.com> in cooperation with Vixie
 * Enterprises.  To learn more about the Internet Software Consortium,
 * see ``http://www.vix.com/isc''.  To learn more about Vixie
 * Enterprises, see ``http://www.vix.com''.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <net/if.h>

#include <netinet/in.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dhcp.h"
#include "tree.h"
#include "dhcpd.h"
#include "log.h"

static time_t tree_evaluate_recurse(int *, unsigned char **, int *,
    struct tree *);
static void do_data_copy(int *, unsigned char **, int *, unsigned char *, int);

pair
cons(caddr_t car, pair cdr)
{
	pair foo;

	foo = calloc(1, sizeof *foo);
	if (!foo)
		fatalx("no memory for cons.");
	foo->car = car;
	foo->cdr = cdr;
	return foo;
}

struct tree_cache *
tree_cache(struct tree *tree)
{
	struct tree_cache	*tc;

	tc = new_tree_cache("tree_cache");
	if (!tc)
		return 0;
	tc->value = NULL;
	tc->len = tc->buf_size = 0;
	tc->timeout = 0;
	tc->tree = tree;
	return tc;
}

struct tree *
tree_const(unsigned char *data, int len)
{
	unsigned char *d;
	struct tree *nt;

	d = calloc(1, len);
	nt = calloc(1, sizeof(struct tree));
	if (!nt || !d)
		fatalx("No memory for constant data tree node.");

	memcpy(d, data, len);

	nt->op = TREE_CONST;
	nt->data.const_val.data = d;
	nt->data.const_val.len = len;

	return nt;
}

struct tree *
tree_concat(struct tree *left, struct tree *right)
{
	struct tree	*nt;

	/*
	 * If we're concatenating a null tree to a non-null tree, just
	 * return the non-null tree; if both trees are null, return
	 * a null tree.
	 */
	if (!left)
		return right;
	if (!right)
		return left;

	/* If both trees are constant, combine them. */
	if (left->op == TREE_CONST && right->op == TREE_CONST) {
		unsigned char *buf;

		buf = calloc(1, left->data.const_val.len
		    + right->data.const_val.len);

		if (!buf)
			fatalx("No memory to concatenate constants.");
		memcpy(buf, left->data.const_val.data,
		    left->data.const_val.len);
		memcpy(buf + left->data.const_val.len,
		    right->data.const_val.data, right->data.const_val.len);
		free(left->data.const_val.data);
		free(right->data.const_val.data);
		left->data.const_val.data = buf;
		left->data.const_val.len += right->data.const_val.len;
		free(right);
		return left;
	}

	/* Otherwise, allocate a new node to concatenate the two. */
	nt = calloc(1, sizeof(struct tree));
	if (!nt)
		fatalx("No memory for data tree concatenation node.");
	nt->op = TREE_CONCAT;
	nt->data.concat.left = left;
	nt->data.concat.right = right;
	return nt;
}

struct tree *
tree_limit(struct tree *tree, int limit)
{
	struct tree	*rv;

	/* If the tree we're limiting is constant, limit it now. */
	if (tree->op == TREE_CONST) {
		if (tree->data.const_val.len > limit)
			tree->data.const_val.len = limit;
		return tree;
	}

	/* Otherwise, put in a node which enforces the limit on evaluation. */
	rv = calloc(1, sizeof(struct tree));
	if (!rv) {
		log_warnx("No memory for data tree limit node.");
		return NULL;
	}
	rv->op = TREE_LIMIT;
	rv->data.limit.tree = tree;
	rv->data.limit.limit = limit;
	return rv;
}

int
tree_evaluate(struct tree_cache *tree_cache)
{
	unsigned char	*bp = tree_cache->value;
	int		 bc = tree_cache->buf_size;
	int		 bufix = 0;

	/*
	 * If there's no tree associated with this cache, it evaluates to a
	 * constant and that was detected at startup.
	 */
	if (!tree_cache->tree)
		return 1;

	/* Try to evaluate the tree without allocating more memory... */
	tree_cache->timeout = tree_evaluate_recurse(&bufix, &bp, &bc,
	    tree_cache->tree);

	/* No additional allocation needed? */
	if (bufix <= bc) {
		tree_cache->len = bufix;
		return 1;
	}

	/*
	 * If we can't allocate more memory, return with what we
	 * have (maybe nothing).
	 */
	bp = calloc(1, bufix);
	if (!bp) {
		log_warnx("no more memory for option data");
		return 0;
	}

	/* Record the change in conditions... */
	bc = bufix;
	bufix = 0;

	/*
	 * Note that the size of the result shouldn't change on the
	 * second call to tree_evaluate_recurse, since we haven't
	 * changed the ``current'' time.
	 */
	tree_evaluate_recurse(&bufix, &bp, &bc, tree_cache->tree);

	/*
	 * Free the old buffer if needed, then store the new buffer
	 * location and size and return.
	 */
	free(tree_cache->value);
	tree_cache->value = bp;
	tree_cache->len = bufix;
	tree_cache->buf_size = bc;
	return 1;
}

static time_t
tree_evaluate_recurse(int *bufix, unsigned char **bufp,
    int *bufcount, struct tree *tree)
{
	int	limit;
	time_t	t1, t2;

	switch (tree->op) {
	case TREE_CONCAT:
		t1 = tree_evaluate_recurse(bufix, bufp, bufcount,
		    tree->data.concat.left);
		t2 = tree_evaluate_recurse(bufix, bufp, bufcount,
		    tree->data.concat.right);
		if (t1 > t2)
			return t2;
		return t1;

	case TREE_CONST:
		do_data_copy(bufix, bufp, bufcount, tree->data.const_val.data,
		    tree->data.const_val.len);
		t1 = MAX_TIME;
		return t1;

	case TREE_LIMIT:
		limit = *bufix + tree->data.limit.limit;
		t1 = tree_evaluate_recurse(bufix, bufp, bufcount,
		    tree->data.limit.tree);
		*bufix = limit;
		return t1;

	default:
		log_warnx("Bad node id in tree: %d.", tree->op);
		t1 = MAX_TIME;
		return t1;
	}
}

static void
do_data_copy(int *bufix, unsigned char **bufp, int *bufcount,
    unsigned char *data, int len)
{
	int	space = *bufcount - *bufix;

	/* If there's more space than we need, use only what we need. */
	if (space > len)
		space = len;

	/*
	 * Copy as much data as will fit, then increment the buffer index
	 * by the amount we actually had to copy, which could be more.
	 */
	if (space > 0)
		memcpy(*bufp + *bufix, data, space);
	*bufix += len;
}
