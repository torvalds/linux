// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"

/* btree scrubbing */

/*
 * Check for btree operation errors.  See the section about handling
 * operational errors in common.c.
 */
static bool
__xchk_btree_process_error(
	struct xfs_scrub	*sc,
	struct xfs_btree_cur	*cur,
	int			level,
	int			*error,
	__u32			errflag,
	void			*ret_ip)
{
	if (*error == 0)
		return true;

	switch (*error) {
	case -EDEADLOCK:
	case -ECHRNG:
		/* Used to restart an op with deadlock avoidance. */
		trace_xchk_deadlock_retry(sc->ip, sc->sm, *error);
		break;
	case -EFSBADCRC:
	case -EFSCORRUPTED:
		/* Note the badness but don't abort. */
		sc->sm->sm_flags |= errflag;
		*error = 0;
		fallthrough;
	default:
		if (cur->bc_ops->type == XFS_BTREE_TYPE_INODE)
			trace_xchk_ifork_btree_op_error(sc, cur, level,
					*error, ret_ip);
		else
			trace_xchk_btree_op_error(sc, cur, level,
					*error, ret_ip);
		break;
	}
	return false;
}

bool
xchk_btree_process_error(
	struct xfs_scrub	*sc,
	struct xfs_btree_cur	*cur,
	int			level,
	int			*error)
{
	return __xchk_btree_process_error(sc, cur, level, error,
			XFS_SCRUB_OFLAG_CORRUPT, __return_address);
}

bool
xchk_btree_xref_process_error(
	struct xfs_scrub	*sc,
	struct xfs_btree_cur	*cur,
	int			level,
	int			*error)
{
	return __xchk_btree_process_error(sc, cur, level, error,
			XFS_SCRUB_OFLAG_XFAIL, __return_address);
}

/* Record btree block corruption. */
static void
__xchk_btree_set_corrupt(
	struct xfs_scrub	*sc,
	struct xfs_btree_cur	*cur,
	int			level,
	__u32			errflag,
	void			*ret_ip)
{
	sc->sm->sm_flags |= errflag;

	if (cur->bc_ops->type == XFS_BTREE_TYPE_INODE)
		trace_xchk_ifork_btree_error(sc, cur, level,
				ret_ip);
	else
		trace_xchk_btree_error(sc, cur, level,
				ret_ip);
}

void
xchk_btree_set_corrupt(
	struct xfs_scrub	*sc,
	struct xfs_btree_cur	*cur,
	int			level)
{
	__xchk_btree_set_corrupt(sc, cur, level, XFS_SCRUB_OFLAG_CORRUPT,
			__return_address);
}

void
xchk_btree_xref_set_corrupt(
	struct xfs_scrub	*sc,
	struct xfs_btree_cur	*cur,
	int			level)
{
	__xchk_btree_set_corrupt(sc, cur, level, XFS_SCRUB_OFLAG_XCORRUPT,
			__return_address);
}

void
xchk_btree_set_preen(
	struct xfs_scrub	*sc,
	struct xfs_btree_cur	*cur,
	int			level)
{
	__xchk_btree_set_corrupt(sc, cur, level, XFS_SCRUB_OFLAG_PREEN,
			__return_address);
}

/*
 * Make sure this record is in order and doesn't stray outside of the parent
 * keys.
 */
STATIC void
xchk_btree_rec(
	struct xchk_btree	*bs)
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
	rec = xfs_btree_rec_addr(cur, cur->bc_levels[0].ptr, block);

	trace_xchk_btree_rec(bs->sc, cur, 0);

	/* Are all records across all record blocks in order? */
	if (bs->lastrec_valid &&
	    !cur->bc_ops->recs_inorder(cur, &bs->lastrec, rec))
		xchk_btree_set_corrupt(bs->sc, cur, 0);
	memcpy(&bs->lastrec, rec, cur->bc_ops->rec_len);
	bs->lastrec_valid = true;

	if (cur->bc_nlevels == 1)
		return;

	/* Is low_key(rec) at least as large as the parent low key? */
	cur->bc_ops->init_key_from_rec(&key, rec);
	keyblock = xfs_btree_get_block(cur, 1, &bp);
	keyp = xfs_btree_key_addr(cur, cur->bc_levels[1].ptr, keyblock);
	if (xfs_btree_keycmp_lt(cur, &key, keyp))
		xchk_btree_set_corrupt(bs->sc, cur, 1);

	if (!(cur->bc_ops->geom_flags & XFS_BTGEO_OVERLAPPING))
		return;

	/* Is high_key(rec) no larger than the parent high key? */
	cur->bc_ops->init_high_key_from_rec(&hkey, rec);
	keyp = xfs_btree_high_key_addr(cur, cur->bc_levels[1].ptr, keyblock);
	if (xfs_btree_keycmp_lt(cur, keyp, &hkey))
		xchk_btree_set_corrupt(bs->sc, cur, 1);
}

