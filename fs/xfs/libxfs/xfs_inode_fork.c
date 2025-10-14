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
#include "xfs_health.h"
#include "xfs_symlink_remote.h"
#include "xfs_rtrmap_btree.h"
#include "xfs_rtrefcount_btree.h"

struct kmem_cache *xfs_ifork_cache;

void
xfs_init_local_fork(
	struct xfs_inode	*ip,
	int			whichfork,
	const void		*data,
	int64_t			size)
{
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, whichfork);
	int			mem_size = size;
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
		char *new_data = kmalloc(mem_size,
				GFP_KERNEL | __GFP_NOLOCKDEP | __GFP_NOFAIL);

		memcpy(new_data, data, size);
		if (zero_terminate)
			new_data[size] = '\0';

		ifp->if_data = new_data;
	} else {
		ifp->if_data = NULL;
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
	 * kmalloc() or memcpy() below.
	 */
	if (unlikely(size > XFS_DFORK_SIZE(dip, ip->i_mount, whichfork))) {
		xfs_warn(ip->i_mount,
	"corrupt inode %llu (bad size %d for local fork, size = %zd).",
			(unsigned long long) ip->i_ino, size,
			XFS_DFORK_SIZE(dip, ip->i_mount, whichfork));
		xfs_inode_verifier_error(ip, -EFSCORRUPTED,
				"xfs_iformat_local", dip, sizeof(*dip),
				__this_address);
		xfs_inode_mark_sick(ip, XFS_SICK_INO_CORE);
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
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, whichfork);
	int			state = xfs_bmap_fork_to_state(whichfork);
	xfs_extnum_t		nex = xfs_dfork_nextents(dip, whichfork);
	int			size = nex * sizeof(xfs_bmbt_rec_t);
	struct xfs_iext_cursor	icur;
	struct xfs_bmbt_rec	*dp;
	struct xfs_bmbt_irec	new;
	int			i;

	/*
	 * If the number of extents is unreasonable, then something is wrong and
	 * we just bail out rather than crash in kmalloc() or memcpy() below.
	 */
	if (unlikely(size < 0 || size > XFS_DFORK_SIZE(dip, mp, whichfork))) {
		xfs_warn(ip->i_mount, "corrupt inode %llu ((a)extents = %llu).",
			ip->i_ino, nex);
		xfs_inode_verifier_error(ip, -EFSCORRUPTED,
				"xfs_iformat_extents(1)", dip, sizeof(*dip),
				__this_address);
		xfs_inode_mark_sick(ip, XFS_SICK_INO_CORE);
		return -EFSCORRUPTED;
	}

	ifp->if_bytes = 0;
	ifp->if_data = NULL;
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
				xfs_inode_mark_sick(ip, XFS_SICK_INO_CORE);
				return xfs_bmap_complain_bad_rec(ip, whichfork,
						fa, &new);
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
	struct xfs_btree_block	*broot;
	int			nrecs;
	int			size;
	int			level;

	ifp = xfs_ifork_ptr(ip, whichfork);
	dfp = (xfs_bmdr_block_t *)XFS_DFORK_PTR(dip, whichfork);
	size = xfs_bmap_broot_space(mp, dfp);
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
		     xfs_bmdr_space_calc(nrecs) >
					XFS_DFORK_SIZE(dip, mp, whichfork) ||
		     ifp->if_nextents > ip->i_nblocks) ||
		     level == 0 || level > XFS_BM_MAXLEVELS(mp, whichfork)) {
		xfs_warn(mp, "corrupt inode %llu (btree).",
					(unsigned long long) ip->i_ino);
		xfs_inode_verifier_error(ip, -EFSCORRUPTED,
				"xfs_iformat_btree", dfp, size,
				__this_address);
		xfs_inode_mark_sick(ip, XFS_SICK_INO_CORE);
		return -EFSCORRUPTED;
	}

	broot = xfs_broot_alloc(ifp, size);
	/*
	 * Copy and convert from the on-disk structure
	 * to the in-memory structure.
	 */
	xfs_bmdr_to_bmbt(ip, dfp, XFS_DFORK_SIZE(dip, ip->i_mount, whichfork),
			 broot, size);

	ifp->if_bytes = 0;
	ifp->if_data = NULL;
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
	 * depend on it.  Use release semantics to set needextents /after/ we
	 * set the format. This ensures that we can use acquire semantics on
	 * needextents in xfs_need_iread_extents() and be guaranteed to see a
	 * valid format value after that load.
	 */
	ip->i_df.if_format = dip->di_format;
	ip->i_df.if_nextents = xfs_dfork_data_extents(dip);
	smp_store_release(&ip->i_df.if_needextents,
			   ip->i_df.if_format == XFS_DINODE_FMT_BTREE ? 1 : 0);

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
		case XFS_DINODE_FMT_META_BTREE:
			switch (ip->i_metatype) {
			case XFS_METAFILE_RTRMAP:
				return xfs_iformat_rtrmap(ip, dip);
			case XFS_METAFILE_RTREFCOUNT:
				return xfs_iformat_rtrefcount(ip, dip);
			default:
				break;
			}
			fallthrough;
		default:
			xfs_inode_verifier_error(ip, -EFSCORRUPTED, __func__,
					dip, sizeof(*dip), __this_address);
			xfs_inode_mark_sick(ip, XFS_SICK_INO_CORE);
			return -EFSCORRUPTED;
		}
		break;
	default:
		xfs_inode_verifier_error(ip, -EFSCORRUPTED, __func__, dip,
				sizeof(*dip), __this_address);
		xfs_inode_mark_sick(ip, XFS_SICK_INO_CORE);
		return -EFSCORRUPTED;
	}
}

