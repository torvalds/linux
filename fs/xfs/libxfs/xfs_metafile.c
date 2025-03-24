// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2018-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_trans.h"
#include "xfs_metafile.h"
#include "xfs_trace.h"
#include "xfs_inode.h"
#include "xfs_quota.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_alloc.h"

static const struct {
	enum xfs_metafile_type	mtype;
	const char		*name;
} xfs_metafile_type_strs[] = { XFS_METAFILE_TYPE_STR };

const char *
xfs_metafile_type_str(enum xfs_metafile_type metatype)
{
	unsigned int	i;

	for (i = 0; i < ARRAY_SIZE(xfs_metafile_type_strs); i++) {
		if (xfs_metafile_type_strs[i].mtype == metatype)
			return xfs_metafile_type_strs[i].name;
	}

	return NULL;
}

/* Set up an inode to be recognized as a metadata directory inode. */
void
xfs_metafile_set_iflag(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	enum xfs_metafile_type	metafile_type)
{
	VFS_I(ip)->i_mode &= ~0777;
	VFS_I(ip)->i_uid = GLOBAL_ROOT_UID;
	VFS_I(ip)->i_gid = GLOBAL_ROOT_GID;
	if (S_ISDIR(VFS_I(ip)->i_mode))
		ip->i_diflags |= XFS_METADIR_DIFLAGS;
	else
		ip->i_diflags |= XFS_METAFILE_DIFLAGS;
	ip->i_diflags2 &= ~XFS_DIFLAG2_DAX;
	ip->i_diflags2 |= XFS_DIFLAG2_METADATA;
	ip->i_metatype = metafile_type;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
}

/* Clear the metadata directory inode flag. */
void
xfs_metafile_clear_iflag(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip)
{
	ASSERT(xfs_is_metadir_inode(ip));
	ASSERT(VFS_I(ip)->i_nlink == 0);

	ip->i_diflags2 &= ~XFS_DIFLAG2_METADATA;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
}

/*
 * Is the amount of space that could be allocated towards a given metadata
 * file at or beneath a certain threshold?
 */
static inline bool
xfs_metafile_resv_can_cover(
	struct xfs_inode	*ip,
	int64_t			rhs)
{
	/*
	 * The amount of space that can be allocated to this metadata file is
	 * the remaining reservation for the particular metadata file + the
	 * global free block count.  Take care of the first case to avoid
	 * touching the per-cpu counter.
	 */
	if (ip->i_delayed_blks >= rhs)
		return true;

	/*
	 * There aren't enough blocks left in the inode's reservation, but it
	 * isn't critical unless there also isn't enough free space.
	 */
	return __percpu_counter_compare(&ip->i_mount->m_fdblocks,
			rhs - ip->i_delayed_blks, 2048) >= 0;
}

/*
 * Is this metadata file critically low on blocks?  For now we'll define that
 * as the number of blocks we can get our hands on being less than 10% of what
 * we reserved or less than some arbitrary number (maximum btree height).
 */
bool
xfs_metafile_resv_critical(
	struct xfs_inode	*ip)
{
	uint64_t		asked_low_water;

	if (!ip)
		return false;

	ASSERT(xfs_is_metadir_inode(ip));
	trace_xfs_metafile_resv_critical(ip, 0);

	if (!xfs_metafile_resv_can_cover(ip, ip->i_mount->m_rtbtree_maxlevels))
		return true;

	asked_low_water = div_u64(ip->i_meta_resv_asked, 10);
	if (!xfs_metafile_resv_can_cover(ip, asked_low_water))
		return true;

	return XFS_TEST_ERROR(false, ip->i_mount,
			XFS_ERRTAG_METAFILE_RESV_CRITICAL);
}

/* Allocate a block from the metadata file's reservation. */
void
xfs_metafile_resv_alloc_space(
	struct xfs_inode	*ip,
	struct xfs_alloc_arg	*args)
{
	int64_t			len = args->len;

	ASSERT(xfs_is_metadir_inode(ip));
	ASSERT(args->resv == XFS_AG_RESV_METAFILE);

	trace_xfs_metafile_resv_alloc_space(ip, args->len);