/*
 * Make sure this key is in order and doesn't stray outside of the parent
 * keys.
 */
STATIC void
xchk_btree_key(
	struct xchk_btree	*bs,
	int			level)
{
	struct xfs_btree_cur	*cur = bs->cur;
	union xfs_btree_key	*key;
	union xfs_btree_key	*keyp;
	struct xfs_btree_block	*block;
	struct xfs_btree_block	*keyblock;
	struct xfs_buf		*bp;

	block = xfs_btree_get_block(cur, level, &bp);
	key = xfs_btree_key_addr(cur, cur->bc_levels[level].ptr, block);

	trace_xchk_btree_key(bs->sc, cur, level);

	/* Are all low keys across all node blocks in order? */
	if (bs->lastkey[level - 1].valid &&
	    !cur->bc_ops->keys_inorder(cur, &bs->lastkey[level - 1].key, key))
		xchk_btree_set_corrupt(bs->sc, cur, level);
	memcpy(&bs->lastkey[level - 1].key, key, cur->bc_ops->key_len);
	bs->lastkey[level - 1].valid = true;

	if (level + 1 >= cur->bc_nlevels)
		return;

	/* Is this block's low key at least as large as the parent low key? */
	keyblock = xfs_btree_get_block(cur, level + 1, &bp);
	keyp = xfs_btree_key_addr(cur, cur->bc_levels[level + 1].ptr, keyblock);
	if (xfs_btree_keycmp_lt(cur, key, keyp))
		xchk_btree_set_corrupt(bs->sc, cur, level);

	if (!(cur->bc_ops->geom_flags & XFS_BTGEO_OVERLAPPING))
		return;

	/* Is this block's high key no larger than the parent high key? */
	key = xfs_btree_high_key_addr(cur, cur->bc_levels[level].ptr, block);
	keyp = xfs_btree_high_key_addr(cur, cur->bc_levels[level + 1].ptr,
			keyblock);
	if (xfs_btree_keycmp_lt(cur, keyp, key))
		xchk_btree_set_corrupt(bs->sc, cur, level);
}

/*
 * Check a btree pointer.  Returns true if it's ok to use this pointer.
 * Callers do not need to set the corrupt flag.
 */
static bool
xchk_btree_ptr_ok(
	struct xchk_btree	*bs,
	int			level,
	union xfs_btree_ptr	*ptr)
{
	/* A btree rooted in an inode has no block pointer to the root. */
	if (bs->cur->bc_ops->type == XFS_BTREE_TYPE_INODE &&
	    level == bs->cur->bc_nlevels)
		return true;

	/* Otherwise, check the pointers. */
	if (__xfs_btree_check_ptr(bs->cur, ptr, 0, level)) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, level);
		return false;
	}

	return true;
}

/* Check that a btree block's sibling matches what we expect it. */
STATIC int
xchk_btree_block_check_sibling(
	struct xchk_btree	*bs,
	int			level,
	int			direction,
	union xfs_btree_ptr	*sibling)
{
	struct xfs_btree_cur	*cur = bs->cur;
	struct xfs_btree_block	*pblock;
	struct xfs_buf		*pbp;
	struct xfs_btree_cur	*ncur = NULL;
	union xfs_btree_ptr	*pp;
	int			success;
	int			error;

	error = xfs_btree_dup_cursor(cur, &ncur);
	if (!xchk_btree_process_error(bs->sc, cur, level + 1, &error) ||
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
			xchk_btree_set_corrupt(bs->sc, cur, level);
		error = 0;
		goto out;
	}

	/* Increment upper level pointer. */
	if (direction > 0)
		error = xfs_btree_increment(ncur, level + 1, &success);
	else
		error = xfs_btree_decrement(ncur, level + 1, &success);
	if (!xchk_btree_process_error(bs->sc, cur, level + 1, &error))
		goto out;
	if (!success) {
		xchk_btree_set_corrupt(bs->sc, cur, level + 1);
		goto out;
	}

