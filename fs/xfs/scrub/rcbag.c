// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_buf_mem.h"
#include "xfs_btree_mem.h"
#include "xfs_error.h"
#include "scrub/scrub.h"
#include "scrub/rcbag_btree.h"
#include "scrub/rcbag.h"
#include "scrub/trace.h"

struct rcbag {
	struct xfs_mount	*mp;
	struct xfbtree		xfbtree;
	uint64_t		nr_items;
};

int
rcbag_init(
	struct xfs_mount	*mp,
	struct xfs_buftarg	*btp,
	struct rcbag		**bagp)
{
	struct rcbag		*bag;
	int			error;

	bag = kzalloc(sizeof(struct rcbag), XCHK_GFP_FLAGS);
	if (!bag)
		return -ENOMEM;

	bag->nr_items = 0;
	bag->mp = mp;

	error = rcbagbt_mem_init(mp, &bag->xfbtree, btp);
	if (error)
		goto out_bag;

	*bagp = bag;
	return 0;

out_bag:
	kfree(bag);
	return error;
}

void
rcbag_free(
	struct rcbag		**bagp)
{
	struct rcbag		*bag = *bagp;

	xfbtree_destroy(&bag->xfbtree);
	kfree(bag);
	*bagp = NULL;
}

/* Track an rmap in the refcount bag. */
int
rcbag_add(
	struct rcbag			*bag,
	struct xfs_trans		*tp,
	const struct xfs_rmap_irec	*rmap)
{
	struct rcbag_rec		bagrec;
	struct xfs_mount		*mp = bag->mp;
	struct xfs_btree_cur		*cur;
	int				has;
	int				error;

	cur = rcbagbt_mem_cursor(mp, tp, &bag->xfbtree);
	error = rcbagbt_lookup_eq(cur, rmap, &has);
	if (error)
		goto out_cur;

	if (has) {
		error = rcbagbt_get_rec(cur, &bagrec, &has);
		if (error)
			goto out_cur;
		if (!has) {
			error = -EFSCORRUPTED;
			goto out_cur;
		}

		bagrec.rbg_refcount++;
		error = rcbagbt_update(cur, &bagrec);
		if (error)
			goto out_cur;
	} else {
		bagrec.rbg_startblock = rmap->rm_startblock;
		bagrec.rbg_blockcount = rmap->rm_blockcount;
		bagrec.rbg_refcount = 1;

		error = rcbagbt_insert(cur, &bagrec, &has);
		if (error)
			goto out_cur;
		if (!has) {
			error = -EFSCORRUPTED;
			goto out_cur;
		}
	}

	xfs_btree_del_cursor(cur, 0);

	error = xfbtree_trans_commit(&bag->xfbtree, tp);
	if (error)
		return error;

	bag->nr_items++;
	return 0;

out_cur:
	xfs_btree_del_cursor(cur, error);
	xfbtree_trans_cancel(&bag->xfbtree, tp);
	return error;
}

/* Return the number of records in the bag. */
uint64_t
rcbag_count(
	const struct rcbag	*rcbag)
{
	return rcbag->nr_items;
}

static inline uint32_t rcbag_rec_next_bno(const struct rcbag_rec *r)
{
	return r->rbg_startblock + r->rbg_blockcount;
}

/*
 * Find the next block where the refcount changes, given the next rmap we
 * looked at and the ones we're already tracking.
 */
