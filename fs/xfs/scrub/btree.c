/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_alloc.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"

/* btree scrubbing */

/*
 * Check for btree operation errors.  See the section about handling
 * operational errors in common.c.
 */
bool
xfs_scrub_btree_process_error(
	struct xfs_scrub_context	*sc,
	struct xfs_btree_cur		*cur,
	int				level,
	int				*error)
{
	if (*error == 0)
		return true;

	switch (*error) {
	case -EDEADLOCK:
		/* Used to restart an op with deadlock avoidance. */
		trace_xfs_scrub_deadlock_retry(sc->ip, sc->sm, *error);
		break;
	case -EFSBADCRC:
	case -EFSCORRUPTED:
		/* Note the badness but don't abort. */
		sc->sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;
		*error = 0;
		/* fall through */
	default:
		if (cur->bc_flags & XFS_BTREE_ROOT_IN_INODE)
			trace_xfs_scrub_ifork_btree_op_error(sc, cur, level,
					*error, __return_address);
		else
			trace_xfs_scrub_btree_op_error(sc, cur, level,
					*error, __return_address);
		break;
	}
	return false;
}

/* Record btree block corruption. */
void
xfs_scrub_btree_set_corrupt(
	struct xfs_scrub_context	*sc,
	struct xfs_btree_cur		*cur,
	int				level)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;

	if (cur->bc_flags & XFS_BTREE_ROOT_IN_INODE)
		trace_xfs_scrub_ifork_btree_error(sc, cur, level,
				__return_address);
	else
		trace_xfs_scrub_btree_error(sc, cur, level,
				__return_address);
}

/*
 * Make sure this record is in order and doesn't stray outside of the parent
 * keys.
 */
STATIC void
xfs_scrub_btree_rec(
	struct xfs_scrub_btree	*bs)
{
	struct xfs_btree_cur	*cur = bs->cur;
	union xfs_btree_rec	*rec;
	union xfs_btree_key	key;
	union xfs_btree_key	hkey;
	union xfs_btree_key	*keyp;
	struct xfs_btree_block	*block;
	struct xfs_btree_block	*keyblock;
	struct xfs_buf		*bp;

	block = xfs_btree_get_block(cur, 0, &bp);
	rec = xfs_btree_rec_addr(cur, cur->bc_ptrs[0], block);

	trace_xfs_scrub_btree_rec(bs->sc, cur, 0);

	/* If this isn't the first record, are they in order? */
	if (!bs->firstrec && !cur->bc_ops->recs_inorder(cur, &bs->lastrec, rec))
		xfs_scrub_btree_set_corrupt(bs->sc, cur, 0);
	bs->firstrec = false;
	memcpy(&bs->lastrec, rec, cur->bc_ops->rec_len);

	if (cur->bc_nlevels == 1)
		return;

	/* Is this at least as large as the parent low key? */
	cur->bc_ops->init_key_from_rec(&key, rec);
	keyblock = xfs_btree_get_block(cur, 1, &bp);
	keyp = xfs_btree_key_addr(cur, cur->bc_ptrs[1], keyblock);
	if (cur->bc_ops->diff_two_keys(cur, &key, keyp) < 0)
		xfs_scrub_btree_set_corrupt(bs->sc, cur, 1);

	if (!(cur->bc_flags & XFS_BTREE_OVERLAPPING))
		return;

	/* Is this no larger than the parent high key? */
	cur->bc_ops->init_high_key_from_rec(&hkey, rec);
	keyp = xfs_btree_high_key_addr(cur, cur->bc_ptrs[1], keyblock);
	if (cur->bc_ops->diff_two_keys(cur, keyp, &hkey) < 0)
		xfs_scrub_btree_set_corrupt(bs->sc, cur, 1);
}

/*
 * Make sure this key is in order and doesn't stray outside of the parent
 * keys.
 */