	/* Compare upper level pointer to sibling pointer. */
	pblock = xfs_btree_get_block(ncur, level + 1, &pbp);
	pp = xfs_btree_ptr_addr(ncur, ncur->bc_levels[level + 1].ptr, pblock);
	if (!xchk_btree_ptr_ok(bs, level + 1, pp))
		goto out;
	if (pbp)
		xchk_buffer_recheck(bs->sc, pbp);

	if (xfs_btree_cmp_two_ptrs(cur, pp, sibling))
		xchk_btree_set_corrupt(bs->sc, cur, level);
out:
	xfs_btree_del_cursor(ncur, XFS_BTREE_ERROR);
	return error;
}

/* Check the siblings of a btree block. */
STATIC int
xchk_btree_block_check_siblings(
	struct xchk_btree	*bs,
	struct xfs_btree_block	*block)
{
	struct xfs_btree_cur	*cur = bs->cur;
	union xfs_btree_ptr	leftsib;
	union xfs_btree_ptr	rightsib;
	int			level;
	int			error = 0;

	xfs_btree_get_sibling(cur, block, &leftsib, XFS_BB_LEFTSIB);
	xfs_btree_get_sibling(cur, block, &rightsib, XFS_BB_RIGHTSIB);
	level = xfs_btree_get_level(block);

	/* Root block should never have siblings. */
	if (level == cur->bc_nlevels - 1) {
		if (!xfs_btree_ptr_is_null(cur, &leftsib) ||
		    !xfs_btree_ptr_is_null(cur, &rightsib))
			xchk_btree_set_corrupt(bs->sc, cur, level);
		goto out;
	}

	/*
	 * Does the left & right sibling pointers match the adjacent
	 * parent level pointers?
	 * (These function absorbs error codes for us.)
	 */
	error = xchk_btree_block_check_sibling(bs, level, -1, &leftsib);
	if (error)
		return error;
	error = xchk_btree_block_check_sibling(bs, level, 1, &rightsib);
	if (error)
		return error;
out:
	return error;
}

struct check_owner {
	struct list_head	list;
	xfs_daddr_t		daddr;
	int			level;
};

/*
 * Make sure this btree block isn't in the free list and that there's
 * an rmap record for it.
 */
STATIC int
xchk_btree_check_block_owner(
	struct xchk_btree	*bs,
	int			level,
	xfs_daddr_t		daddr)
{
	xfs_agnumber_t		agno;
	xfs_agblock_t		agbno;
	bool			init_sa;
	int			error = 0;

	if (!bs->cur)
		return 0;

	agno = xfs_daddr_to_agno(bs->cur->bc_mp, daddr);
	agbno = xfs_daddr_to_agbno(bs->cur->bc_mp, daddr);

	/*
	 * If the btree being examined is not itself a per-AG btree, initialize
	 * sc->sa so that we can check for the presence of an ownership record
	 * in the rmap btree for the AG containing the block.
	 */
	init_sa = bs->cur->bc_ops->type != XFS_BTREE_TYPE_AG;
	if (init_sa) {
		error = xchk_ag_init_existing(bs->sc, agno, &bs->sc->sa);
		if (!xchk_btree_xref_process_error(bs->sc, bs->cur,
				level, &error))
			goto out_free;
	}

	xchk_xref_is_used_space(bs->sc, agbno, 1);
	/*
	 * The bnobt scrubber aliases bs->cur to bs->sc->sa.bno_cur, so we
	 * have to nullify it (to shut down further block owner checks) if
	 * self-xref encounters problems.
	 */
	if (!bs->sc->sa.bno_cur && xfs_btree_is_bno(bs->cur->bc_ops))
		bs->cur = NULL;

	xchk_xref_is_only_owned_by(bs->sc, agbno, 1, bs->oinfo);
	if (!bs->sc->sa.rmap_cur && xfs_btree_is_rmap(bs->cur->bc_ops))
		bs->cur = NULL;

out_free:
	if (init_sa)
		xchk_ag_free(bs->sc, &bs->sc->sa);

	return error;
}

/* Check the owner of a btree block. */
STATIC int
xchk_btree_check_owner(
	struct xchk_btree	*bs,
	int			level,
	struct xfs_buf		*bp)
{
	struct xfs_btree_cur	*cur = bs->cur;