static uint16_t
xfs_dfork_attr_shortform_size(
	struct xfs_dinode		*dip)
{
	struct xfs_attr_sf_hdr		*sf = XFS_DFORK_APTR(dip);

	return be16_to_cpu(sf->totsize);
}

void
xfs_ifork_init_attr(
	struct xfs_inode	*ip,
	enum xfs_dinode_fmt	format,
	xfs_extnum_t		nextents)
{
	/*
	 * Initialize the extent count early, as the per-format routines may
	 * depend on it.  Use release semantics to set needextents /after/ we
	 * set the format. This ensures that we can use acquire semantics on
	 * needextents in xfs_need_iread_extents() and be guaranteed to see a
	 * valid format value after that load.
	 */
	ip->i_af.if_format = format;
	ip->i_af.if_nextents = nextents;
	smp_store_release(&ip->i_af.if_needextents,
			   ip->i_af.if_format == XFS_DINODE_FMT_BTREE ? 1 : 0);
}

void
xfs_ifork_zap_attr(
	struct xfs_inode	*ip)
{
	xfs_idestroy_fork(&ip->i_af);
	memset(&ip->i_af, 0, sizeof(struct xfs_ifork));
	ip->i_af.if_format = XFS_DINODE_FMT_EXTENTS;
}

int
xfs_iformat_attr_fork(
	struct xfs_inode	*ip,
	struct xfs_dinode	*dip)
{
	xfs_extnum_t		naextents = xfs_dfork_attr_extents(dip);
	int			error = 0;

	/*
	 * Initialize the extent count early, as the per-format routines may
	 * depend on it.
	 */
	xfs_ifork_init_attr(ip, dip->di_aformat, naextents);

	switch (ip->i_af.if_format) {
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
		xfs_inode_mark_sick(ip, XFS_SICK_INO_CORE);
		error = -EFSCORRUPTED;
		break;
	}

	if (error)
		xfs_ifork_zap_attr(ip);
	return error;
}

/*
 * Allocate the if_broot component of an inode fork so that it is @new_size
 * bytes in size, using __GFP_NOLOCKDEP like all the other code that
 * initializes a broot during inode load.  Returns if_broot.
 */
struct xfs_btree_block *
xfs_broot_alloc(
	struct xfs_ifork	*ifp,
	size_t			new_size)
{
	ASSERT(ifp->if_broot == NULL);

	ifp->if_broot = kmalloc(new_size,
				GFP_KERNEL | __GFP_NOLOCKDEP | __GFP_NOFAIL);
	ifp->if_broot_bytes = new_size;
	return ifp->if_broot;
}