STATIC void
xfs_scrub_btree_key(
	struct xfs_scrub_btree	*bs,
	int			level)
{
	struct xfs_btree_cur	*cur = bs->cur;
	union xfs_btree_key	*key;
	union xfs_btree_key	*keyp;
	struct xfs_btree_block	*block;
	struct xfs_btree_block	*keyblock;
	struct xfs_buf		*bp;

	block = xfs_btree_get_block(cur, level, &bp);
	key = xfs_btree_key_addr(cur, cur->bc_ptrs[level], block);

	trace_xfs_scrub_btree_key(bs->sc, cur, level);

	/* If this isn't the first key, are they in order? */
	if (!bs->firstkey[level] &&
	    !cur->bc_ops->keys_inorder(cur, &bs->lastkey[level], key))
		xfs_scrub_btree_set_corrupt(bs->sc, cur, level);
	bs->firstkey[level] = false;
	memcpy(&bs->lastkey[level], key, cur->bc_ops->key_len);

	if (level + 1 >= cur->bc_nlevels)
		return;

	/* Is this at least as large as the parent low key? */
	keyblock = xfs_btree_get_block(cur, level + 1, &bp);
	keyp = xfs_btree_key_addr(cur, cur->bc_ptrs[level + 1], keyblock);
	if (cur->bc_ops->diff_two_keys(cur, key, keyp) < 0)
		xfs_scrub_btree_set_corrupt(bs->sc, cur, level);

	if (!(cur->bc_flags & XFS_BTREE_OVERLAPPING))
		return;

	/* Is this no larger than the parent high key? */
	key = xfs_btree_high_key_addr(cur, cur->bc_ptrs[level], block);
	keyp = xfs_btree_high_key_addr(cur, cur->bc_ptrs[level + 1], keyblock);
	if (cur->bc_ops->diff_two_keys(cur, keyp, key) < 0)
		xfs_scrub_btree_set_corrupt(bs->sc, cur, level);
}

/*
 * Check a btree pointer.  Returns true if it's ok to use this pointer.
 * Callers do not need to set the corrupt flag.
 */
static bool
xfs_scrub_btree_ptr_ok(
	struct xfs_scrub_btree		*bs,
	int				level,
	union xfs_btree_ptr		*ptr)
{
	bool				res;

	/* A btree rooted in an inode has no block pointer to the root. */
	if ((bs->cur->bc_flags & XFS_BTREE_ROOT_IN_INODE) &&
	    level == bs->cur->bc_nlevels)
		return true;

	/* Otherwise, check the pointers. */
	if (bs->cur->bc_flags & XFS_BTREE_LONG_PTRS)
		res = xfs_btree_check_lptr(bs->cur, be64_to_cpu(ptr->l), level);
	else
		res = xfs_btree_check_sptr(bs->cur, be32_to_cpu(ptr->s), level);
	if (!res)
		xfs_scrub_btree_set_corrupt(bs->sc, bs->cur, level);

	return res;
}

/* Check that a btree block's sibling matches what we expect it. */
STATIC int
xfs_scrub_btree_block_check_sibling(
	struct xfs_scrub_btree		*bs,
	int				level,
	int				direction,
	union xfs_btree_ptr		*sibling)
{
	struct xfs_btree_cur		*cur = bs->cur;
	struct xfs_btree_block		*pblock;
	struct xfs_buf			*pbp;
	struct xfs_btree_cur		*ncur = NULL;
	union xfs_btree_ptr		*pp;
	int				success;
	int				error;

	error = xfs_btree_dup_cursor(cur, &ncur);
	if (!xfs_scrub_btree_process_error(bs->sc, cur, level + 1, &error) ||
	    !ncur)
		return error;

	/*
	 * If the pointer is null, we shouldn't be able to move the upper
	 * level pointer anywhere.
	 */
	if (xfs_btree_ptr_is_null(cur, sibling)) {
		if (direction > 0)
			error = xfs_btree_increment(ncur, level + 1, &success);
		else
			error = xfs_btree_decrement(ncur, level + 1, &success);
		if (error == 0 && success)
			xfs_scrub_btree_set_corrupt(bs->sc, cur, level);
		error = 0;
		goto out;
	}

	/* Increment upper level pointer. */
	if (direction > 0)
		error = xfs_btree_increment(ncur, level + 1, &success);
	else
		error = xfs_btree_decrement(ncur, level + 1, &success);
	if (!xfs_scrub_btree_process_error(bs->sc, cur, level + 1, &error))
		goto out;
	if (!success) {
		xfs_scrub_btree_set_corrupt(bs->sc, cur, level + 1);
		goto out;
	}

	/* Compare upper level pointer to sibling pointer. */
	pblock = xfs_btree_get_block(ncur, level + 1, &pbp);
	pp = xfs_btree_ptr_addr(ncur, ncur->bc_ptrs[level + 1], pblock);
	if (!xfs_scrub_btree_ptr_ok(bs, level + 1, pp))
		goto out;

