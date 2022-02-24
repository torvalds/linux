// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_inode_item.h"
#include "xfs_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_bmap.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_dir2_priv.h"
#include "xfs_attr_leaf.h"
#include "xfs_types.h"
#include "xfs_errortag.h"

struct kmem_cache *xfs_ifork_cache;

void
xfs_init_local_fork(
	struct xfs_inode	*ip,
	int			whichfork,
	const void		*data,
	int64_t			size)
{
	struct xfs_ifork	*ifp = XFS_IFORK_PTR(ip, whichfork);
	int			mem_size = size, real_size = 0;
	bool			zero_terminate;

	/*
	 * If we are using the local fork to store a symlink body we need to
	 * zero-terminate it so that we can pass it back to the VFS directly.
	 * Overallocate the in-memory fork by one for that and add a zero
	 * to terminate it below.
	 */
	zero_terminate = S_ISLNK(VFS_I(ip)->i_mode);
	if (zero_terminate)
		mem_size++;

	if (size) {
		real_size = roundup(mem_size, 4);
		ifp->if_u1.if_data = kmem_alloc(real_size, KM_NOFS);
		memcpy(ifp->if_u1.if_data, data, size);
		if (zero_terminate)
			ifp->if_u1.if_data[size] = '\0';
	} else {
		ifp->if_u1.if_data = NULL;
	}

	ifp->if_bytes = size;
}

/*
 * The file is in-lined in the on-disk inode.
 */
STATIC int
xfs_iformat_local(
	struct xfs_inode	*ip,
	struct xfs_dinode	*dip,
	int			whichfork,
	int			size)
{
	/*
	 * If the size is unreasonable, then something
	 * is wrong and we just bail out rather than crash in
	 * kmem_alloc() or memcpy() below.
	 */
	if (unlikely(size > XFS_DFORK_SIZE(dip, ip->i_mount, whichfork))) {
		xfs_warn(ip->i_mount,
	"corrupt inode %Lu (bad size %d for local fork, size = %zd).",
			(unsigned long long) ip->i_ino, size,
			XFS_DFORK_SIZE(dip, ip->i_mount, whichfork));
		xfs_inode_verifier_error(ip, -EFSCORRUPTED,
				"xfs_iformat_local", dip, sizeof(*dip),
				__this_address);
		return -EFSCORRUPTED;
	}

	xfs_init_local_fork(ip, whichfork, XFS_DFORK_PTR(dip, whichfork), size);
	return 0;
}

/*
 * The file consists of a set of extents all of which fit into the on-disk
 * inode.
 */
STATIC int
xfs_iformat_extents(
	struct xfs_inode	*ip,
	struct xfs_dinode	*dip,
	int			whichfork)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_ifork	*ifp = XFS_IFORK_PTR(ip, whichfork);
	int			state = xfs_bmap_fork_to_state(whichfork);
	int			nex = XFS_DFORK_NEXTENTS(dip, whichfork);
	int			size = nex * sizeof(xfs_bmbt_rec_t);
	struct xfs_iext_cursor	icur;
	struct xfs_bmbt_rec	*dp;
	struct xfs_bmbt_irec	new;
	int			i;

	/*
	 * If the number of extents is unreasonable, then something is wrong and
	 * we just bail out rather than crash in kmem_alloc() or memcpy() below.
	 */
	if (unlikely(size < 0 || size > XFS_DFORK_SIZE(dip, mp, whichfork))) {
		xfs_warn(ip->i_mount, "corrupt inode %Lu ((a)extents = %d).",
			(unsigned long long) ip->i_ino, nex);
		xfs_inode_verifier_error(ip, -EFSCORRUPTED,
				"xfs_iformat_extents(1)", dip, sizeof(*dip),
				__this_address);
		return -EFSCORRUPTED;
	}

	ifp->if_bytes = 0;
	ifp->if_u1.if_root = NULL;
	ifp->if_height = 0;
	if (size) {
		dp = (xfs_bmbt_rec_t *) XFS_DFORK_PTR(dip, whichfork);

		xfs_iext_first(ifp, &icur);
		for (i = 0; i < nex; i++, dp++) {
			xfs_failaddr_t	fa;

			xfs_bmbt_disk_get_all(dp, &new);
			fa = xfs_bmap_validate_extent(ip, whichfork, &new);
			if (fa) {
				xfs_inode_verifier_error(ip, -EFSCORRUPTED,
						"xfs_iformat_extents(2)",
						dp, sizeof(*dp), fa);
				return -EFSCORRUPTED;
			}

			xfs_iext_insert(ip, &icur, &new, state);
			trace_xfs_read_extent(ip, &icur, state, _THIS_IP_);
			xfs_iext_next(ifp, &icur);
		}
	}
	return 0;
}