/*
 * Reallocate the if_broot component of an inode fork so that it is @new_size
 * bytes in size.  Returns if_broot.
 */
struct xfs_btree_block *
xfs_broot_realloc(
	struct xfs_ifork	*ifp,
	size_t			new_size)
{
	/* No size change?  No action needed. */
	if (new_size == ifp->if_broot_bytes)
		return ifp->if_broot;

	/* New size is zero, free it. */
	if (new_size == 0) {
		ifp->if_broot_bytes = 0;
		kfree(ifp->if_broot);
		ifp->if_broot = NULL;
		return NULL;
	}

	/*
	 * Shrinking the iroot means we allocate a new smaller object and copy
	 * it.  We don't trust krealloc not to nop on realloc-down.
	 */
	if (ifp->if_broot_bytes > 0 && ifp->if_broot_bytes > new_size) {
		struct xfs_btree_block	*old_broot = ifp->if_broot;

		ifp->if_broot = kmalloc(new_size, GFP_KERNEL | __GFP_NOFAIL);
		ifp->if_broot_bytes = new_size;
		memcpy(ifp->if_broot, old_broot, new_size);
		kfree(old_broot);
		return ifp->if_broot;
	}

	/*
	 * Growing the iroot means we can krealloc.  This may get us the same
	 * object.
	 */
	ifp->if_broot = krealloc(ifp->if_broot, new_size,
			GFP_KERNEL | __GFP_NOFAIL);
	ifp->if_broot_bytes = new_size;
	return ifp->if_broot;
}

/*
 * This is called when the amount of space needed for if_data
 * is increased or decreased.  The change in size is indicated by
 * the number of bytes that need to be added or deleted in the
 * byte_diff parameter.
 *
 * If the amount of space needed has decreased below the size of the
 * inline buffer, then switch to using the inline buffer.  Otherwise,
 * use krealloc() or kmalloc() to adjust the size of the buffer
 * to what is needed.
 *
 * ip -- the inode whose if_data area is changing
 * byte_diff -- the change in the number of bytes, positive or negative,
 *	 requested for the if_data array.
 */
void *
xfs_idata_realloc(
	struct xfs_inode	*ip,
	int64_t			byte_diff,
	int			whichfork)
{
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, whichfork);
	int64_t			new_size = ifp->if_bytes + byte_diff;

	ASSERT(new_size >= 0);
	ASSERT(new_size <= xfs_inode_fork_size(ip, whichfork));

	if (byte_diff) {
		ifp->if_data = krealloc(ifp->if_data, new_size,
					GFP_KERNEL | __GFP_NOFAIL);
		if (new_size == 0)
			ifp->if_data = NULL;
		ifp->if_bytes = new_size;
	}

	return ifp->if_data;
}

/* Free all memory and reset a fork back to its initial state. */
void
xfs_idestroy_fork(
	struct xfs_ifork	*ifp)
{
	if (ifp->if_broot != NULL) {
		kfree(ifp->if_broot);
		ifp->if_broot = NULL;
	}

	switch (ifp->if_format) {
	case XFS_DINODE_FMT_LOCAL:
		kfree(ifp->if_data);
		ifp->if_data = NULL;
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
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, whichfork);
	struct xfs_iext_cursor	icur;
	struct xfs_bmbt_irec	rec;
	int64_t			copied = 0;

	xfs_assert_ilocked(ip, XFS_ILOCK_EXCL | XFS_ILOCK_SHARED);
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
	ifp = xfs_ifork_ptr(ip, whichfork);
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
			ASSERT(ifp->if_data != NULL);
			ASSERT(ifp->if_bytes <= xfs_inode_fork_size(ip, whichfork));
			memcpy(cp, ifp->if_data, ifp->if_bytes);
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
			ASSERT(xfs_bmap_bmdr_space(ifp->if_broot) <=
			        xfs_inode_fork_size(ip, whichfork));
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

	case XFS_DINODE_FMT_META_BTREE:
		ASSERT(whichfork == XFS_DATA_FORK);

		if (!(iip->ili_fields & brootflag[whichfork]))
			break;