	if (xfs_btree_diff_two_ptrs(cur, pp, sibling))
		xfs_scrub_btree_set_corrupt(bs->sc, cur, level);
out:
	xfs_btree_del_cursor(ncur, XFS_BTREE_ERROR);
	return error;
}

/* Check the siblings of a btree block. */
STATIC int
xfs_scrub_btree_block_check_siblings(
	struct xfs_scrub_btree		*bs,
	struct xfs_btree_block		*block)
{
	struct xfs_btree_cur		*cur = bs->cur;
	union xfs_btree_ptr		leftsib;
	union xfs_btree_ptr		rightsib;
	int				level;
	int				error = 0;

	xfs_btree_get_sibling(cur, block, &leftsib, XFS_BB_LEFTSIB);
	xfs_btree_get_sibling(cur, block, &rightsib, XFS_BB_RIGHTSIB);
	level = xfs_btree_get_level(block);

	/* Root block should never have siblings. */
	if (level == cur->bc_nlevels - 1) {
		if (!xfs_btree_ptr_is_null(cur, &leftsib) ||
		    !xfs_btree_ptr_is_null(cur, &rightsib))
			xfs_scrub_btree_set_corrupt(bs->sc, cur, level);
		goto out;
	}

	/*
	 * Does the left & right sibling pointers match the adjacent
	 * parent level pointers?
	 * (These function absorbs error codes for us.)
	 */
	error = xfs_scrub_btree_block_check_sibling(bs, level, -1, &leftsib);
	if (error)
		return error;
	error = xfs_scrub_btree_block_check_sibling(bs, level, 1, &rightsib);
	if (error)
		return error;
out:
	return error;
}

/*
 * Grab and scrub a btree block given a btree pointer.  Returns block
 * and buffer pointers (if applicable) if they're ok to use.
 */
STATIC int
xfs_scrub_btree_get_block(
	struct xfs_scrub_btree		*bs,
	int				level,
	union xfs_btree_ptr		*pp,
	struct xfs_btree_block		**pblock,
	struct xfs_buf			**pbp)
{
	void				*failed_at;
	int				error;

	*pblock = NULL;
	*pbp = NULL;

	error = xfs_btree_lookup_get_block(bs->cur, level, pp, pblock);
	if (!xfs_scrub_btree_process_error(bs->sc, bs->cur, level, &error) ||
	    !*pblock)
		return error;

	xfs_btree_get_block(bs->cur, level, pbp);
	if (bs->cur->bc_flags & XFS_BTREE_LONG_PTRS)
		failed_at = __xfs_btree_check_lblock(bs->cur, *pblock,
				level, *pbp);
	else
		failed_at = __xfs_btree_check_sblock(bs->cur, *pblock,
				 level, *pbp);
	if (failed_at) {
		xfs_scrub_btree_set_corrupt(bs->sc, bs->cur, level);
		return 0;
	}

	/*
	 * Check the block's siblings; this function absorbs error codes
	 * for us.
	 */
	return xfs_scrub_btree_block_check_siblings(bs, *pblock);
}

/*
 * Check that the low and high keys of this block match the keys stored
 * in the parent block.
 */
STATIC void
xfs_scrub_btree_block_keys(
	struct xfs_scrub_btree		*bs,
	int				level,
	struct xfs_btree_block		*block)
{
	union xfs_btree_key		block_keys;
	struct xfs_btree_cur		*cur = bs->cur;
	union xfs_btree_key		*high_bk;
	union xfs_btree_key		*parent_keys;
	union xfs_btree_key		*high_pk;
	struct xfs_btree_block		*parent_block;
	struct xfs_buf			*bp;

	if (level >= cur->bc_nlevels - 1)
		return;

	/* Calculate the keys for this block. */
	xfs_btree_get_keys(cur, block, &block_keys);

	/* Obtain the parent's copy of the keys for this block. */
	parent_block = xfs_btree_get_block(cur, level + 1, &bp);
	parent_keys = xfs_btree_key_addr(cur, cur->bc_ptrs[level + 1],
			parent_block);

	if (cur->bc_ops->diff_two_keys(cur, &block_keys, parent_keys) != 0)
		xfs_scrub_btree_set_corrupt(bs->sc, cur, 1);