/*
 * The file has too many extents to fit into
 * the inode, so they are in B-tree format.
 * Allocate a buffer for the root of the B-tree
 * and copy the root into it.  The i_extents
 * field will remain NULL until all of the
 * extents are read in (when they are needed).
 */
STATIC int
xfs_iformat_btree(
	struct xfs_inode	*ip,
	struct xfs_dinode	*dip,
	int			whichfork)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_bmdr_block_t	*dfp;
	struct xfs_ifork	*ifp;
	/* REFERENCED */
	int			nrecs;
	int			size;
	int			level;

	ifp = XFS_IFORK_PTR(ip, whichfork);
	dfp = (xfs_bmdr_block_t *)XFS_DFORK_PTR(dip, whichfork);
	size = XFS_BMAP_BROOT_SPACE(mp, dfp);
	nrecs = be16_to_cpu(dfp->bb_numrecs);
	level = be16_to_cpu(dfp->bb_level);

	/*
	 * blow out if -- fork has less extents than can fit in
	 * fork (fork shouldn't be a btree format), root btree
	 * block has more records than can fit into the fork,
	 * or the number of extents is greater than the number of
	 * blocks.
	 */
	if (unlikely(ifp->if_nextents <= XFS_IFORK_MAXEXT(ip, whichfork) ||
		     nrecs == 0 ||
		     XFS_BMDR_SPACE_CALC(nrecs) >
					XFS_DFORK_SIZE(dip, mp, whichfork) ||
		     ifp->if_nextents > ip->i_nblocks) ||
		     level == 0 || level > XFS_BM_MAXLEVELS(mp, whichfork)) {
		xfs_warn(mp, "corrupt inode %Lu (btree).",
					(unsigned long long) ip->i_ino);
		xfs_inode_verifier_error(ip, -EFSCORRUPTED,
				"xfs_iformat_btree", dfp, size,
				__this_address);
		return -EFSCORRUPTED;
	}

	ifp->if_broot_bytes = size;
	ifp->if_broot = kmem_alloc(size, KM_NOFS);
	ASSERT(ifp->if_broot != NULL);
	/*
	 * Copy and convert from the on-disk structure
	 * to the in-memory structure.
	 */
	xfs_bmdr_to_bmbt(ip, dfp, XFS_DFORK_SIZE(dip, ip->i_mount, whichfork),
			 ifp->if_broot, size);

	ifp->if_bytes = 0;
	ifp->if_u1.if_root = NULL;
	ifp->if_height = 0;
	return 0;
}

int
xfs_iformat_data_fork(
	struct xfs_inode	*ip,
	struct xfs_dinode	*dip)
{
	struct inode		*inode = VFS_I(ip);
	int			error;

	/*
	 * Initialize the extent count early, as the per-format routines may
	 * depend on it.
	 */
	ip->i_df.if_format = dip->di_format;
	ip->i_df.if_nextents = be32_to_cpu(dip->di_nextents);

	switch (inode->i_mode & S_IFMT) {
	case S_IFIFO:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFSOCK:
		ip->i_disk_size = 0;
		inode->i_rdev = xfs_to_linux_dev_t(xfs_dinode_get_rdev(dip));
		return 0;
	case S_IFREG:
	case S_IFLNK:
	case S_IFDIR:
		switch (ip->i_df.if_format) {
		case XFS_DINODE_FMT_LOCAL:
			error = xfs_iformat_local(ip, dip, XFS_DATA_FORK,
					be64_to_cpu(dip->di_size));
			if (!error)
				error = xfs_ifork_verify_local_data(ip);
			return error;
		case XFS_DINODE_FMT_EXTENTS:
			return xfs_iformat_extents(ip, dip, XFS_DATA_FORK);
		case XFS_DINODE_FMT_BTREE:
			return xfs_iformat_btree(ip, dip, XFS_DATA_FORK);
		default:
			xfs_inode_verifier_error(ip, -EFSCORRUPTED, __func__,
					dip, sizeof(*dip), __this_address);
			return -EFSCORRUPTED;
		}
		break;
	default:
		xfs_inode_verifier_error(ip, -EFSCORRUPTED, __func__, dip,
				sizeof(*dip), __this_address);
		return -EFSCORRUPTED;
	}
}