int
rcbag_next_edge(
	struct rcbag			*bag,
	struct xfs_trans		*tp,
	const struct xfs_rmap_irec	*next_rmap,
	bool				next_valid,
	uint32_t			*next_bnop)
{
	struct rcbag_rec		bagrec;
	struct xfs_mount		*mp = bag->mp;
	struct xfs_btree_cur		*cur;
	uint32_t			next_bno = NULLAGBLOCK;
	int				has;
	int				error;

	if (next_valid)
		next_bno = next_rmap->rm_startblock;

	cur = rcbagbt_mem_cursor(mp, tp, &bag->xfbtree);
	error = xfs_btree_goto_left_edge(cur);
	if (error)
		goto out_cur;

	while (true) {
		error = xfs_btree_increment(cur, 0, &has);
		if (error)
			goto out_cur;
		if (!has)
			break;

		error = rcbagbt_get_rec(cur, &bagrec, &has);
		if (error)
			goto out_cur;
		if (!has) {
			error = -EFSCORRUPTED;
			goto out_cur;
		}

		next_bno = min(next_bno, rcbag_rec_next_bno(&bagrec));
	}

	/*
	 * We should have found /something/ because either next_rrm is the next
	 * interesting rmap to look at after emitting this refcount extent, or
	 * there are other rmaps in rmap_bag contributing to the current
	 * sharing count.  But if something is seriously wrong, bail out.
	 */
	if (next_bno == NULLAGBLOCK) {
		error = -EFSCORRUPTED;
		goto out_cur;
	}

	xfs_btree_del_cursor(cur, 0);

	*next_bnop = next_bno;
	return 0;

out_cur:
	xfs_btree_del_cursor(cur, error);
	return error;
}

/* Pop all refcount bag records that end at next_bno */
int
rcbag_remove_ending_at(
	struct rcbag		*bag,
	struct xfs_trans	*tp,
	uint32_t		next_bno)
{
	struct rcbag_rec	bagrec;
	struct xfs_mount	*mp = bag->mp;
	struct xfs_btree_cur	*cur;
	int			has;
	int			error;

	/* go to the right edge of the tree */
	cur = rcbagbt_mem_cursor(mp, tp, &bag->xfbtree);
	memset(&cur->bc_rec, 0xFF, sizeof(cur->bc_rec));
	error = xfs_btree_lookup(cur, XFS_LOOKUP_GE, &has);
	if (error)
		goto out_cur;

	while (true) {
		error = xfs_btree_decrement(cur, 0, &has);
		if (error)
			goto out_cur;
		if (!has)
			break;

		error = rcbagbt_get_rec(cur, &bagrec, &has);
		if (error)
			goto out_cur;
		if (!has) {
			error = -EFSCORRUPTED;
			goto out_cur;
		}

		if (rcbag_rec_next_bno(&bagrec) != next_bno)
			continue;

		error = xfs_btree_delete(cur, &has);
		if (error)
			goto out_cur;
		if (!has) {
			error = -EFSCORRUPTED;
			goto out_cur;
		}

		bag->nr_items -= bagrec.rbg_refcount;
	}

	xfs_btree_del_cursor(cur, 0);
	return xfbtree_trans_commit(&bag->xfbtree, tp);
out_cur:
	xfs_btree_del_cursor(cur, error);
	xfbtree_trans_cancel(&bag->xfbtree, tp);
	return error;
}

/* Dump the rcbag. */
void
rcbag_dump(
	struct rcbag			*bag,
	struct xfs_trans		*tp)
{
	struct rcbag_rec		bagrec;
	struct xfs_mount		*mp = bag->mp;
	struct xfs_btree_cur		*cur;
	unsigned long long		nr = 0;
	int				has;
	int				error;

	cur = rcbagbt_mem_cursor(mp, tp, &bag->xfbtree);
	error = xfs_btree_goto_left_edge(cur);
	if (error)
		goto out_cur;

	while (true) {
		error = xfs_btree_increment(cur, 0, &has);
		if (error)
			goto out_cur;
		if (!has)
			break;

		error = rcbagbt_get_rec(cur, &bagrec, &has);
		if (error)
			goto out_cur;
		if (!has) {
			error = -EFSCORRUPTED;
			goto out_cur;
		}

		xfs_err(bag->mp, "[%llu]: bno 0x%x fsbcount 0x%x refcount 0x%llx\n",
				nr++,
				(unsigned int)bagrec.rbg_startblock,
				(unsigned int)bagrec.rbg_blockcount,
				(unsigned long long)bagrec.rbg_refcount);
	}

out_cur:
	xfs_btree_del_cursor(cur, error);
}
