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
#include "xfs_rtgroup.h"
#include "xfs_rtrmap_btree.h"
#include "xfs_rtrefcount_btree.h"

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
 * Is the metafile reservations at or beneath a certain threshold?
 */
static inline bool
xfs_metafile_resv_can_cover(
	struct xfs_mount	*mp,
	int64_t			rhs)
{
	/*
	 * The amount of space that can be allocated to this metadata file is
	 * the remaining reservation for the particular metadata file + the
	 * global free block count.  Take care of the first case to avoid
	 * touching the per-cpu counter.
	 */
	if (mp->m_metafile_resv_avail >= rhs)
		return true;

	/*
	 * There aren't enough blocks left in the inode's reservation, but it
	 * isn't critical unless there also isn't enough free space.
	 */
	return xfs_compare_freecounter(mp, XC_FREE_BLOCKS,
			rhs - mp->m_metafile_resv_avail, 2048) >= 0;
}

/*
 * Is the metafile reservation critically low on blocks?  For now we'll define
 * that as the number of blocks we can get our hands on being less than 10% of
 * what we reserved or less than some arbitrary number (maximum btree height).
 */
bool
xfs_metafile_resv_critical(
	struct xfs_mount	*mp)
{
	ASSERT(xfs_has_metadir(mp));

	trace_xfs_metafile_resv_critical(mp, 0);

	if (!xfs_metafile_resv_can_cover(mp, mp->m_rtbtree_maxlevels))
		return true;

	if (!xfs_metafile_resv_can_cover(mp,
			div_u64(mp->m_metafile_resv_target, 10)))
		return true;

	return XFS_TEST_ERROR(mp, XFS_ERRTAG_METAFILE_RESV_CRITICAL);
}

/* Allocate a block from the metadata file's reservation. */
void
xfs_metafile_resv_alloc_space(
	struct xfs_inode	*ip,
	struct xfs_alloc_arg	*args)
{
	struct xfs_mount	*mp = ip->i_mount;
	int64_t			len = args->len;

	ASSERT(xfs_is_metadir_inode(ip));
	ASSERT(args->resv == XFS_AG_RESV_METAFILE);

	trace_xfs_metafile_resv_alloc_space(mp, args->len);

	/*
	 * Allocate the blocks from the metadata inode's block reservation
	 * and update the ondisk sb counter.
	 */
	mutex_lock(&mp->m_metafile_resv_lock);
	if (mp->m_metafile_resv_avail > 0) {
		int64_t		from_resv;

		from_resv = min_t(int64_t, len, mp->m_metafile_resv_avail);
		mp->m_metafile_resv_avail -= from_resv;
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

	mp->m_metafile_resv_used += args->len;
	mutex_unlock(&mp->m_metafile_resv_lock);

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
	struct xfs_mount	*mp = ip->i_mount;
	int64_t			to_resv;

	ASSERT(xfs_is_metadir_inode(ip));

	trace_xfs_metafile_resv_free_space(mp, len);

	ip->i_nblocks -= len;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	mutex_lock(&mp->m_metafile_resv_lock);
	mp->m_metafile_resv_used -= len;

	/*
	 * Add the freed blocks back into the inode's delalloc reservation
	 * until it reaches the maximum size.  Update the ondisk fdblocks only.
	 */
	to_resv = mp->m_metafile_resv_target -
		(mp->m_metafile_resv_used + mp->m_metafile_resv_avail);
	if (to_resv > 0) {
		to_resv = min_t(int64_t, to_resv, len);
		mp->m_metafile_resv_avail += to_resv;
		xfs_mod_delalloc(ip, 0, to_resv);
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_RES_FDBLOCKS, to_resv);
		len -= to_resv;
	}
	mutex_unlock(&mp->m_metafile_resv_lock);

	/*
	 * Everything else goes back to the filesystem, so update the in-core
	 * and on-disk counters.
	 */
	if (len)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_FDBLOCKS, len);
}

static void
__xfs_metafile_resv_free(
	struct xfs_mount	*mp)
{
	if (mp->m_metafile_resv_avail) {
		xfs_mod_sb_delalloc(mp, -(int64_t)mp->m_metafile_resv_avail);
		xfs_add_fdblocks(mp, mp->m_metafile_resv_avail);
	}
	mp->m_metafile_resv_avail = 0;
	mp->m_metafile_resv_used = 0;
	mp->m_metafile_resv_target = 0;
}

/* Release unused metafile space reservation. */
void
xfs_metafile_resv_free(
	struct xfs_mount	*mp)
{
	if (!xfs_has_metadir(mp))
		return;

	trace_xfs_metafile_resv_free(mp, 0);

	mutex_lock(&mp->m_metafile_resv_lock);
	__xfs_metafile_resv_free(mp);
	mutex_unlock(&mp->m_metafile_resv_lock);
}

/* Set up a metafile space reservation. */
int
xfs_metafile_resv_init(
	struct xfs_mount	*mp)
{
	struct xfs_rtgroup	*rtg = NULL;
	xfs_filblks_t		used = 0, target = 0;
	xfs_filblks_t		hidden_space;
	xfs_rfsblock_t		dblocks_avail = mp->m_sb.sb_dblocks / 4;
	int			error = 0;

	if (!xfs_has_metadir(mp))
		return 0;

	/*
	 * Free any previous reservation to have a clean slate.
	 */
	mutex_lock(&mp->m_metafile_resv_lock);
	__xfs_metafile_resv_free(mp);

	/*
	 * Currently the only btree metafiles that require reservations are the
	 * rtrmap and the rtrefcount.  Anything new will have to be added here
	 * as well.
	 */
	while ((rtg = xfs_rtgroup_next(mp, rtg))) {
		if (xfs_has_rtrmapbt(mp)) {
			used += rtg_rmap(rtg)->i_nblocks;
			target += xfs_rtrmapbt_calc_reserves(mp);
		}
		if (xfs_has_rtreflink(mp)) {
			used += rtg_refcount(rtg)->i_nblocks;
			target += xfs_rtrefcountbt_calc_reserves(mp);
		}
	}

	if (!target)
		goto out_unlock;

	/*
	 * Space taken by the per-AG metadata btrees are accounted on-disk as
	 * used space.  We therefore only hide the space that is reserved but
	 * not used by the trees.
	 */
	if (used > target)
		target = used;
	else if (target > dblocks_avail)
		target = dblocks_avail;
	hidden_space = target - used;

	error = xfs_dec_fdblocks(mp, hidden_space, true);
	if (error) {
		trace_xfs_metafile_resv_init_error(mp, 0);
		goto out_unlock;
	}

	xfs_mod_sb_delalloc(mp, hidden_space);

	mp->m_metafile_resv_target = target;
	mp->m_metafile_resv_used = used;
	mp->m_metafile_resv_avail = hidden_space;

	trace_xfs_metafile_resv_init(mp, target);

out_unlock:
	mutex_unlock(&mp->m_metafile_resv_lock);
	return error;
}