static uint16_t
xfs_dfork_attr_shortform_size(
	struct xfs_dinode		*dip)
{
	struct xfs_attr_shortform	*atp =
		(struct xfs_attr_shortform *)XFS_DFORK_APTR(dip);

	return be16_to_cpu(atp->hdr.totsize);
}

struct xfs_ifork *
xfs_ifork_alloc(
	enum xfs_dinode_fmt	format,
	xfs_extnum_t		nextents)
{
	struct xfs_ifork	*ifp;

	ifp = kmem_cache_zalloc(xfs_ifork_cache, GFP_NOFS | __GFP_NOFAIL);
	ifp->if_format = format;
	ifp->if_nextents = nextents;
	return ifp;
}

int
xfs_iformat_attr_fork(
	struct xfs_inode	*ip,
	struct xfs_dinode	*dip)
{
	int			error = 0;

	/*
	 * Initialize the extent count early, as the per-format routines may
	 * depend on it.
	 */
	ip->i_afp = xfs_ifork_alloc(dip->di_aformat,
				be16_to_cpu(dip->di_anextents));

	switch (ip->i_afp->if_format) {
	case XFS_DINODE_FMT_LOCAL:
		error = xfs_iformat_local(ip, dip, XFS_ATTR_FORK,
				xfs_dfork_attr_shortform_size(dip));
		if (!error)
			error = xfs_ifork_verify_local_attr(ip);
		break;
	case XFS_DINODE_FMT_EXTENTS:
		error = xfs_iformat_extents(ip, dip, XFS_ATTR_FORK);
		break;
	case XFS_DINODE_FMT_BTREE:
		error = xfs_iformat_btree(ip, dip, XFS_ATTR_FORK);
		break;
	default:
		xfs_inode_verifier_error(ip, error, __func__, dip,
				sizeof(*dip), __this_address);
		error = -EFSCORRUPTED;
		break;
	}

	if (error) {
		kmem_cache_free(xfs_ifork_cache, ip->i_afp);
		ip->i_afp = NULL;
	}
	return error;
}

/*
 * Reallocate the space for if_broot based on the number of records
 * being added or deleted as indicated in rec_diff.  Move the records
 * and pointers in if_broot to fit the new size.  When shrinking this
 * will eliminate holes between the records and pointers created by
 * the caller.  When growing this will create holes to be filled in
 * by the caller.
 *
 * The caller must not request to add more records than would fit in
 * the on-disk inode root.  If the if_broot is currently NULL, then
 * if we are adding records, one will be allocated.  The caller must also
 * not request that the number of records go below zero, although
 * it can go to zero.
 *
 * ip -- the inode whose if_broot area is changing
 * ext_diff -- the change in the number of records, positive or negative,
 *	 requested for the if_broot array.
 */
