/*-
 * Copyright (c) 2015 Nuxi, https://nuxi.nl/
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define	_SEARCH_PRIVATE
#include <search.h>
#include <stdbool.h>
#include <stdlib.h>

#include "tsearch_path.h"

/*
 * Makes a step to the left along the binary search tree. This step is
 * also saved, so it can be replayed while rebalancing.
*/
#define	GO_LEFT() do {							\
	if ((*leaf)->balance == 0 ||					\
	    ((*leaf)->balance < 0 && (*leaf)->rlink->balance == 0)) {	\
		/*							\
		 * If we reach a node that is balanced, or has a child	\
		 * in the opposite direction that is balanced, we know	\
		 * that we won't need to perform any rotations above	\
		 * this point. In this case rotations are always	\
		 * capable of keeping the subtree in balance. Make	\
		 * this the root node and reset the path.		\
		 */							\
		rootp = leaf;						\
		path_init(&path);					\
	}								\
	path_taking_left(&path);					\
	leaf = &(*leaf)->llink;						\
} while (0)

/* Makes a step to the right along the binary search tree. */
#define	GO_RIGHT() do {							\
	if ((*leaf)->balance == 0 ||					\
	    ((*leaf)->balance > 0 && (*leaf)->llink->balance == 0)) {	\
		rootp = leaf;						\
		path_init(&path);					\
	}								\
	path_taking_right(&path);					\
	leaf = &(*leaf)->rlink;						\
} while (0)

void *
tdelete(const void *restrict key, posix_tnode **restrict rootp,
    int (*compar)(const void *, const void *))
{
	struct path path;
	posix_tnode **leaf, *old, **n, *x, *y, *z, *result;
	int cmp;

	/* POSIX requires that tdelete() returns NULL if rootp is NULL. */
	if (rootp == NULL)
		return (NULL);

	/*
	 * Find the leaf that needs to be removed. Return if we cannot
	 * find an existing entry. Keep track of the path that is taken
	 * to get to the node, as we will need it to adjust the
	 * balances.
	 */
	result = (posix_tnode *)1;
	path_init(&path);
	leaf = rootp;
	for (;;) {
		if (*leaf == NULL)
			return (NULL);
		cmp = compar(key, (*leaf)->key);
		if (cmp < 0) {
			result = *leaf;
			GO_LEFT();
		} else if (cmp > 0) {
			result = *leaf;
			GO_RIGHT();
		} else {
			break;
		}
	}

	/* Found a matching key in the tree. Remove the node. */
	if ((*leaf)->llink == NULL) {
		/* Node has no left children. Replace by its right subtree. */
		old = *leaf;
		*leaf = old->rlink;
		free(old);
	} else {
		/*
		 * Node has left children. Replace this node's key by
		 * its predecessor's and remove that node instead.
		 */
		void **keyp = &(*leaf)->key;
		GO_LEFT();
		while ((*leaf)->rlink != NULL)
			GO_RIGHT();
		old = *leaf;
		*keyp = old->key;
		*leaf = old->llink;
		free(old);
	}

	/*
	 * Walk along the same path a second time and adjust the
	 * balances. Though this code looks similar to the rebalancing
	 * performed in tsearch(), it is not identical. We now also need
	 * to consider the case of outward imbalance in the right-right
	 * and left-left case that only exists when deleting. Hence the
	 * duplication of code.
	 */
	for (n = rootp; n != leaf;) {
		if (path_took_left(&path)) {
			x = *n;
			if (x->balance < 0) {
				y = x->rlink;
				if (y->balance > 0) {
					/* Right-left case. */
					z = y->llink;
					x->rlink = z->llink;
					z->llink = x;
					y->llink = z->rlink;
					z->rlink = y;
					*n = z;

					x->balance = z->balance < 0 ? 1 : 0;
					y->balance = z->balance > 0 ? -1 : 0;
					z->balance = 0;
				} else {
					/* Right-right case. */
					x->rlink = y->llink;
					y->llink = x;
					*n = y;

					if (y->balance < 0) {
						x->balance = 0;
						y->balance = 0;
					} else {
						x->balance = -1;
						y->balance = 1;
					}
				}
			} else {
				--x->balance;
			}
			n = &x->llink;
		} else {
			x = *n;
			if (x->balance > 0) {
				y = x->llink;
				if (y->balance < 0) {
					/* Left-right case. */
					z = y->rlink;
					y->rlink = z->llink;
					z->llink = y;
					x->llink = z->rlink;
					z->rlink = x;
					*n = z;

					x->balance = z->balance > 0 ? -1 : 0;
					y->balance = z->balance < 0 ? 1 : 0;
					z->balance = 0;
				} else {
					/* Left-left case. */
					x->llink = y->rlink;
					y->rlink = x;
					*n = y;

					if (y->balance > 0) {
						x->balance = 0;
						y->balance = 0;
					} else {
						x->balance = 1;
						y->balance = -1;
					}
				}
			} else {
				++x->balance;
			}
			n = &x->rlink;
		}
	}

	/* Return the parent of the old entry. */
	return (result);
}
