// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Fujitsu.  All Rights Reserved.
 */

#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_alloc.h"
#include "xfs_bit.h"
#include "xfs_btree.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_rtalloc.h"
#include "xfs_trans.h"
#include "xfs_ag.h"

#include <linux/mm.h>
#include <linux/dax.h>
#include <linux/fs.h>

struct xfs_failure_info {
	xfs_agblock_t		startblock;
	xfs_extlen_t		blockcount;
	int			mf_flags;
	bool			want_shutdown;
};

static pgoff_t
xfs_failure_pgoff(
	struct xfs_mount		*mp,
	const struct xfs_rmap_irec	*rec,
	const struct xfs_failure_info	*notify)
{
	loff_t				pos = XFS_FSB_TO_B(mp, rec->rm_offset);

	if (notify->startblock > rec->rm_startblock)
		pos += XFS_FSB_TO_B(mp,
				notify->startblock - rec->rm_startblock);
	return pos >> PAGE_SHIFT;
}

static unsigned long
xfs_failure_pgcnt(
	struct xfs_mount		*mp,
	const struct xfs_rmap_irec	*rec,
	const struct xfs_failure_info	*notify)
{
	xfs_agblock_t			end_rec;
	xfs_agblock_t			end_notify;
	xfs_agblock_t			start_cross;
	xfs_agblock_t			end_cross;

	start_cross = max(rec->rm_startblock, notify->startblock);

	end_rec = rec->rm_startblock + rec->rm_blockcount;
	end_notify = notify->startblock + notify->blockcount;
	end_cross = min(end_rec, end_notify);

	return XFS_FSB_TO_B(mp, end_cross - start_cross) >> PAGE_SHIFT;
}

static int
xfs_dax_failure_fn(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*data)
{
	struct xfs_mount		*mp = cur->bc_mp;
	struct xfs_inode		*ip;
	struct xfs_failure_info		*notify = data;
	struct address_space		*mapping;
	pgoff_t				pgoff;
	unsigned long			pgcnt;
	int				error = 0;

	if (XFS_RMAP_NON_INODE_OWNER(rec->rm_owner) ||
	    (rec->rm_flags & (XFS_RMAP_ATTR_FORK | XFS_RMAP_BMBT_BLOCK))) {
		/* Continue the query because this isn't a failure. */
		if (notify->mf_flags & MF_MEM_PRE_REMOVE)
			return 0;
		notify->want_shutdown = true;
		return 0;
	}

	/* Get files that incore, filter out others that are not in use. */
	error = xfs_iget(mp, cur->bc_tp, rec->rm_owner, XFS_IGET_INCORE,
			 0, &ip);
	/* Continue the rmap query if the inode isn't incore */
	if (error == -ENODATA)
		return 0;
	if (error) {
		notify->want_shutdown = true;
		return 0;
	}

	mapping = VFS_I(ip)->i_mapping;
	pgoff = xfs_failure_pgoff(mp, rec, notify);
	pgcnt = xfs_failure_pgcnt(mp, rec, notify);

	/* Continue the rmap query if the inode isn't a dax file. */
	if (dax_mapping(mapping))
		error = mf_dax_kill_procs(mapping, pgoff, pgcnt,
					  notify->mf_flags);

	/* Invalidate the cache in dax pages. */
	if (notify->mf_flags & MF_MEM_PRE_REMOVE)
		invalidate_inode_pages2_range(mapping, pgoff,
					      pgoff + pgcnt - 1);

	xfs_irele(ip);
	return error;
}

static int
xfs_dax_notify_failure_freeze(
	struct xfs_mount	*mp)
{
	struct super_block	*sb = mp->m_super;
	int			error;

	error = freeze_super(sb, FREEZE_HOLDER_KERNEL);
	if (error)
		xfs_emerg(mp, "already frozen by kernel, err=%d", error);

	return error;
}

static void
xfs_dax_notify_failure_thaw(
	struct xfs_mount	*mp,
	bool			kernel_frozen)
{
	struct super_block	*sb = mp->m_super;
	int			error;

	if (kernel_frozen) {
		error = thaw_super(sb, FREEZE_HOLDER_KERNEL);
		if (error)
			xfs_emerg(mp, "still frozen after notify failure, err=%d",
				error);
	}

	/*
	 * Also thaw userspace call anyway because the device is about to be
	 * removed immediately.
	 */
	thaw_super(sb, FREEZE_HOLDER_USERSPACE);
}