	/*
	 * In theory, xfs_btree_get_block should only give us a null buffer
	 * pointer for the root of a root-in-inode btree type, but we need
	 * to check defensively here in case the cursor state is also screwed
	 * up.
	 */
	if (bp == NULL) {
		if (cur->bc_ops->type != XFS_BTREE_TYPE_INODE)
			xchk_btree_set_corrupt(bs->sc, bs->cur, level);
		return 0;
	}

	/*
	 * We want to cross-reference each btree block with the bnobt
	 * and the rmapbt.  We cannot cross-reference the bnobt or
	 * rmapbt while scanning the bnobt or rmapbt, respectively,
	 * because we cannot alter the cursor and we'd prefer not to
	 * duplicate cursors.  Therefore, save the buffer daddr for
	 * later scanning.
	 */
	if (xfs_btree_is_bno(cur->bc_ops) || xfs_btree_is_rmap(cur->bc_ops)) {
		struct check_owner	*co;

		co = kmalloc(sizeof(struct check_owner), XCHK_GFP_FLAGS);
		if (!co)
			return -ENOMEM;

		INIT_LIST_HEAD(&co->list);
		co->level = level;
		co->daddr = xfs_buf_daddr(bp);
		list_add_tail(&co->list, &bs->to_check);
		return 0;
	}

	return xchk_btree_check_block_owner(bs, level, xfs_buf_daddr(bp));
}

/* Decide if we want to check minrecs of a btree block in the inode root. */
static inline bool
xchk_btree_check_iroot_minrecs(
	struct xchk_btree	*bs)
{
	/*
	 * xfs_bmap_add_attrfork_btree had an implementation bug wherein it
	 * would miscalculate the space required for the data fork bmbt root
	 * when adding an attr fork, and promote the iroot contents to an
	 * external block unnecessarily.  This went unnoticed for many years
	 * until scrub found filesystems in this state.  Inode rooted btrees are
	 * not supposed to have immediate child blocks that are small enough
	 * that the contents could fit in the inode root, but we can't fail
	 * existing filesystems, so instead we disable the check for data fork
	 * bmap btrees when there's an attr fork.
	 */
	if (xfs_btree_is_bmap(bs->cur->bc_ops) &&
	    bs->cur->bc_ino.whichfork == XFS_DATA_FORK &&
	    xfs_inode_has_attr_fork(bs->sc->ip))
		return false;

	return true;
}

/*
 * Check that this btree block has at least minrecs records or is one of the
 * special blocks that don't require that.
 */
STATIC void
xchk_btree_check_minrecs(
	struct xchk_btree	*bs,
	int			level,
	struct xfs_btree_block	*block)
{
	struct xfs_btree_cur	*cur = bs->cur;
	unsigned int		root_level = cur->bc_nlevels - 1;
	unsigned int		numrecs = be16_to_cpu(block->bb_numrecs);

	/* More records than minrecs means the block is ok. */
	if (numrecs >= cur->bc_ops->get_minrecs(cur, level))
		return;

	/*
	 * For btrees rooted in the inode, it's possible that the root block
	 * contents spilled into a regular ondisk block because there wasn't
	 * enough space in the inode root.  The number of records in that
	 * child block might be less than the standard minrecs, but that's ok
	 * provided that there's only one direct child of the root.
	 */
	if (cur->bc_ops->type == XFS_BTREE_TYPE_INODE &&
	    level == cur->bc_nlevels - 2) {
		struct xfs_btree_block	*root_block;
		struct xfs_buf		*root_bp;
		int			root_maxrecs;

		root_block = xfs_btree_get_block(cur, root_level, &root_bp);
		root_maxrecs = cur->bc_ops->get_dmaxrecs(cur, root_level);
		if (xchk_btree_check_iroot_minrecs(bs) &&
		    (be16_to_cpu(root_block->bb_numrecs) != 1 ||
		     numrecs <= root_maxrecs))
			xchk_btree_set_corrupt(bs->sc, cur, level);
		return;
	}

	/*
	 * Otherwise, only the root level is allowed to have fewer than minrecs
	 * records or keyptrs.
	 */
	if (level < root_level)
		xchk_btree_set_corrupt(bs->sc, cur, level);
}

/*
 * If this btree block has a parent, make sure that the parent's keys capture
 * the keyspace contained in this block.
 */
