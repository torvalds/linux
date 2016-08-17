/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/range_tree.h>
#include <sys/space_reftree.h>

/*
 * Space reference trees.
 *
 * A range tree is a collection of integers.  Every integer is either
 * in the tree, or it's not.  A space reference tree generalizes
 * the idea: it allows its members to have arbitrary reference counts,
 * as opposed to the implicit reference count of 0 or 1 in a range tree.
 * This representation comes in handy when computing the union or
 * intersection of multiple space maps.  For example, the union of
 * N range trees is the subset of the reference tree with refcnt >= 1.
 * The intersection of N range trees is the subset with refcnt >= N.
 *
 * [It's very much like a Fourier transform.  Unions and intersections
 * are hard to perform in the 'range tree domain', so we convert the trees
 * into the 'reference count domain', where it's trivial, then invert.]
 *
 * vdev_dtl_reassess() uses computations of this form to determine
 * DTL_MISSING and DTL_OUTAGE for interior vdevs -- e.g. a RAID-Z vdev
 * has an outage wherever refcnt >= vdev_nparity + 1, and a mirror vdev
 * has an outage wherever refcnt >= vdev_children.
 */
static int
space_reftree_compare(const void *x1, const void *x2)
{
	const space_ref_t *sr1 = x1;
	const space_ref_t *sr2 = x2;

	if (sr1->sr_offset < sr2->sr_offset)
		return (-1);
	if (sr1->sr_offset > sr2->sr_offset)
		return (1);

	if (sr1 < sr2)
		return (-1);
	if (sr1 > sr2)
		return (1);

	return (0);
}

void
space_reftree_create(avl_tree_t *t)
{
	avl_create(t, space_reftree_compare,
	    sizeof (space_ref_t), offsetof(space_ref_t, sr_node));
}

void
space_reftree_destroy(avl_tree_t *t)
{
	space_ref_t *sr;
	void *cookie = NULL;

	while ((sr = avl_destroy_nodes(t, &cookie)) != NULL)
		kmem_free(sr, sizeof (*sr));

	avl_destroy(t);
}

static void
space_reftree_add_node(avl_tree_t *t, uint64_t offset, int64_t refcnt)
{
	space_ref_t *sr;

	sr = kmem_alloc(sizeof (*sr), KM_SLEEP);
	sr->sr_offset = offset;
	sr->sr_refcnt = refcnt;

	avl_add(t, sr);
}

void
space_reftree_add_seg(avl_tree_t *t, uint64_t start, uint64_t end,
	int64_t refcnt)
{
	space_reftree_add_node(t, start, refcnt);
	space_reftree_add_node(t, end, -refcnt);
}

/*
 * Convert (or add) a range tree into a reference tree.
 */
void
space_reftree_add_map(avl_tree_t *t, range_tree_t *rt, int64_t refcnt)
{
	range_seg_t *rs;

	ASSERT(MUTEX_HELD(rt->rt_lock));

	for (rs = avl_first(&rt->rt_root); rs; rs = AVL_NEXT(&rt->rt_root, rs))
		space_reftree_add_seg(t, rs->rs_start, rs->rs_end, refcnt);
}

/*
 * Convert a reference tree into a range tree.  The range tree will contain
 * all members of the reference tree for which refcnt >= minref.
 */
void
space_reftree_generate_map(avl_tree_t *t, range_tree_t *rt, int64_t minref)
{
	uint64_t start = -1ULL;
	int64_t refcnt = 0;
	space_ref_t *sr;

	ASSERT(MUTEX_HELD(rt->rt_lock));

	range_tree_vacate(rt, NULL, NULL);

	for (sr = avl_first(t); sr != NULL; sr = AVL_NEXT(t, sr)) {
		refcnt += sr->sr_refcnt;
		if (refcnt >= minref) {
			if (start == -1ULL) {
				start = sr->sr_offset;
			}
		} else {
			if (start != -1ULL) {
				uint64_t end = sr->sr_offset;
				ASSERT(start <= end);
				if (end > start)
					range_tree_add(rt, start, end - start);
				start = -1ULL;
			}
		}
	}
	ASSERT(refcnt == 0);
	ASSERT(start == -1ULL);
}