	/*
	 * Allocate the blocks from the metadata inode's block reservation
	 * and update the ondisk sb counter.
	 */
	if (ip->i_delayed_blks > 0) {
		int64_t		from_resv;

		from_resv = min_t(int64_t, len, ip->i_delayed_blks);
		ip->i_delayed_blks -= from_resv;
		xfs_mod_delalloc(ip, 0, -from_resv);
		xfs_trans_mod_sb(args->tp, XFS_TRANS_SB_RES_FDBLOCKS,
				-from_resv);
		len -= from_resv;
	}

	/*
	 * Any allocation in excess of the reservation requires in-core and
	 * on-disk fdblocks updates.  If we can grab @len blocks from the
	 * in-core fdblocks then all we need to do is update the on-disk
	 * superblock; if not, then try to steal some from the transaction's
	 * block reservation.  Overruns are only expected for rmap btrees.
	 */
	if (len) {
		unsigned int	field;
		int		error;

		error = xfs_dec_fdblocks(ip->i_mount, len, true);
		if (error)
			field = XFS_TRANS_SB_FDBLOCKS;
		else
			field = XFS_TRANS_SB_RES_FDBLOCKS;

		xfs_trans_mod_sb(args->tp, field, -len);
	}

	ip->i_nblocks += args->len;
	xfs_trans_log_inode(args->tp, ip, XFS_ILOG_CORE);
}

/* Free a block to the metadata file's reservation. */
void
xfs_metafile_resv_free_space(
	struct xfs_inode	*ip,
	struct xfs_trans	*tp,
	xfs_filblks_t		len)
{
	int64_t			to_resv;

	ASSERT(xfs_is_metadir_inode(ip));
	trace_xfs_metafile_resv_free_space(ip, len);

	ip->i_nblocks -= len;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	/*
	 * Add the freed blocks back into the inode's delalloc reservation
	 * until it reaches the maximum size.  Update the ondisk fdblocks only.
	 */
	to_resv = ip->i_meta_resv_asked - (ip->i_nblocks + ip->i_delayed_blks);
	if (to_resv > 0) {
		to_resv = min_t(int64_t, to_resv, len);
		ip->i_delayed_blks += to_resv;
		xfs_mod_delalloc(ip, 0, to_resv);
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_RES_FDBLOCKS, to_resv);
		len -= to_resv;
	}

	/*
	 * Everything else goes back to the filesystem, so update the in-core
	 * and on-disk counters.
	 */
	if (len)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_FDBLOCKS, len);
}

/* Release a metadata file's space reservation. */
void
xfs_metafile_resv_free(
	struct xfs_inode	*ip)
{
	/* Non-btree metadata inodes don't need space reservations. */
	if (!ip || !ip->i_meta_resv_asked)
		return;

	ASSERT(xfs_is_metadir_inode(ip));
	trace_xfs_metafile_resv_free(ip, 0);

	if (ip->i_delayed_blks) {
		xfs_mod_delalloc(ip, 0, -ip->i_delayed_blks);
		xfs_add_fdblocks(ip->i_mount, ip->i_delayed_blks);
		ip->i_delayed_blks = 0;
	}
	ip->i_meta_resv_asked = 0;
}

/* Set up a metadata file's space reservation. */
int
xfs_metafile_resv_init(
	struct xfs_inode	*ip,
	xfs_filblks_t		ask)
{
	xfs_filblks_t		hidden_space;
	xfs_filblks_t		used;
	int			error;

	if (!ip || ip->i_meta_resv_asked > 0)
		return 0;

	ASSERT(xfs_is_metadir_inode(ip));

	/*
	 * Space taken by all other metadata btrees are accounted on-disk as
	 * used space.  We therefore only hide the space that is reserved but
	 * not used by the trees.
	 */
	used = ip->i_nblocks;
	if (used > ask)
		ask = used;
	hidden_space = ask - used;

	error = xfs_dec_fdblocks(ip->i_mount, hidden_space, true);
	if (error) {
		trace_xfs_metafile_resv_init_error(ip, error, _RET_IP_);
		return error;
	}

	xfs_mod_delalloc(ip, 0, hidden_space);
	ip->i_delayed_blks = hidden_space;
	ip->i_meta_resv_asked = ask;

	trace_xfs_metafile_resv_init(ip, ask);
	return 0;
}
