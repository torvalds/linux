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
#include "xfs_notify_failure.h"
#include "xfs_rtgroup.h"
#include "xfs_rtrmap_btree.h"

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
xfs_dax_translate_range(
	struct xfs_buftarg	*btp,
	u64			offset,
	u64			len,
	xfs_daddr_t		*daddr,
	uint64_t		*bblen)
{
	u64			dev_start = btp->bt_dax_part_off;
	u64			dev_len = bdev_nr_bytes(btp->bt_bdev);
	u64			dev_end = dev_start + dev_len - 1;

	/* Notify failure on the whole device. */
	if (offset == 0 && len == U64_MAX) {
		offset = dev_start;
		len = dev_len;
	}

	/* Ignore the range out of filesystem area */
	if (offset + len - 1 < dev_start)
		return -ENXIO;
	if (offset > dev_end)
		return -ENXIO;

	/* Calculate the real range when it touches the boundary */
	if (offset > dev_start)
		offset -= dev_start;
	else {
		len -= dev_start - offset;
		offset = 0;
	}
	if (offset + len - 1 > dev_end)
		len = dev_end - offset + 1;

	*daddr = BTOBB(offset);
	*bblen = BTOBB(len);
	return 0;
}

static int
xfs_dax_notify_logdev_failure(
	struct xfs_mount	*mp,
	u64			offset,
	u64			len,
	int			mf_flags)
{
	xfs_daddr_t		daddr;
	uint64_t		bblen;
	int			error;

	/*
	 * Return ENXIO instead of shutting down the filesystem if the failed
	 * region is beyond the end of the log.
	 */
	error = xfs_dax_translate_range(mp->m_logdev_targp,
			offset, len, &daddr, &bblen);
	if (error)
		return error;

	/*
	 * In the pre-remove case the failure notification is attempting to
	 * trigger a force unmount.  The expectation is that the device is
	 * still present, but its removal is in progress and can not be
	 * cancelled, proceed with accessing the log device.
	 */
	if (mf_flags & MF_MEM_PRE_REMOVE)
		return 0;

	xfs_err(mp, "ondisk log corrupt, shutting down fs!");
	xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_ONDISK);
	return -EFSCORRUPTED;
}

static int
xfs_dax_notify_dev_failure(
	struct xfs_mount	*mp,
	u64			offset,
	u64			len,
	int			mf_flags,
	enum xfs_group_type	type)
{
	struct xfs_failure_info	notify = { .mf_flags = mf_flags };
	struct xfs_trans	*tp = NULL;
	struct xfs_btree_cur	*cur = NULL;
	int			error = 0;
	bool			kernel_frozen = false;
	uint32_t		start_gno, end_gno;
	xfs_fsblock_t		start_bno, end_bno;
	xfs_daddr_t		daddr;
	uint64_t		bblen;
	struct xfs_group	*xg = NULL;

	if (!xfs_has_rmapbt(mp)) {
		xfs_debug(mp, "notify_failure() needs rmapbt enabled!");
		return -EOPNOTSUPP;
	}

	error = xfs_dax_translate_range(type == XG_TYPE_RTG ?
			mp->m_rtdev_targp : mp->m_ddev_targp,
			offset, len, &daddr, &bblen);
	if (error)
		return error;

	if (type == XG_TYPE_RTG) {
		start_bno = xfs_daddr_to_rtb(mp, daddr);
		end_bno = xfs_daddr_to_rtb(mp, daddr + bblen - 1);
	} else {
		start_bno = XFS_DADDR_TO_FSB(mp, daddr);
		end_bno = XFS_DADDR_TO_FSB(mp, daddr + bblen - 1);
	}

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

	start_gno = xfs_fsb_to_gno(mp, start_bno, type);
	end_gno = xfs_fsb_to_gno(mp, end_bno, type);
	while ((xg = xfs_group_next_range(mp, xg, start_gno, end_gno, type))) {
		struct xfs_buf		*agf_bp = NULL;
		struct xfs_rtgroup	*rtg = NULL;
		struct xfs_rmap_irec	ri_low = { };
		struct xfs_rmap_irec	ri_high;

		if (type == XG_TYPE_AG) {
			struct xfs_perag	*pag = to_perag(xg);

			error = xfs_alloc_read_agf(pag, tp, 0, &agf_bp);
			if (error) {
				xfs_perag_put(pag);
				break;
			}

			cur = xfs_rmapbt_init_cursor(mp, tp, agf_bp, pag);
		} else {
			rtg = to_rtg(xg);
			xfs_rtgroup_lock(rtg, XFS_RTGLOCK_RMAP);
			cur = xfs_rtrmapbt_init_cursor(tp, rtg);
		}

		/*
		 * Set the rmap range from ri_low to ri_high, which represents
		 * a [start, end] where we looking for the files or metadata.
		 */
		memset(&ri_high, 0xFF, sizeof(ri_high));
		if (xg->xg_gno == start_gno)
			ri_low.rm_startblock =
				xfs_fsb_to_gbno(mp, start_bno, type);
		if (xg->xg_gno == end_gno)
			ri_high.rm_startblock =
				xfs_fsb_to_gbno(mp, end_bno, type);

		notify.startblock = ri_low.rm_startblock;
		notify.blockcount = min(xg->xg_block_count,
					ri_high.rm_startblock + 1) -
					ri_low.rm_startblock;

		error = xfs_rmap_query_range(cur, &ri_low, &ri_high,
				xfs_dax_failure_fn, &notify);
		xfs_btree_del_cursor(cur, error);
		if (agf_bp)
			xfs_trans_brelse(tp, agf_bp);
		if (rtg)
			xfs_rtgroup_unlock(rtg, XFS_RTGLOCK_RMAP);
		if (error) {
			xfs_group_put(xg);
			break;
		}
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

	if (!(mp->m_super->s_flags & SB_BORN)) {
		xfs_warn(mp, "filesystem is not ready for notify_failure()!");
		return -EIO;
	}

	if (mp->m_logdev_targp != mp->m_ddev_targp &&
	    mp->m_logdev_targp->bt_daxdev == dax_dev) {
		return xfs_dax_notify_logdev_failure(mp, offset, len, mf_flags);
	}

	return xfs_dax_notify_dev_failure(mp, offset, len, mf_flags,
		(mp->m_rtdev_targp && mp->m_rtdev_targp->bt_daxdev == dax_dev) ?
				XG_TYPE_RTG : XG_TYPE_AG);
}

const struct dax_holder_operations xfs_dax_holder_operations = {
	.notify_failure		= xfs_dax_notify_failure,
};