void
xfs_iroot_realloc(
	xfs_inode_t		*ip,
	int			rec_diff,
	int			whichfork)
{
	struct xfs_mount	*mp = ip->i_mount;
	int			cur_max;
	struct xfs_ifork	*ifp;
	struct xfs_btree_block	*new_broot;
	int			new_max;
	size_t			new_size;
	char			*np;
	char			*op;

	/*
	 * Handle the degenerate case quietly.
	 */
	if (rec_diff == 0) {
		return;
	}

	ifp = XFS_IFORK_PTR(ip, whichfork);
	if (rec_diff > 0) {
		/*
		 * If there wasn't any memory allocated before, just
		 * allocate it now and get out.
		 */
		if (ifp->if_broot_bytes == 0) {
			new_size = XFS_BMAP_BROOT_SPACE_CALC(mp, rec_diff);
			ifp->if_broot = kmem_alloc(new_size, KM_NOFS);
			ifp->if_broot_bytes = (int)new_size;
			return;
		}

		/*
		 * If there is already an existing if_broot, then we need
		 * to realloc() it and shift the pointers to their new
		 * location.  The records don't change location because
		 * they are kept butted up against the btree block header.
		 */
		cur_max = xfs_bmbt_maxrecs(mp, ifp->if_broot_bytes, 0);
		new_max = cur_max + rec_diff;
		new_size = XFS_BMAP_BROOT_SPACE_CALC(mp, new_max);
		ifp->if_broot = krealloc(ifp->if_broot, new_size,
					 GFP_NOFS | __GFP_NOFAIL);
		op = (char *)XFS_BMAP_BROOT_PTR_ADDR(mp, ifp->if_broot, 1,
						     ifp->if_broot_bytes);
		np = (char *)XFS_BMAP_BROOT_PTR_ADDR(mp, ifp->if_broot, 1,
						     (int)new_size);
		ifp->if_broot_bytes = (int)new_size;
		ASSERT(XFS_BMAP_BMDR_SPACE(ifp->if_broot) <=
			XFS_IFORK_SIZE(ip, whichfork));
		memmove(np, op, cur_max * (uint)sizeof(xfs_fsblock_t));
		return;
	}

	/*
	 * rec_diff is less than 0.  In this case, we are shrinking the
	 * if_broot buffer.  It must already exist.  If we go to zero
	 * records, just get rid of the root and clear the status bit.
	 */
	ASSERT((ifp->if_broot != NULL) && (ifp->if_broot_bytes > 0));
	cur_max = xfs_bmbt_maxrecs(mp, ifp->if_broot_bytes, 0);
	new_max = cur_max + rec_diff;
	ASSERT(new_max >= 0);
	if (new_max > 0)
		new_size = XFS_BMAP_BROOT_SPACE_CALC(mp, new_max);
	else
		new_size = 0;
	if (new_size > 0) {
		new_broot = kmem_alloc(new_size, KM_NOFS);
		/*
		 * First copy over the btree block header.
		 */
		memcpy(new_broot, ifp->if_broot,
			XFS_BMBT_BLOCK_LEN(ip->i_mount));
	} else {
		new_broot = NULL;
	}

	/*
	 * Only copy the records and pointers if there are any.
	 */
	if (new_max > 0) {
		/*
		 * First copy the records.
		 */
		op = (char *)XFS_BMBT_REC_ADDR(mp, ifp->if_broot, 1);
		np = (char *)XFS_BMBT_REC_ADDR(mp, new_broot, 1);
		memcpy(np, op, new_max * (uint)sizeof(xfs_bmbt_rec_t));

		/*
		 * Then copy the pointers.
		 */
		op = (char *)XFS_BMAP_BROOT_PTR_ADDR(mp, ifp->if_broot, 1,
						     ifp->if_broot_bytes);
		np = (char *)XFS_BMAP_BROOT_PTR_ADDR(mp, new_broot, 1,
						     (int)new_size);
		memcpy(np, op, new_max * (uint)sizeof(xfs_fsblock_t));
	}
	kmem_free(ifp->if_broot);
	ifp->if_broot = new_broot;
	ifp->if_broot_bytes = (int)new_size;
	if (ifp->if_broot)
		ASSERT(XFS_BMAP_BMDR_SPACE(ifp->if_broot) <=
			XFS_IFORK_SIZE(ip, whichfork));
	return;
}


/*
 * This is called when the amount of space needed for if_data
 * is increased or decreased.  The change in size is indicated by
 * the number of bytes that need to be added or deleted in the
 * byte_diff parameter.
 *
 * If the amount of space needed has decreased below the size of the
 * inline buffer, then switch to using the inline buffer.  Otherwise,
 * use kmem_realloc() or kmem_alloc() to adjust the size of the buffer
 * to what is needed.
 *
 * ip -- the inode whose if_data area is changing
 * byte_diff -- the change in the number of bytes, positive or negative,
 *	 requested for the if_data array.
 */
void
xfs_idata_realloc(
	struct xfs_inode	*ip,
	int64_t			byte_diff,
	int			whichfork)
{
	struct xfs_ifork	*ifp = XFS_IFORK_PTR(ip, whichfork);
	int64_t			new_size = ifp->if_bytes + byte_diff;

	ASSERT(new_size >= 0);
	ASSERT(new_size <= XFS_IFORK_SIZE(ip, whichfork));

	if (byte_diff == 0)
		return;

	if (new_size == 0) {
		kmem_free(ifp->if_u1.if_data);
		ifp->if_u1.if_data = NULL;
		ifp->if_bytes = 0;
		return;
	}

	/*
	 * For inline data, the underlying buffer must be a multiple of 4 bytes
	 * in size so that it can be logged and stay on word boundaries.
	 * We enforce that here.
	 */
	ifp->if_u1.if_data = krealloc(ifp->if_u1.if_data, roundup(new_size, 4),
				      GFP_NOFS | __GFP_NOFAIL);
	ifp->if_bytes = new_size;
}