STATIC void
xchk_btree_block_check_keys(
	struct xchk_btree	*bs,
	int			level,
	struct xfs_btree_block	*block)
{
	union xfs_btree_key	block_key;
	union xfs_btree_key	*block_high_key;
	union xfs_btree_key	*parent_low_key, *parent_high_key;
	struct xfs_btree_cur	*cur = bs->cur;
	struct xfs_btree_block	*parent_block;
	struct xfs_buf		*bp;

	if (level == cur->bc_nlevels - 1)
		return;

	xfs_btree_get_keys(cur, block, &block_key);

	/* Make sure the low key of this block matches the parent. */
	parent_block = xfs_btree_get_block(cur, level + 1, &bp);
	parent_low_key = xfs_btree_key_addr(cur, cur->bc_levels[level + 1].ptr,
			parent_block);
	if (xfs_btree_keycmp_ne(cur, &block_key, parent_low_key)) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, level);
		return;
	}

	if (!(cur->bc_ops->geom_flags & XFS_BTGEO_OVERLAPPING))
		return;

	/* Make sure the high key of this block matches the parent. */
	parent_high_key = xfs_btree_high_key_addr(cur,
			cur->bc_levels[level + 1].ptr, parent_block);
	block_high_key = xfs_btree_high_key_from_key(cur, &block_key);
	if (xfs_btree_keycmp_ne(cur, block_high_key, parent_high_key))
		xchk_btree_set_corrupt(bs->sc, bs->cur, level);
}

/*
 * Grab and scrub a btree block given a btree pointer.  Returns block
 * and buffer pointers (if applicable) if they're ok to use.
 */
STATIC int
xchk_btree_get_block(
	struct xchk_btree	*bs,
	int			level,
	union xfs_btree_ptr	*pp,
	struct xfs_btree_block	**pblock,
	struct xfs_buf		**pbp)
{
	int			error;

	*pblock = NULL;
	*pbp = NULL;

	error = xfs_btree_lookup_get_block(bs->cur, level, pp, pblock);
	if (!xchk_btree_process_error(bs->sc, bs->cur, level, &error) ||
	    !*pblock)
		return error;

	xfs_btree_get_block(bs->cur, level, pbp);
	if (__xfs_btree_check_block(bs->cur, *pblock, level, *pbp)) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, level);
		return 0;
	}
	if (*pbp)
		xchk_buffer_recheck(bs->sc, *pbp);

	xchk_btree_check_minrecs(bs, level, *pblock);

	/*
	 * Check the block's owner; this function absorbs error codes
	 * for us.
	 */
	error = xchk_btree_check_owner(bs, level, *pbp);
	if (error)
		return error;

	/*
	 * Check the block's siblings; this function absorbs error codes
	 * for us.
	 */
	error = xchk_btree_block_check_siblings(bs, *pblock);
	if (error)
		return error;

	xchk_btree_block_check_keys(bs, level, *pblock);
	return 0;
}

/*
 * Check that the low and high keys of this block match the keys stored
 * in the parent block.
 */
STATIC void
xchk_btree_block_keys(
	struct xchk_btree	*bs,
	int			level,
	struct xfs_btree_block	*block)
{
	union xfs_btree_key	block_keys;
	struct xfs_btree_cur	*cur = bs->cur;
	union xfs_btree_key	*high_bk;
	union xfs_btree_key	*parent_keys;
	union xfs_btree_key	*high_pk;
	struct xfs_btree_block	*parent_block;
	struct xfs_buf		*bp;

	if (level >= cur->bc_nlevels - 1)
		return;

	/* Calculate the keys for this block. */
	xfs_btree_get_keys(cur, block, &block_keys);

	/* Obtain the parent's copy of the keys for this block. */
	parent_block = xfs_btree_get_block(cur, level + 1, &bp);
	parent_keys = xfs_btree_key_addr(cur, cur->bc_levels[level + 1].ptr,
			parent_block);

	if (xfs_btree_keycmp_ne(cur, &block_keys, parent_keys))
		xchk_btree_set_corrupt(bs->sc, cur, 1);

	if (!(cur->bc_ops->geom_flags & XFS_BTGEO_OVERLAPPING))
		return;

	/* Get high keys */
	high_bk = xfs_btree_high_key_from_key(cur, &block_keys);
	high_pk = xfs_btree_high_key_addr(cur, cur->bc_levels[level + 1].ptr,
			parent_block);

