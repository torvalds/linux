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

kmem_zone_t *xfs_ifork_zone;

STATIC int xfs_iformat_local(xfs_inode_t *, xfs_dinode_t *, int, int);
STATIC int xfs_iformat_extents(xfs_inode_t *, xfs_dinode_t *, int);
STATIC int xfs_iformat_btree(xfs_inode_t *, xfs_dinode_t *, int);

/*
 * Copy inode type and data and attr format specific information from the
 * on-disk inode to the in-core inode and fork structures.  For fifos, devices,
 * and sockets this means set i_rdev to the proper value.  For files,
 * directories, and symlinks this means to bring in the in-line data or extent
 * pointers as well as the attribute fork.  For a fork in B-tree format, only
 * the root is immediately brought in-core.  The rest will be read in later when
 * first referenced (see xfs_iread_extents()).
 */
int
xfs_iformat_fork(
	struct xfs_inode	*ip,
	struct xfs_dinode	*dip)
{
	struct inode		*inode = VFS_I(ip);
	struct xfs_attr_shortform *atp;
	int			size;
	int			error = 0;
	xfs_fsize_t             di_size;

	switch (inode->i_mode & S_IFMT) {
	case S_IFIFO:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFSOCK:
		ip->i_d.di_size = 0;
		inode->i_rdev = xfs_to_linux_dev_t(xfs_dinode_get_rdev(dip));
		break;

	case S_IFREG:
	case S_IFLNK:
	case S_IFDIR:
		switch (dip->di_format) {
		case XFS_DINODE_FMT_LOCAL:
			di_size = be64_to_cpu(dip->di_size);
			size = (int)di_size;
			error = xfs_iformat_local(ip, dip, XFS_DATA_FORK, size);
			break;
		case XFS_DINODE_FMT_EXTENTS:
			error = xfs_iformat_extents(ip, dip, XFS_DATA_FORK);
			break;
		case XFS_DINODE_FMT_BTREE:
			error = xfs_iformat_btree(ip, dip, XFS_DATA_FORK);
			break;
		default:
			return -EFSCORRUPTED;
		}
		break;

	default:
		return -EFSCORRUPTED;
	}
	if (error)
		return error;

	if (xfs_is_reflink_inode(ip)) {
		ASSERT(ip->i_cowfp == NULL);
		xfs_ifork_init_cow(ip);
	}

	if (!XFS_DFORK_Q(dip))
		return 0;

	ASSERT(ip->i_afp == NULL);
	ip->i_afp = kmem_zone_zalloc(xfs_ifork_zone, KM_NOFS);

	switch (dip->di_aformat) {
	case XFS_DINODE_FMT_LOCAL:
		atp = (xfs_attr_shortform_t *)XFS_DFORK_APTR(dip);
		size = be16_to_cpu(atp->hdr.totsize);

		error = xfs_iformat_local(ip, dip, XFS_ATTR_FORK, size);
		break;
	case XFS_DINODE_FMT_EXTENTS:
		error = xfs_iformat_extents(ip, dip, XFS_ATTR_FORK);
		break;
	case XFS_DINODE_FMT_BTREE:
		error = xfs_iformat_btree(ip, dip, XFS_ATTR_FORK);
		break;
	default:
		error = -EFSCORRUPTED;
		break;
	}
	if (error) {
		kmem_zone_free(xfs_ifork_zone, ip->i_afp);
		ip->i_afp = NULL;
		if (ip->i_cowfp)
			kmem_zone_free(xfs_ifork_zone, ip->i_cowfp);
		ip->i_cowfp = NULL;
		xfs_idestroy_fork(ip, XFS_DATA_FORK);
	}
	return error;
}

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
	ifp->if_flags &= ~(XFS_IFEXTENTS | XFS_IFBROOT);
	ifp->if_flags |= XFS_IFINLINE;
}

/*
 * The file is in-lined in the on-disk inode.
 */