void
xfs_idestroy_fork(
	struct xfs_ifork	*ifp)
{
	if (ifp->if_broot != NULL) {
		kmem_free(ifp->if_broot);
		ifp->if_broot = NULL;
	}

	switch (ifp->if_format) {
	case XFS_DINODE_FMT_LOCAL:
		kmem_free(ifp->if_u1.if_data);
		ifp->if_u1.if_data = NULL;
		break;
	case XFS_DINODE_FMT_EXTENTS:
	case XFS_DINODE_FMT_BTREE:
		if (ifp->if_height)
			xfs_iext_destroy(ifp);
		break;
	}
}

/*
 * Convert in-core extents to on-disk form
 *
 * In the case of the data fork, the in-core and on-disk fork sizes can be
 * different due to delayed allocation extents. We only copy on-disk extents
 * here, so callers must always use the physical fork size to determine the
 * size of the buffer passed to this routine.  We will return the size actually
 * used.
 */
int
xfs_iextents_copy(
	struct xfs_inode	*ip,
	struct xfs_bmbt_rec	*dp,
	int			whichfork)
{
	int			state = xfs_bmap_fork_to_state(whichfork);
	struct xfs_ifork	*ifp = XFS_IFORK_PTR(ip, whichfork);
	struct xfs_iext_cursor	icur;
	struct xfs_bmbt_irec	rec;
	int64_t			copied = 0;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL | XFS_ILOCK_SHARED));
	ASSERT(ifp->if_bytes > 0);

	for_each_xfs_iext(ifp, &icur, &rec) {
		if (isnullstartblock(rec.br_startblock))
			continue;
		ASSERT(xfs_bmap_validate_extent(ip, whichfork, &rec) == NULL);
		xfs_bmbt_disk_set_all(dp, &rec);
		trace_xfs_write_extent(ip, &icur, state, _RET_IP_);
		copied += sizeof(struct xfs_bmbt_rec);
		dp++;
	}

	ASSERT(copied > 0);
	ASSERT(copied <= ifp->if_bytes);
	return copied;
}

/*
 * Each of the following cases stores data into the same region
 * of the on-disk inode, so only one of them can be valid at
 * any given time. While it is possible to have conflicting formats
 * and log flags, e.g. having XFS_ILOG_?DATA set when the fork is
 * in EXTENTS format, this can only happen when the fork has
 * changed formats after being modified but before being flushed.
 * In these cases, the format always takes precedence, because the
 * format indicates the current state of the fork.
 */
void
xfs_iflush_fork(
	struct xfs_inode	*ip,
	struct xfs_dinode	*dip,
	struct xfs_inode_log_item *iip,
	int			whichfork)
{
	char			*cp;
	struct xfs_ifork	*ifp;
	xfs_mount_t		*mp;
	static const short	brootflag[2] =
		{ XFS_ILOG_DBROOT, XFS_ILOG_ABROOT };
	static const short	dataflag[2] =
		{ XFS_ILOG_DDATA, XFS_ILOG_ADATA };
	static const short	extflag[2] =
		{ XFS_ILOG_DEXT, XFS_ILOG_AEXT };

	if (!iip)
		return;
	ifp = XFS_IFORK_PTR(ip, whichfork);
	/*
	 * This can happen if we gave up in iformat in an error path,
	 * for the attribute fork.
	 */
	if (!ifp) {
		ASSERT(whichfork == XFS_ATTR_FORK);
		return;
	}
	cp = XFS_DFORK_PTR(dip, whichfork);
	mp = ip->i_mount;
	switch (ifp->if_format) {
	case XFS_DINODE_FMT_LOCAL:
		if ((iip->ili_fields & dataflag[whichfork]) &&
		    (ifp->if_bytes > 0)) {
			ASSERT(ifp->if_u1.if_data != NULL);
			ASSERT(ifp->if_bytes <= XFS_IFORK_SIZE(ip, whichfork));
			memcpy(cp, ifp->if_u1.if_data, ifp->if_bytes);
		}
		break;

	case XFS_DINODE_FMT_EXTENTS:
		if ((iip->ili_fields & extflag[whichfork]) &&
		    (ifp->if_bytes > 0)) {
			ASSERT(ifp->if_nextents > 0);
			(void)xfs_iextents_copy(ip, (xfs_bmbt_rec_t *)cp,
				whichfork);
		}
		break;

	case XFS_DINODE_FMT_BTREE:
		if ((iip->ili_fields & brootflag[whichfork]) &&
		    (ifp->if_broot_bytes > 0)) {
			ASSERT(ifp->if_broot != NULL);
			ASSERT(XFS_BMAP_BMDR_SPACE(ifp->if_broot) <=
			        XFS_IFORK_SIZE(ip, whichfork));
			xfs_bmbt_to_bmdr(mp, ifp->if_broot, ifp->if_broot_bytes,
				(xfs_bmdr_block_t *)cp,
				XFS_DFORK_SIZE(dip, mp, whichfork));
		}
		break;

	case XFS_DINODE_FMT_DEV:
		if (iip->ili_fields & XFS_ILOG_DEV) {
			ASSERT(whichfork == XFS_DATA_FORK);
			xfs_dinode_put_rdev(dip,
					linux_to_xfs_dev_t(VFS_I(ip)->i_rdev));
		}
		break;

	default:
		ASSERT(0);
		break;
	}
}