	if (xfs_btree_keycmp_ne(cur, high_bk, high_pk))
		xchk_btree_set_corrupt(bs->sc, cur, 1);
}

/*
 * Visit all nodes and leaves of a btree.  Check that all pointers and
 * records are in order, that the keys reflect the records, and use a callback
 * so that the caller can verify individual records.
 */
int
xchk_btree(
	struct xfs_scrub		*sc,
	struct xfs_btree_cur		*cur,
	xchk_btree_rec_fn		scrub_fn,
	const struct xfs_owner_info	*oinfo,
	void				*private)
{
	union xfs_btree_ptr		ptr;
	struct xchk_btree		*bs;
	union xfs_btree_ptr		*pp;
	union xfs_btree_rec		*recp;
	struct xfs_btree_block		*block;
	struct xfs_buf			*bp;
	struct check_owner		*co;
	struct check_owner		*n;
	size_t				cur_sz;
	int				level;
	int				error = 0;

	/*
	 * Allocate the btree scrub context from the heap, because this
	 * structure can get rather large.  Don't let a caller feed us a
	 * totally absurd size.
	 */
	cur_sz = xchk_btree_sizeof(cur->bc_nlevels);
	if (cur_sz > PAGE_SIZE) {
		xchk_btree_set_corrupt(sc, cur, 0);
		return 0;
	}
	bs = kzalloc(cur_sz, XCHK_GFP_FLAGS);
	if (!bs)
		return -ENOMEM;
	bs->cur = cur;
	bs->scrub_rec = scrub_fn;
	bs->oinfo = oinfo;
	bs->private = private;
	bs->sc = sc;

	/* Initialize scrub state */
	INIT_LIST_HEAD(&bs->to_check);

	/*
	 * Load the root of the btree.  The helper function absorbs
	 * error codes for us.
	 */
	level = cur->bc_nlevels - 1;
	xfs_btree_init_ptr_from_cur(cur, &ptr);
	if (!xchk_btree_ptr_ok(bs, cur->bc_nlevels, &ptr))
		goto out;
	error = xchk_btree_get_block(bs, level, &ptr, &block, &bp);
	if (error || !block)
		goto out;

	cur->bc_levels[level].ptr = 1;

	while (level < cur->bc_nlevels) {
		block = xfs_btree_get_block(cur, level, &bp);

		if (level == 0) {
			/* End of leaf, pop back towards the root. */
			if (cur->bc_levels[level].ptr >
			    be16_to_cpu(block->bb_numrecs)) {
				xchk_btree_block_keys(bs, level, block);
				if (level < cur->bc_nlevels - 1)
					cur->bc_levels[level + 1].ptr++;
				level++;
				continue;
			}

			/* Records in order for scrub? */
			xchk_btree_rec(bs);

			/* Call out to the record checker. */
			recp = xfs_btree_rec_addr(cur, cur->bc_levels[0].ptr,
					block);
			error = bs->scrub_rec(bs, recp);
			if (error)
				break;
			if (xchk_should_terminate(sc, &error) ||
			    (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
				break;

			cur->bc_levels[level].ptr++;
			continue;
		}

		/* End of node, pop back towards the root. */
		if (cur->bc_levels[level].ptr >
					be16_to_cpu(block->bb_numrecs)) {
			xchk_btree_block_keys(bs, level, block);
			if (level < cur->bc_nlevels - 1)
				cur->bc_levels[level + 1].ptr++;
			level++;
			continue;
		}

		/* Keys in order for scrub? */
		xchk_btree_key(bs, level);

		/* Drill another level deeper. */
		pp = xfs_btree_ptr_addr(cur, cur->bc_levels[level].ptr, block);
		if (!xchk_btree_ptr_ok(bs, level, pp)) {
			cur->bc_levels[level].ptr++;
			continue;
		}
		level--;
		error = xchk_btree_get_block(bs, level, pp, &block, &bp);
		if (error || !block)
			goto out;

		cur->bc_levels[level].ptr = 1;
	}

out:
	/* Process deferred owner checks on btree blocks. */
	list_for_each_entry_safe(co, n, &bs->to_check, list) {
		if (!error && bs->cur)
			error = xchk_btree_check_block_owner(bs, co->level,
					co->daddr);
		list_del(&co->list);
		kfree(co);
	}
	kfree(bs);

	return error;
}