STATIC int
xfs_iformat_local(
	xfs_inode_t	*ip,
	xfs_dinode_t	*dip,
	int		whichfork,
	int		size)
{
	/*
	 * If the size is unreasonable, then something
	 * is wrong and we just bail out rather than crash in
	 * kmem_alloc() or memcpy() below.
	 */
	if (unlikely(size > XFS_DFORK_SIZE(dip, ip->i_mount, whichfork))) {
		xfs_warn(ip->i_mount,
	"corrupt inode %Lu (bad size %d for local fork, size = %d).",
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
	ifp->if_flags |= XFS_IFEXTENTS;
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
	xfs_inode_t		*ip,
	xfs_dinode_t		*dip,
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
	if (unlikely(XFS_IFORK_NEXTENTS(ip, whichfork) <=
					XFS_IFORK_MAXEXT(ip, whichfork) ||
		     nrecs == 0 ||
		     XFS_BMDR_SPACE_CALC(nrecs) >
					XFS_DFORK_SIZE(dip, mp, whichfork) ||
		     XFS_IFORK_NEXTENTS(ip, whichfork) > ip->i_d.di_nblocks) ||
		     level == 0 || level > XFS_BTREE_MAXLEVELS) {
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
	ifp->if_flags &= ~XFS_IFEXTENTS;
	ifp->if_flags |= XFS_IFBROOT;

	ifp->if_bytes = 0;
	ifp->if_u1.if_root = NULL;
	ifp->if_height = 0;
	return 0;
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
		ifp->if_broot = kmem_realloc(ifp->if_broot, new_size,
				KM_NOFS);
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
		ifp->if_flags &= ~XFS_IFBROOT;
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
	ifp->if_u1.if_data = kmem_realloc(ifp->if_u1.if_data,
			roundup(new_size, 4), KM_NOFS);
	ifp->if_bytes = new_size;
}

void
xfs_idestroy_fork(
	xfs_inode_t	*ip,
	int		whichfork)
{
	struct xfs_ifork	*ifp;

	ifp = XFS_IFORK_PTR(ip, whichfork);
	if (ifp->if_broot != NULL) {
		kmem_free(ifp->if_broot);
		ifp->if_broot = NULL;
	}

	/*
	 * If the format is local, then we can't have an extents
	 * array so just look for an inline data array.  If we're
	 * not local then we may or may not have an extents list,
	 * so check and free it up if we do.
	 */
	if (XFS_IFORK_FORMAT(ip, whichfork) == XFS_DINODE_FMT_LOCAL) {
		if (ifp->if_u1.if_data != NULL) {
			kmem_free(ifp->if_u1.if_data);
			ifp->if_u1.if_data = NULL;
		}
	} else if ((ifp->if_flags & XFS_IFEXTENTS) && ifp->if_height) {
		xfs_iext_destroy(ifp);
	}

	if (whichfork == XFS_ATTR_FORK) {
		kmem_zone_free(xfs_ifork_zone, ip->i_afp);
		ip->i_afp = NULL;
	} else if (whichfork == XFS_COW_FORK) {
		kmem_zone_free(xfs_ifork_zone, ip->i_cowfp);
		ip->i_cowfp = NULL;
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
	xfs_inode_t		*ip,
	xfs_dinode_t		*dip,
	xfs_inode_log_item_t	*iip,
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
	switch (XFS_IFORK_FORMAT(ip, whichfork)) {
	case XFS_DINODE_FMT_LOCAL:
		if ((iip->ili_fields & dataflag[whichfork]) &&
		    (ifp->if_bytes > 0)) {
			ASSERT(ifp->if_u1.if_data != NULL);
			ASSERT(ifp->if_bytes <= XFS_IFORK_SIZE(ip, whichfork));
			memcpy(cp, ifp->if_u1.if_data, ifp->if_bytes);
		}
		break;

	case XFS_DINODE_FMT_EXTENTS:
		ASSERT((ifp->if_flags & XFS_IFEXTENTS) ||
		       !(iip->ili_fields & extflag[whichfork]));
		if ((iip->ili_fields & extflag[whichfork]) &&
		    (ifp->if_bytes > 0)) {
			ASSERT(XFS_IFORK_NEXTENTS(ip, whichfork) > 0);
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

	ip->i_cowfp = kmem_zone_zalloc(xfs_ifork_zone,
				       KM_NOFS);
	ip->i_cowfp->if_flags = XFS_IFEXTENTS;
	ip->i_cformat = XFS_DINODE_FMT_EXTENTS;
	ip->i_cnextents = 0;
}

/* Default fork content verifiers. */
struct xfs_ifork_ops xfs_default_ifork_ops = {
	.verify_attr	= xfs_attr_shortform_verify,
	.verify_dir	= xfs_dir2_sf_verify,
	.verify_symlink	= xfs_symlink_shortform_verify,
};

/* Verify the inline contents of the data fork of an inode. */
xfs_failaddr_t
xfs_ifork_verify_data(
	struct xfs_inode	*ip,
	struct xfs_ifork_ops	*ops)
{
	/* Non-local data fork, we're done. */
	if (ip->i_d.di_format != XFS_DINODE_FMT_LOCAL)
		return NULL;

	/* Check the inline data fork if there is one. */
	switch (VFS_I(ip)->i_mode & S_IFMT) {
	case S_IFDIR:
		return ops->verify_dir(ip);
	case S_IFLNK:
		return ops->verify_symlink(ip);
	default:
		return NULL;
	}
}

/* Verify the inline contents of the attr fork of an inode. */
xfs_failaddr_t
xfs_ifork_verify_attr(
	struct xfs_inode	*ip,
	struct xfs_ifork_ops	*ops)
{
	/* There has to be an attr fork allocated if aformat is local. */
	if (ip->i_d.di_aformat != XFS_DINODE_FMT_LOCAL)
		return NULL;
	if (!XFS_IFORK_PTR(ip, XFS_ATTR_FORK))
		return __this_address;
	return ops->verify_attr(ip);
}