/* Convert bmap state flags to an inode fork. */
struct xfs_ifork *
xfs_iext_state_to_fork(
	struct xfs_inode	*ip,
	int			state)
{
	if (state & BMAP_COWFORK)
		return ip->i_cowfp;
	else if (state & BMAP_ATTRFORK)
		return ip->i_afp;
	return &ip->i_df;
}

/*
 * Initialize an inode's copy-on-write fork.
 */
void
xfs_ifork_init_cow(
	struct xfs_inode	*ip)
{
	if (ip->i_cowfp)
		return;

	ip->i_cowfp = kmem_cache_zalloc(xfs_ifork_cache,
				       GFP_NOFS | __GFP_NOFAIL);
	ip->i_cowfp->if_format = XFS_DINODE_FMT_EXTENTS;
}

/* Verify the inline contents of the data fork of an inode. */
int
xfs_ifork_verify_local_data(
	struct xfs_inode	*ip)
{
	xfs_failaddr_t		fa = NULL;

	switch (VFS_I(ip)->i_mode & S_IFMT) {
	case S_IFDIR:
		fa = xfs_dir2_sf_verify(ip);
		break;
	case S_IFLNK:
		fa = xfs_symlink_shortform_verify(ip);
		break;
	default:
		break;
	}

	if (fa) {
		xfs_inode_verifier_error(ip, -EFSCORRUPTED, "data fork",
				ip->i_df.if_u1.if_data, ip->i_df.if_bytes, fa);
		return -EFSCORRUPTED;
	}

	return 0;
}

/* Verify the inline contents of the attr fork of an inode. */
int
xfs_ifork_verify_local_attr(
	struct xfs_inode	*ip)
{
	struct xfs_ifork	*ifp = ip->i_afp;
	xfs_failaddr_t		fa;

	if (!ifp)
		fa = __this_address;
	else
		fa = xfs_attr_shortform_verify(ip);

	if (fa) {
		xfs_inode_verifier_error(ip, -EFSCORRUPTED, "attr fork",
				ifp ? ifp->if_u1.if_data : NULL,
				ifp ? ifp->if_bytes : 0, fa);
		return -EFSCORRUPTED;
	}

	return 0;
}

int
xfs_iext_count_may_overflow(
	struct xfs_inode	*ip,
	int			whichfork,
	int			nr_to_add)
{
	struct xfs_ifork	*ifp = XFS_IFORK_PTR(ip, whichfork);
	uint64_t		max_exts;
	uint64_t		nr_exts;

	if (whichfork == XFS_COW_FORK)
		return 0;

	max_exts = (whichfork == XFS_ATTR_FORK) ? MAXAEXTNUM : MAXEXTNUM;

	if (XFS_TEST_ERROR(false, ip->i_mount, XFS_ERRTAG_REDUCE_MAX_IEXTENTS))
		max_exts = 10;

	nr_exts = ifp->if_nextents + nr_to_add;
	if (nr_exts < ifp->if_nextents || nr_exts > max_exts)
		return -EFBIG;

	return 0;
}