		switch (ip->i_metatype) {
		case XFS_METAFILE_RTRMAP:
			xfs_iflush_rtrmap(ip, dip);
			break;
		case XFS_METAFILE_RTREFCOUNT:
			xfs_iflush_rtrefcount(ip, dip);
			break;
		default:
			ASSERT(0);
			break;
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
		return &ip->i_af;
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
				GFP_KERNEL | __GFP_NOLOCKDEP | __GFP_NOFAIL);
	ip->i_cowfp->if_format = XFS_DINODE_FMT_EXTENTS;
}

/* Verify the inline contents of the data fork of an inode. */
int
xfs_ifork_verify_local_data(
	struct xfs_inode	*ip)
{
	xfs_failaddr_t		fa = NULL;

	switch (VFS_I(ip)->i_mode & S_IFMT) {
	case S_IFDIR: {
		struct xfs_mount	*mp = ip->i_mount;
		struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, XFS_DATA_FORK);
		struct xfs_dir2_sf_hdr	*sfp = ifp->if_data;

		fa = xfs_dir2_sf_verify(mp, sfp, ifp->if_bytes);
		break;
	}
	case S_IFLNK: {
		struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, XFS_DATA_FORK);

		fa = xfs_symlink_shortform_verify(ifp->if_data, ifp->if_bytes);
		break;
	}
	default:
		break;
	}

	if (fa) {
		xfs_inode_verifier_error(ip, -EFSCORRUPTED, "data fork",
				ip->i_df.if_data, ip->i_df.if_bytes, fa);
		return -EFSCORRUPTED;
	}

	return 0;
}

/* Verify the inline contents of the attr fork of an inode. */
int
xfs_ifork_verify_local_attr(
	struct xfs_inode	*ip)
{
	struct xfs_ifork	*ifp = &ip->i_af;
	xfs_failaddr_t		fa;

	if (!xfs_inode_has_attr_fork(ip)) {
		fa = __this_address;
	} else {
		struct xfs_ifork		*ifp = &ip->i_af;

		ASSERT(ifp->if_format == XFS_DINODE_FMT_LOCAL);
		fa = xfs_attr_shortform_verify(ifp->if_data, ifp->if_bytes);
	}
	if (fa) {
		xfs_inode_verifier_error(ip, -EFSCORRUPTED, "attr fork",
				ifp->if_data, ifp->if_bytes, fa);
		return -EFSCORRUPTED;
	}

	return 0;
}

/*
 * Check if the inode fork supports adding nr_to_add more extents.
 *
 * If it doesn't but we can upgrade it to large extent counters, do the upgrade.
 * If we can't upgrade or are already using big counters but still can't fit the
 * additional extents, return -EFBIG.
 */
int
xfs_iext_count_extend(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	int			whichfork,
	uint			nr_to_add)
{
	struct xfs_mount	*mp = ip->i_mount;
	bool			has_large =
		xfs_inode_has_large_extent_counts(ip);
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, whichfork);
	uint64_t		nr_exts;

	ASSERT(nr_to_add <= XFS_MAX_EXTCNT_UPGRADE_NR);

	if (whichfork == XFS_COW_FORK)
		return 0;

	/* no point in upgrading if if_nextents overflows */
	nr_exts = ifp->if_nextents + nr_to_add;
	if (nr_exts < ifp->if_nextents)
		return -EFBIG;

	if (XFS_TEST_ERROR(mp, XFS_ERRTAG_REDUCE_MAX_IEXTENTS) && nr_exts > 10)
		return -EFBIG;

	if (nr_exts > xfs_iext_max_nextents(has_large, whichfork)) {
		if (has_large || !xfs_has_large_extent_counts(mp))
			return -EFBIG;
		ip->i_diflags2 |= XFS_DIFLAG2_NREXT64;
		xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	}
	return 0;
}

/* Decide if a file mapping is on the realtime device or not. */
bool
xfs_ifork_is_realtime(
	struct xfs_inode	*ip,
	int			whichfork)
{
	return XFS_IS_REALTIME_INODE(ip) && whichfork != XFS_ATTR_FORK;
}