static int
xfs_dax_notify_ddev_failure(
	struct xfs_mount	*mp,
	xfs_daddr_t		daddr,
	xfs_daddr_t		bblen,
	int			mf_flags)
{
	struct xfs_failure_info	notify = { .mf_flags = mf_flags };
	struct xfs_trans	*tp = NULL;
	struct xfs_btree_cur	*cur = NULL;
	struct xfs_buf		*agf_bp = NULL;
	int			error = 0;
	bool			kernel_frozen = false;
	xfs_fsblock_t		fsbno = XFS_DADDR_TO_FSB(mp, daddr);
	xfs_agnumber_t		agno = XFS_FSB_TO_AGNO(mp, fsbno);
	xfs_fsblock_t		end_fsbno = XFS_DADDR_TO_FSB(mp,
							     daddr + bblen - 1);
	xfs_agnumber_t		end_agno = XFS_FSB_TO_AGNO(mp, end_fsbno);

	if (mf_flags & MF_MEM_PRE_REMOVE) {
		xfs_info(mp, "Device is about to be removed!");
		/*
		 * Freeze fs to prevent new mappings from being created.
		 * - Keep going on if others already hold the kernel forzen.
		 * - Keep going on if other errors too because this device is
		 *   starting to fail.
		 * - If kernel frozen state is hold successfully here, thaw it
		 *   here as well at the end.
		 */
		kernel_frozen = xfs_dax_notify_failure_freeze(mp) == 0;
	}

	error = xfs_trans_alloc_empty(mp, &tp);
	if (error)
		goto out;

	for (; agno <= end_agno; agno++) {
		struct xfs_rmap_irec	ri_low = { };
		struct xfs_rmap_irec	ri_high;
		struct xfs_agf		*agf;
		struct xfs_perag	*pag;
		xfs_agblock_t		range_agend;

		pag = xfs_perag_get(mp, agno);
		error = xfs_alloc_read_agf(pag, tp, 0, &agf_bp);
		if (error) {
			xfs_perag_put(pag);
			break;
		}

		cur = xfs_rmapbt_init_cursor(mp, tp, agf_bp, pag);

		/*
		 * Set the rmap range from ri_low to ri_high, which represents
		 * a [start, end] where we looking for the files or metadata.
		 */
		memset(&ri_high, 0xFF, sizeof(ri_high));
		ri_low.rm_startblock = XFS_FSB_TO_AGBNO(mp, fsbno);
		if (agno == end_agno)
			ri_high.rm_startblock = XFS_FSB_TO_AGBNO(mp, end_fsbno);

		agf = agf_bp->b_addr;
		range_agend = min(be32_to_cpu(agf->agf_length) - 1,
				ri_high.rm_startblock);
		notify.startblock = ri_low.rm_startblock;
		notify.blockcount = range_agend + 1 - ri_low.rm_startblock;

		error = xfs_rmap_query_range(cur, &ri_low, &ri_high,
				xfs_dax_failure_fn, &notify);
		xfs_btree_del_cursor(cur, error);
		xfs_trans_brelse(tp, agf_bp);
		xfs_perag_put(pag);
		if (error)
			break;

		fsbno = XFS_AGB_TO_FSB(mp, agno + 1, 0);
	}

	xfs_trans_cancel(tp);

	/*
	 * Shutdown fs from a force umount in pre-remove case which won't fail,
	 * so errors can be ignored.  Otherwise, shutdown the filesystem with
	 * CORRUPT flag if error occured or notify.want_shutdown was set during
	 * RMAP querying.
	 */
	if (mf_flags & MF_MEM_PRE_REMOVE)
		xfs_force_shutdown(mp, SHUTDOWN_FORCE_UMOUNT);
	else if (error || notify.want_shutdown) {
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_ONDISK);
		if (!error)
			error = -EFSCORRUPTED;
	}

out:
	/* Thaw the fs if it has been frozen before. */
	if (mf_flags & MF_MEM_PRE_REMOVE)
		xfs_dax_notify_failure_thaw(mp, kernel_frozen);

	return error;
}

static int
xfs_dax_notify_failure(
	struct dax_device	*dax_dev,
	u64			offset,
	u64			len,
	int			mf_flags)
{
	struct xfs_mount	*mp = dax_holder(dax_dev);
	u64			ddev_start;
	u64			ddev_end;

	if (!(mp->m_super->s_flags & SB_BORN)) {
		xfs_warn(mp, "filesystem is not ready for notify_failure()!");
		return -EIO;
	}

	if (mp->m_rtdev_targp && mp->m_rtdev_targp->bt_daxdev == dax_dev) {
		xfs_debug(mp,
			 "notify_failure() not supported on realtime device!");
		return -EOPNOTSUPP;
	}

	if (mp->m_logdev_targp && mp->m_logdev_targp->bt_daxdev == dax_dev &&
	    mp->m_logdev_targp != mp->m_ddev_targp) {
		/*
		 * In the pre-remove case the failure notification is attempting
		 * to trigger a force unmount.  The expectation is that the
		 * device is still present, but its removal is in progress and
		 * can not be cancelled, proceed with accessing the log device.
		 */
		if (mf_flags & MF_MEM_PRE_REMOVE)
			return 0;
		xfs_err(mp, "ondisk log corrupt, shutting down fs!");
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_ONDISK);
		return -EFSCORRUPTED;
	}

	if (!xfs_has_rmapbt(mp)) {
		xfs_debug(mp, "notify_failure() needs rmapbt enabled!");
		return -EOPNOTSUPP;
	}

	ddev_start = mp->m_ddev_targp->bt_dax_part_off;
	ddev_end = ddev_start + bdev_nr_bytes(mp->m_ddev_targp->bt_bdev) - 1;

	/* Notify failure on the whole device. */
	if (offset == 0 && len == U64_MAX) {
		offset = ddev_start;
		len = bdev_nr_bytes(mp->m_ddev_targp->bt_bdev);
	}

	/* Ignore the range out of filesystem area */
	if (offset + len - 1 < ddev_start)
		return -ENXIO;
	if (offset > ddev_end)
		return -ENXIO;

	/* Calculate the real range when it touches the boundary */
	if (offset > ddev_start)
		offset -= ddev_start;
	else {
		len -= ddev_start - offset;
		offset = 0;
	}
	if (offset + len - 1 > ddev_end)
		len = ddev_end - offset + 1;

	return xfs_dax_notify_ddev_failure(mp, BTOBB(offset), BTOBB(len),
			mf_flags);
}

const struct dax_holder_operations xfs_dax_holder_operations = {
	.notify_failure		= xfs_dax_notify_failure,
};