	if (!(cur->bc_flags & XFS_BTREE_OVERLAPPING))
		return;

	/* Get high keys */
	high_bk = xfs_btree_high_key_from_key(cur, &block_keys);
	high_pk = xfs_btree_high_key_addr(cur, cur->bc_ptrs[level + 1],
			parent_block);

	if (cur->bc_ops->diff_two_keys(cur, high_bk, high_pk) != 0)
		xfs_scrub_btree_set_corrupt(bs->sc, cur, 1);
}

/*
 * Visit all nodes and leaves of a btree.  Check that all pointers and
 * records are in order, that the keys reflect the records, and use a callback
 * so that the caller can verify individual records.
 */
int
xfs_scrub_btree(
	struct xfs_scrub_context	*sc,
	struct xfs_btree_cur		*cur,
	xfs_scrub_btree_rec_fn		scrub_fn,
	struct xfs_owner_info		*oinfo,
	void				*private)
{
	struct xfs_scrub_btree		bs = { NULL };
	union xfs_btree_ptr		ptr;
	union xfs_btree_ptr		*pp;
	union xfs_btree_rec		*recp;
	struct xfs_btree_block		*block;
	int				level;
	struct xfs_buf			*bp;
	int				i;
	int				error = 0;

	/* Initialize scrub state */
	bs.cur = cur;
	bs.scrub_rec = scrub_fn;
	bs.oinfo = oinfo;
	bs.firstrec = true;
	bs.private = private;
	bs.sc = sc;
	for (i = 0; i < XFS_BTREE_MAXLEVELS; i++)
		bs.firstkey[i] = true;
	INIT_LIST_HEAD(&bs.to_check);

	/* Don't try to check a tree with a height we can't handle. */
	if (cur->bc_nlevels > XFS_BTREE_MAXLEVELS) {
		xfs_scrub_btree_set_corrupt(sc, cur, 0);
		goto out;
	}

	/*
	 * Load the root of the btree.  The helper function absorbs
	 * error codes for us.
	 */
	level = cur->bc_nlevels - 1;
	cur->bc_ops->init_ptr_from_cur(cur, &ptr);
	if (!xfs_scrub_btree_ptr_ok(&bs, cur->bc_nlevels, &ptr))
		goto out;
	error = xfs_scrub_btree_get_block(&bs, level, &ptr, &block, &bp);
	if (error || !block)
		goto out;

	cur->bc_ptrs[level] = 1;

	while (level < cur->bc_nlevels) {
		block = xfs_btree_get_block(cur, level, &bp);

		if (level == 0) {
			/* End of leaf, pop back towards the root. */
			if (cur->bc_ptrs[level] >
			    be16_to_cpu(block->bb_numrecs)) {
				xfs_scrub_btree_block_keys(&bs, level, block);
				if (level < cur->bc_nlevels - 1)
					cur->bc_ptrs[level + 1]++;
				level++;
				continue;
			}

			/* Records in order for scrub? */
			xfs_scrub_btree_rec(&bs);

			/* Call out to the record checker. */
			recp = xfs_btree_rec_addr(cur, cur->bc_ptrs[0], block);
			error = bs.scrub_rec(&bs, recp);
			if (error)
				break;
			if (xfs_scrub_should_terminate(sc, &error) ||
			    (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
				break;

			cur->bc_ptrs[level]++;
			continue;
		}

		/* End of node, pop back towards the root. */
		if (cur->bc_ptrs[level] > be16_to_cpu(block->bb_numrecs)) {
			xfs_scrub_btree_block_keys(&bs, level, block);
			if (level < cur->bc_nlevels - 1)
				cur->bc_ptrs[level + 1]++;
			level++;
			continue;
		}

		/* Keys in order for scrub? */
		xfs_scrub_btree_key(&bs, level);

		/* Drill another level deeper. */
		pp = xfs_btree_ptr_addr(cur, cur->bc_ptrs[level], block);
		if (!xfs_scrub_btree_ptr_ok(&bs, level, pp)) {
			cur->bc_ptrs[level]++;
			continue;
		}
		level--;
		error = xfs_scrub_btree_get_block(&bs, level, pp, &block, &bp);
		if (error || !block)
			goto out;

		cur->bc_ptrs[level] = 1;
	}

out:
	return error;
}
