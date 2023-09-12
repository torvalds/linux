// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
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
#include "xfs_inode.h"
#include "xfs_dir2.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_rtalloc.h"
#include "xfs_bmap.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_log.h"
#include "xfs_log_priv.h"
#include "xfs_error.h"
#include "xfs_quota.h"
#include "xfs_fsops.h"
#include "xfs_icache.h"
#include "xfs_sysfs.h"
#include "xfs_rmap_btree.h"
#include "xfs_refcount_btree.h"
#include "xfs_reflink.h"
#include "xfs_extent_busy.h"
#include "xfs_health.h"
#include "xfs_trace.h"
#include "xfs_ag.h"
#include "scrub/stats.h"

static DEFINE_MUTEX(xfs_uuid_table_mutex);
static int xfs_uuid_table_size;
static uuid_t *xfs_uuid_table;

void
xfs_uuid_table_free(void)
{
	if (xfs_uuid_table_size == 0)
		return;
	kmem_free(xfs_uuid_table);
	xfs_uuid_table = NULL;
	xfs_uuid_table_size = 0;
}

/*
 * See if the UUID is unique among mounted XFS filesystems.
 * Mount fails if UUID is nil or a FS with the same UUID is already mounted.
 */
STATIC int
xfs_uuid_mount(
	struct xfs_mount	*mp)
{
	uuid_t			*uuid = &mp->m_sb.sb_uuid;
	int			hole, i;

	/* Publish UUID in struct super_block */
	uuid_copy(&mp->m_super->s_uuid, uuid);

	if (xfs_has_nouuid(mp))
		return 0;

	if (uuid_is_null(uuid)) {
		xfs_warn(mp, "Filesystem has null UUID - can't mount");
		return -EINVAL;
	}

	mutex_lock(&xfs_uuid_table_mutex);
	for (i = 0, hole = -1; i < xfs_uuid_table_size; i++) {
		if (uuid_is_null(&xfs_uuid_table[i])) {
			hole = i;
			continue;
		}
		if (uuid_equal(uuid, &xfs_uuid_table[i]))
			goto out_duplicate;
	}

	if (hole < 0) {
		xfs_uuid_table = krealloc(xfs_uuid_table,
			(xfs_uuid_table_size + 1) * sizeof(*xfs_uuid_table),
			GFP_KERNEL | __GFP_NOFAIL);
		hole = xfs_uuid_table_size++;
	}
	xfs_uuid_table[hole] = *uuid;
	mutex_unlock(&xfs_uuid_table_mutex);

	return 0;

 out_duplicate:
	mutex_unlock(&xfs_uuid_table_mutex);
	xfs_warn(mp, "Filesystem has duplicate UUID %pU - can't mount", uuid);
	return -EINVAL;
}

STATIC void
xfs_uuid_unmount(
	struct xfs_mount	*mp)
{
	uuid_t			*uuid = &mp->m_sb.sb_uuid;
	int			i;

	if (xfs_has_nouuid(mp))
		return;

	mutex_lock(&xfs_uuid_table_mutex);
	for (i = 0; i < xfs_uuid_table_size; i++) {
		if (uuid_is_null(&xfs_uuid_table[i]))
			continue;
		if (!uuid_equal(uuid, &xfs_uuid_table[i]))
			continue;
		memset(&xfs_uuid_table[i], 0, sizeof(uuid_t));
		break;
	}
	ASSERT(i < xfs_uuid_table_size);
	mutex_unlock(&xfs_uuid_table_mutex);
}

/*
 * Check size of device based on the (data/realtime) block count.
 * Note: this check is used by the growfs code as well as mount.
 */
int
xfs_sb_validate_fsb_count(
	xfs_sb_t	*sbp,
	uint64_t	nblocks)
{
	ASSERT(PAGE_SHIFT >= sbp->sb_blocklog);
	ASSERT(sbp->sb_blocklog >= BBSHIFT);

	/* Limited by ULONG_MAX of page cache index */
	if (nblocks >> (PAGE_SHIFT - sbp->sb_blocklog) > ULONG_MAX)
		return -EFBIG;
	return 0;
}

/*
 * xfs_readsb
 *
 * Does the initial read of the superblock.
 */
int
xfs_readsb(
	struct xfs_mount *mp,
	int		flags)
{
	unsigned int	sector_size;
	struct xfs_buf	*bp;
	struct xfs_sb	*sbp = &mp->m_sb;
	int		error;
	int		loud = !(flags & XFS_MFSI_QUIET);
	const struct xfs_buf_ops *buf_ops;

	ASSERT(mp->m_sb_bp == NULL);
	ASSERT(mp->m_ddev_targp != NULL);

	/*
	 * For the initial read, we must guess at the sector
	 * size based on the block device.  It's enough to
	 * get the sb_sectsize out of the superblock and
	 * then reread with the proper length.
	 * We don't verify it yet, because it may not be complete.
	 */
	sector_size = xfs_getsize_buftarg(mp->m_ddev_targp);
	buf_ops = NULL;

	/*
	 * Allocate a (locked) buffer to hold the superblock. This will be kept
	 * around at all times to optimize access to the superblock. Therefore,
	 * set XBF_NO_IOACCT to make sure it doesn't hold the buftarg count
	 * elevated.
	 */
reread:
	error = xfs_buf_read_uncached(mp->m_ddev_targp, XFS_SB_DADDR,
				      BTOBB(sector_size), XBF_NO_IOACCT, &bp,
				      buf_ops);
	if (error) {
		if (loud)
			xfs_warn(mp, "SB validate failed with error %d.", error);
		/* bad CRC means corrupted metadata */
		if (error == -EFSBADCRC)
			error = -EFSCORRUPTED;
		return error;
	}

	/*
	 * Initialize the mount structure from the superblock.
	 */
	xfs_sb_from_disk(sbp, bp->b_addr);

	/*
	 * If we haven't validated the superblock, do so now before we try
	 * to check the sector size and reread the superblock appropriately.
	 */
	if (sbp->sb_magicnum != XFS_SB_MAGIC) {
		if (loud)
			xfs_warn(mp, "Invalid superblock magic number");
		error = -EINVAL;
		goto release_buf;
	}

	/*
	 * We must be able to do sector-sized and sector-aligned IO.
	 */
	if (sector_size > sbp->sb_sectsize) {
		if (loud)
			xfs_warn(mp, "device supports %u byte sectors (not %u)",
				sector_size, sbp->sb_sectsize);
		error = -ENOSYS;
		goto release_buf;
	}

	if (buf_ops == NULL) {
		/*
		 * Re-read the superblock so the buffer is correctly sized,
		 * and properly verified.
		 */
		xfs_buf_relse(bp);
		sector_size = sbp->sb_sectsize;
		buf_ops = loud ? &xfs_sb_buf_ops : &xfs_sb_quiet_buf_ops;
		goto reread;
	}

	mp->m_features |= xfs_sb_version_to_features(sbp);
	xfs_reinit_percpu_counters(mp);

	/* no need to be quiet anymore, so reset the buf ops */
	bp->b_ops = &xfs_sb_buf_ops;

	mp->m_sb_bp = bp;
	xfs_buf_unlock(bp);
	return 0;

release_buf:
	xfs_buf_relse(bp);
	return error;
}

/*
 * If the sunit/swidth change would move the precomputed root inode value, we
 * must reject the ondisk change because repair will stumble over that.
 * However, we allow the mount to proceed because we never rejected this
 * combination before.  Returns true to update the sb, false otherwise.
 */
static inline int
xfs_check_new_dalign(
	struct xfs_mount	*mp,
	int			new_dalign,
	bool			*update_sb)
{
	struct xfs_sb		*sbp = &mp->m_sb;
	xfs_ino_t		calc_ino;

	calc_ino = xfs_ialloc_calc_rootino(mp, new_dalign);
	trace_xfs_check_new_dalign(mp, new_dalign, calc_ino);

	if (sbp->sb_rootino == calc_ino) {
		*update_sb = true;
		return 0;
	}

	xfs_warn(mp,
"Cannot change stripe alignment; would require moving root inode.");

	/*
	 * XXX: Next time we add a new incompat feature, this should start
	 * returning -EINVAL to fail the mount.  Until then, spit out a warning
	 * that we're ignoring the administrator's instructions.
	 */
	xfs_warn(mp, "Skipping superblock stripe alignment update.");
	*update_sb = false;
	return 0;
}

/*
 * If we were provided with new sunit/swidth values as mount options, make sure
 * that they pass basic alignment and superblock feature checks, and convert
 * them into the same units (FSB) that everything else expects.  This step
 * /must/ be done before computing the inode geometry.
 */
STATIC int
xfs_validate_new_dalign(
	struct xfs_mount	*mp)
{
	if (mp->m_dalign == 0)
		return 0;

	/*
	 * If stripe unit and stripe width are not multiples
	 * of the fs blocksize turn off alignment.
	 */
	if ((BBTOB(mp->m_dalign) & mp->m_blockmask) ||
	    (BBTOB(mp->m_swidth) & mp->m_blockmask)) {
		xfs_warn(mp,
	"alignment check failed: sunit/swidth vs. blocksize(%d)",
			mp->m_sb.sb_blocksize);
		return -EINVAL;
	}

	/*
	 * Convert the stripe unit and width to FSBs.
	 */
	mp->m_dalign = XFS_BB_TO_FSBT(mp, mp->m_dalign);
	if (mp->m_dalign && (mp->m_sb.sb_agblocks % mp->m_dalign)) {
		xfs_warn(mp,
	"alignment check failed: sunit/swidth vs. agsize(%d)",
			mp->m_sb.sb_agblocks);
		return -EINVAL;
	}

	if (!mp->m_dalign) {
		xfs_warn(mp,
	"alignment check failed: sunit(%d) less than bsize(%d)",
			mp->m_dalign, mp->m_sb.sb_blocksize);
		return -EINVAL;
	}

	mp->m_swidth = XFS_BB_TO_FSBT(mp, mp->m_swidth);

	if (!xfs_has_dalign(mp)) {
		xfs_warn(mp,
"cannot change alignment: superblock does not support data alignment");
		return -EINVAL;
	}

	return 0;
}

/* Update alignment values based on mount options and sb values. */
STATIC int
xfs_update_alignment(
	struct xfs_mount	*mp)
{
	struct xfs_sb		*sbp = &mp->m_sb;

	if (mp->m_dalign) {
		bool		update_sb;
		int		error;

		if (sbp->sb_unit == mp->m_dalign &&
		    sbp->sb_width == mp->m_swidth)
			return 0;

		error = xfs_check_new_dalign(mp, mp->m_dalign, &update_sb);
		if (error || !update_sb)
			return error;

		sbp->sb_unit = mp->m_dalign;
		sbp->sb_width = mp->m_swidth;
		mp->m_update_sb = true;
	} else if (!xfs_has_noalign(mp) && xfs_has_dalign(mp)) {
		mp->m_dalign = sbp->sb_unit;
		mp->m_swidth = sbp->sb_width;
	}

	return 0;
}

/*
 * precalculate the low space thresholds for dynamic speculative preallocation.
 */
void
xfs_set_low_space_thresholds(
	struct xfs_mount	*mp)
{
	uint64_t		dblocks = mp->m_sb.sb_dblocks;
	uint64_t		rtexts = mp->m_sb.sb_rextents;
	int			i;

	do_div(dblocks, 100);
	do_div(rtexts, 100);

	for (i = 0; i < XFS_LOWSP_MAX; i++) {
		mp->m_low_space[i] = dblocks * (i + 1);
		mp->m_low_rtexts[i] = rtexts * (i + 1);
	}
}

/*
 * Check that the data (and log if separate) is an ok size.
 */
STATIC int
xfs_check_sizes(
	struct xfs_mount *mp)
{
	struct xfs_buf	*bp;
	xfs_daddr_t	d;
	int		error;

	d = (xfs_daddr_t)XFS_FSB_TO_BB(mp, mp->m_sb.sb_dblocks);
	if (XFS_BB_TO_FSB(mp, d) != mp->m_sb.sb_dblocks) {
		xfs_warn(mp, "filesystem size mismatch detected");
		return -EFBIG;
	}
	error = xfs_buf_read_uncached(mp->m_ddev_targp,
					d - XFS_FSS_TO_BB(mp, 1),
					XFS_FSS_TO_BB(mp, 1), 0, &bp, NULL);
	if (error) {
		xfs_warn(mp, "last sector read failed");
		return error;
	}
	xfs_buf_relse(bp);

	if (mp->m_logdev_targp == mp->m_ddev_targp)
		return 0;

	d = (xfs_daddr_t)XFS_FSB_TO_BB(mp, mp->m_sb.sb_logblocks);
	if (XFS_BB_TO_FSB(mp, d) != mp->m_sb.sb_logblocks) {
		xfs_warn(mp, "log size mismatch detected");
		return -EFBIG;
	}
	error = xfs_buf_read_uncached(mp->m_logdev_targp,
					d - XFS_FSB_TO_BB(mp, 1),
					XFS_FSB_TO_BB(mp, 1), 0, &bp, NULL);
	if (error) {
		xfs_warn(mp, "log device read failed");
		return error;
	}
	xfs_buf_relse(bp);
	return 0;
}

/*
 * Clear the quotaflags in memory and in the superblock.
 */
int
xfs_mount_reset_sbqflags(
	struct xfs_mount	*mp)
{
	mp->m_qflags = 0;

	/* It is OK to look at sb_qflags in the mount path without m_sb_lock. */
	if (mp->m_sb.sb_qflags == 0)
		return 0;
	spin_lock(&mp->m_sb_lock);
	mp->m_sb.sb_qflags = 0;
	spin_unlock(&mp->m_sb_lock);

	if (!xfs_fs_writable(mp, SB_FREEZE_WRITE))
		return 0;

	return xfs_sync_sb(mp, false);
}

uint64_t
xfs_default_resblks(xfs_mount_t *mp)
{
	uint64_t resblks;

	/*
	 * We default to 5% or 8192 fsbs of space reserved, whichever is
	 * smaller.  This is intended to cover concurrent allocation
	 * transactions when we initially hit enospc. These each require a 4
	 * block reservation. Hence by default we cover roughly 2000 concurrent
	 * allocation reservations.
	 */
	resblks = mp->m_sb.sb_dblocks;
	do_div(resblks, 20);
	resblks = min_t(uint64_t, resblks, 8192);
	return resblks;
}

/* Ensure the summary counts are correct. */
STATIC int
xfs_check_summary_counts(
	struct xfs_mount	*mp)
{
	int			error = 0;

	/*
	 * The AG0 superblock verifier rejects in-progress filesystems,
	 * so we should never see the flag set this far into mounting.
	 */
	if (mp->m_sb.sb_inprogress) {
		xfs_err(mp, "sb_inprogress set after log recovery??");
		WARN_ON(1);
		return -EFSCORRUPTED;
	}

	/*
	 * Now the log is mounted, we know if it was an unclean shutdown or
	 * not. If it was, with the first phase of recovery has completed, we
	 * have consistent AG blocks on disk. We have not recovered EFIs yet,
	 * but they are recovered transactionally in the second recovery phase
	 * later.
	 *
	 * If the log was clean when we mounted, we can check the summary
	 * counters.  If any of them are obviously incorrect, we can recompute
	 * them from the AGF headers in the next step.
	 */
	if (xfs_is_clean(mp) &&
	    (mp->m_sb.sb_fdblocks > mp->m_sb.sb_dblocks ||
	     !xfs_verify_icount(mp, mp->m_sb.sb_icount) ||
	     mp->m_sb.sb_ifree > mp->m_sb.sb_icount))
		xfs_fs_mark_sick(mp, XFS_SICK_FS_COUNTERS);

	/*
	 * We can safely re-initialise incore superblock counters from the
	 * per-ag data. These may not be correct if the filesystem was not
	 * cleanly unmounted, so we waited for recovery to finish before doing
	 * this.
	 *
	 * If the filesystem was cleanly unmounted or the previous check did
	 * not flag anything weird, then we can trust the values in the
	 * superblock to be correct and we don't need to do anything here.
	 * Otherwise, recalculate the summary counters.
	 */
	if ((xfs_has_lazysbcount(mp) && !xfs_is_clean(mp)) ||
	    xfs_fs_has_sickness(mp, XFS_SICK_FS_COUNTERS)) {
		error = xfs_initialize_perag_data(mp, mp->m_sb.sb_agcount);
		if (error)
			return error;
	}

	/*
	 * Older kernels misused sb_frextents to reflect both incore
	 * reservations made by running transactions and the actual count of
	 * free rt extents in the ondisk metadata.  Transactions committed
	 * during runtime can therefore contain a superblock update that
	 * undercounts the number of free rt extents tracked in the rt bitmap.
	 * A clean unmount record will have the correct frextents value since
	 * there can be no other transactions running at that point.
	 *
	 * If we're mounting the rt volume after recovering the log, recompute
	 * frextents from the rtbitmap file to fix the inconsistency.
	 */
	if (xfs_has_realtime(mp) && !xfs_is_clean(mp)) {
		error = xfs_rtalloc_reinit_frextents(mp);
		if (error)
			return error;
	}

	return 0;
}

static void
xfs_unmount_check(
	struct xfs_mount	*mp)
{
	if (xfs_is_shutdown(mp))
		return;

	if (percpu_counter_sum(&mp->m_ifree) >
			percpu_counter_sum(&mp->m_icount)) {
		xfs_alert(mp, "ifree/icount mismatch at unmount");
		xfs_fs_mark_sick(mp, XFS_SICK_FS_COUNTERS);
	}
}

/*
 * Flush and reclaim dirty inodes in preparation for unmount. Inodes and
 * internal inode structures can be sitting in the CIL and AIL at this point,
 * so we need to unpin them, write them back and/or reclaim them before unmount
 * can proceed.  In other words, callers are required to have inactivated all
 * inodes.
 *
 * An inode cluster that has been freed can have its buffer still pinned in
 * memory because the transaction is still sitting in a iclog. The stale inodes
 * on that buffer will be pinned to the buffer until the transaction hits the
 * disk and the callbacks run. Pushing the AIL will skip the stale inodes and
 * may never see the pinned buffer, so nothing will push out the iclog and
 * unpin the buffer.
 *
 * Hence we need to force the log to unpin everything first. However, log
 * forces don't wait for the discards they issue to complete, so we have to
 * explicitly wait for them to complete here as well.
 *
 * Then we can tell the world we are unmounting so that error handling knows
 * that the filesystem is going away and we should error out anything that we
 * have been retrying in the background.  This will prevent never-ending
 * retries in AIL pushing from hanging the unmount.
 *
 * Finally, we can push the AIL to clean all the remaining dirty objects, then
 * reclaim the remaining inodes that are still in memory at this point in time.
 */
static void
xfs_unmount_flush_inodes(
	struct xfs_mount	*mp)
{
	xfs_log_force(mp, XFS_LOG_SYNC);
	xfs_extent_busy_wait_all(mp);
	flush_workqueue(xfs_discard_wq);

	set_bit(XFS_OPSTATE_UNMOUNTING, &mp->m_opstate);

	xfs_ail_push_all_sync(mp->m_ail);
	xfs_inodegc_stop(mp);
	cancel_delayed_work_sync(&mp->m_reclaim_work);
	xfs_reclaim_inodes(mp);
	xfs_health_unmount(mp);
}

static void
xfs_mount_setup_inode_geom(
	struct xfs_mount	*mp)
{
	struct xfs_ino_geometry *igeo = M_IGEO(mp);

	igeo->attr_fork_offset = xfs_bmap_compute_attr_offset(mp);
	ASSERT(igeo->attr_fork_offset < XFS_LITINO(mp));

	xfs_ialloc_setup_geometry(mp);
}

/* Compute maximum possible height for per-AG btree types for this fs. */
static inline void
xfs_agbtree_compute_maxlevels(
	struct xfs_mount	*mp)
{
	unsigned int		levels;

	levels = max(mp->m_alloc_maxlevels, M_IGEO(mp)->inobt_maxlevels);
	levels = max(levels, mp->m_rmap_maxlevels);
	mp->m_agbtree_maxlevels = max(levels, mp->m_refc_maxlevels);
}

/*
 * This function does the following on an initial mount of a file system:
 *	- reads the superblock from disk and init the mount struct
 *	- if we're a 32-bit kernel, do a size check on the superblock
 *		so we don't mount terabyte filesystems
 *	- init mount struct realtime fields
 *	- allocate inode hash table for fs
 *	- init directory manager
 *	- perform recovery and init the log manager
 */
int
xfs_mountfs(
	struct xfs_mount	*mp)
{
	struct xfs_sb		*sbp = &(mp->m_sb);
	struct xfs_inode	*rip;
	struct xfs_ino_geometry	*igeo = M_IGEO(mp);
	uint64_t		resblks;
	uint			quotamount = 0;
	uint			quotaflags = 0;
	int			error = 0;

	xfs_sb_mount_common(mp, sbp);

	/*
	 * Check for a mismatched features2 values.  Older kernels read & wrote
	 * into the wrong sb offset for sb_features2 on some platforms due to
	 * xfs_sb_t not being 64bit size aligned when sb_features2 was added,
	 * which made older superblock reading/writing routines swap it as a
	 * 64-bit value.
	 *
	 * For backwards compatibility, we make both slots equal.
	 *
	 * If we detect a mismatched field, we OR the set bits into the existing
	 * features2 field in case it has already been modified; we don't want
	 * to lose any features.  We then update the bad location with the ORed
	 * value so that older kernels will see any features2 flags. The
	 * superblock writeback code ensures the new sb_features2 is copied to
	 * sb_bad_features2 before it is logged or written to disk.
	 */
	if (xfs_sb_has_mismatched_features2(sbp)) {
		xfs_warn(mp, "correcting sb_features alignment problem");
		sbp->sb_features2 |= sbp->sb_bad_features2;
		mp->m_update_sb = true;
	}


	/* always use v2 inodes by default now */
	if (!(mp->m_sb.sb_versionnum & XFS_SB_VERSION_NLINKBIT)) {
		mp->m_sb.sb_versionnum |= XFS_SB_VERSION_NLINKBIT;
		mp->m_features |= XFS_FEAT_NLINK;
		mp->m_update_sb = true;
	}

	/*
	 * If we were given new sunit/swidth options, do some basic validation
	 * checks and convert the incore dalign and swidth values to the
	 * same units (FSB) that everything else uses.  This /must/ happen
	 * before computing the inode geometry.
	 */
	error = xfs_validate_new_dalign(mp);
	if (error)
		goto out;

	xfs_alloc_compute_maxlevels(mp);
	xfs_bmap_compute_maxlevels(mp, XFS_DATA_FORK);
	xfs_bmap_compute_maxlevels(mp, XFS_ATTR_FORK);
	xfs_mount_setup_inode_geom(mp);
	xfs_rmapbt_compute_maxlevels(mp);
	xfs_refcountbt_compute_maxlevels(mp);

	xfs_agbtree_compute_maxlevels(mp);

	/*
	 * Check if sb_agblocks is aligned at stripe boundary.  If sb_agblocks
	 * is NOT aligned turn off m_dalign since allocator alignment is within
	 * an ag, therefore ag has to be aligned at stripe boundary.  Note that
	 * we must compute the free space and rmap btree geometry before doing
	 * this.
	 */
	error = xfs_update_alignment(mp);
	if (error)
		goto out;

	/* enable fail_at_unmount as default */
	mp->m_fail_unmount = true;

	error = xfs_sysfs_init(&mp->m_kobj, &xfs_mp_ktype,
			       NULL, mp->m_super->s_id);
	if (error)
		goto out;

	error = xfs_sysfs_init(&mp->m_stats.xs_kobj, &xfs_stats_ktype,
			       &mp->m_kobj, "stats");
	if (error)
		goto out_remove_sysfs;

	xchk_stats_register(mp->m_scrub_stats, mp->m_debugfs);

	error = xfs_error_sysfs_init(mp);
	if (error)
		goto out_remove_scrub_stats;

	error = xfs_errortag_init(mp);
	if (error)
		goto out_remove_error_sysfs;

	error = xfs_uuid_mount(mp);
	if (error)
		goto out_remove_errortag;

	/*
	 * Update the preferred write size based on the information from the
	 * on-disk superblock.
	 */
	mp->m_allocsize_log =
		max_t(uint32_t, sbp->sb_blocklog, mp->m_allocsize_log);
	mp->m_allocsize_blocks = 1U << (mp->m_allocsize_log - sbp->sb_blocklog);

	/* set the low space thresholds for dynamic preallocation */
	xfs_set_low_space_thresholds(mp);

	/*
	 * If enabled, sparse inode chunk alignment is expected to match the
	 * cluster size. Full inode chunk alignment must match the chunk size,
	 * but that is checked on sb read verification...
	 */
	if (xfs_has_sparseinodes(mp) &&
	    mp->m_sb.sb_spino_align !=
			XFS_B_TO_FSBT(mp, igeo->inode_cluster_size_raw)) {
		xfs_warn(mp,
	"Sparse inode block alignment (%u) must match cluster size (%llu).",
			 mp->m_sb.sb_spino_align,
			 XFS_B_TO_FSBT(mp, igeo->inode_cluster_size_raw));
		error = -EINVAL;
		goto out_remove_uuid;
	}

	/*
	 * Check that the data (and log if separate) is an ok size.
	 */
	error = xfs_check_sizes(mp);
	if (error)
		goto out_remove_uuid;

	/*
	 * Initialize realtime fields in the mount structure
	 */
	error = xfs_rtmount_init(mp);
	if (error) {
		xfs_warn(mp, "RT mount failed");
		goto out_remove_uuid;
	}

	/*
	 *  Copies the low order bits of the timestamp and the randomly
	 *  set "sequence" number out of a UUID.
	 */
	mp->m_fixedfsid[0] =
		(get_unaligned_be16(&sbp->sb_uuid.b[8]) << 16) |
		 get_unaligned_be16(&sbp->sb_uuid.b[4]);
	mp->m_fixedfsid[1] = get_unaligned_be32(&sbp->sb_uuid.b[0]);

	error = xfs_da_mount(mp);
	if (error) {
		xfs_warn(mp, "Failed dir/attr init: %d", error);
		goto out_remove_uuid;
	}

	/*
	 * Initialize the precomputed transaction reservations values.
	 */
	xfs_trans_init(mp);

	/*
	 * Allocate and initialize the per-ag data.
	 */
	error = xfs_initialize_perag(mp, sbp->sb_agcount, mp->m_sb.sb_dblocks,
			&mp->m_maxagi);
	if (error) {
		xfs_warn(mp, "Failed per-ag init: %d", error);
		goto out_free_dir;
	}

	if (XFS_IS_CORRUPT(mp, !sbp->sb_logblocks)) {
		xfs_warn(mp, "no log defined");
		error = -EFSCORRUPTED;
		goto out_free_perag;
	}

	error = xfs_inodegc_register_shrinker(mp);
	if (error)
		goto out_fail_wait;

	/*
	 * Log's mount-time initialization. The first part of recovery can place
	 * some items on the AIL, to be handled when recovery is finished or
	 * cancelled.
	 */
	error = xfs_log_mount(mp, mp->m_logdev_targp,
			      XFS_FSB_TO_DADDR(mp, sbp->sb_logstart),
			      XFS_FSB_TO_BB(mp, sbp->sb_logblocks));
	if (error) {
		xfs_warn(mp, "log mount failed");
		goto out_inodegc_shrinker;
	}

	/* Enable background inode inactivation workers. */
	xfs_inodegc_start(mp);
	xfs_blockgc_start(mp);

	/*
	 * Now that we've recovered any pending superblock feature bit
	 * additions, we can finish setting up the attr2 behaviour for the
	 * mount. The noattr2 option overrides the superblock flag, so only
	 * check the superblock feature flag if the mount option is not set.
	 */
	if (xfs_has_noattr2(mp)) {
		mp->m_features &= ~XFS_FEAT_ATTR2;
	} else if (!xfs_has_attr2(mp) &&
		   (mp->m_sb.sb_features2 & XFS_SB_VERSION2_ATTR2BIT)) {
		mp->m_features |= XFS_FEAT_ATTR2;
	}

	/*
	 * Get and sanity-check the root inode.
	 * Save the pointer to it in the mount structure.
	 */
	error = xfs_iget(mp, NULL, sbp->sb_rootino, XFS_IGET_UNTRUSTED,
			 XFS_ILOCK_EXCL, &rip);
	if (error) {
		xfs_warn(mp,
			"Failed to read root inode 0x%llx, error %d",
			sbp->sb_rootino, -error);
		goto out_log_dealloc;
	}

	ASSERT(rip != NULL);

	if (XFS_IS_CORRUPT(mp, !S_ISDIR(VFS_I(rip)->i_mode))) {
		xfs_warn(mp, "corrupted root inode %llu: not a directory",
			(unsigned long long)rip->i_ino);
		xfs_iunlock(rip, XFS_ILOCK_EXCL);
		error = -EFSCORRUPTED;
		goto out_rele_rip;
	}
	mp->m_rootip = rip;	/* save it */

	xfs_iunlock(rip, XFS_ILOCK_EXCL);

	/*
	 * Initialize realtime inode pointers in the mount structure
	 */
	error = xfs_rtmount_inodes(mp);
	if (error) {
		/*
		 * Free up the root inode.
		 */
		xfs_warn(mp, "failed to read RT inodes");
		goto out_rele_rip;
	}

	/* Make sure the summary counts are ok. */
	error = xfs_check_summary_counts(mp);
	if (error)
		goto out_rtunmount;

	/*
	 * If this is a read-only mount defer the superblock updates until
	 * the next remount into writeable mode.  Otherwise we would never
	 * perform the update e.g. for the root filesystem.
	 */
	if (mp->m_update_sb && !xfs_is_readonly(mp)) {
		error = xfs_sync_sb(mp, false);
		if (error) {
			xfs_warn(mp, "failed to write sb changes");
			goto out_rtunmount;
		}
	}

	/*
	 * Initialise the XFS quota management subsystem for this mount
	 */
	if (XFS_IS_QUOTA_ON(mp)) {
		error = xfs_qm_newmount(mp, &quotamount, &quotaflags);
		if (error)
			goto out_rtunmount;
	} else {
		/*
		 * If a file system had quotas running earlier, but decided to
		 * mount without -o uquota/pquota/gquota options, revoke the
		 * quotachecked license.
		 */
		if (mp->m_sb.sb_qflags & XFS_ALL_QUOTA_ACCT) {
			xfs_notice(mp, "resetting quota flags");
			error = xfs_mount_reset_sbqflags(mp);
			if (error)
				goto out_rtunmount;
		}
	}

	/*
	 * Finish recovering the file system.  This part needed to be delayed
	 * until after the root and real-time bitmap inodes were consistently
	 * read in.  Temporarily create per-AG space reservations for metadata
	 * btree shape changes because space freeing transactions (for inode
	 * inactivation) require the per-AG reservation in lieu of reserving
	 * blocks.
	 */
	error = xfs_fs_reserve_ag_blocks(mp);
	if (error && error == -ENOSPC)
		xfs_warn(mp,
	"ENOSPC reserving per-AG metadata pool, log recovery may fail.");
	error = xfs_log_mount_finish(mp);
	xfs_fs_unreserve_ag_blocks(mp);
	if (error) {
		xfs_warn(mp, "log mount finish failed");
		goto out_rtunmount;
	}

	/*
	 * Now the log is fully replayed, we can transition to full read-only
	 * mode for read-only mounts. This will sync all the metadata and clean
	 * the log so that the recovery we just performed does not have to be
	 * replayed again on the next mount.
	 *
	 * We use the same quiesce mechanism as the rw->ro remount, as they are
	 * semantically identical operations.
	 */
	if (xfs_is_readonly(mp) && !xfs_has_norecovery(mp))
		xfs_log_clean(mp);

	/*
	 * Complete the quota initialisation, post-log-replay component.
	 */
	if (quotamount) {
		ASSERT(mp->m_qflags == 0);
		mp->m_qflags = quotaflags;

		xfs_qm_mount_quotas(mp);
	}

	/*
	 * Now we are mounted, reserve a small amount of unused space for
	 * privileged transactions. This is needed so that transaction
	 * space required for critical operations can dip into this pool
	 * when at ENOSPC. This is needed for operations like create with
	 * attr, unwritten extent conversion at ENOSPC, etc. Data allocations
	 * are not allowed to use this reserved space.
	 *
	 * This may drive us straight to ENOSPC on mount, but that implies
	 * we were already there on the last unmount. Warn if this occurs.
	 */
	if (!xfs_is_readonly(mp)) {
		resblks = xfs_default_resblks(mp);
		error = xfs_reserve_blocks(mp, &resblks, NULL);
		if (error)
			xfs_warn(mp,
	"Unable to allocate reserve blocks. Continuing without reserve pool.");

		/* Reserve AG blocks for future btree expansion. */
		error = xfs_fs_reserve_ag_blocks(mp);
		if (error && error != -ENOSPC)
			goto out_agresv;
	}

	return 0;

 out_agresv:
	xfs_fs_unreserve_ag_blocks(mp);
	xfs_qm_unmount_quotas(mp);
 out_rtunmount:
	xfs_rtunmount_inodes(mp);
 out_rele_rip:
	xfs_irele(rip);
	/* Clean out dquots that might be in memory after quotacheck. */
	xfs_qm_unmount(mp);

	/*
	 * Inactivate all inodes that might still be in memory after a log
	 * intent recovery failure so that reclaim can free them.  Metadata
	 * inodes and the root directory shouldn't need inactivation, but the
	 * mount failed for some reason, so pull down all the state and flee.
	 */
	xfs_inodegc_flush(mp);

	/*
	 * Flush all inode reclamation work and flush the log.
	 * We have to do this /after/ rtunmount and qm_unmount because those
	 * two will have scheduled delayed reclaim for the rt/quota inodes.
	 *
	 * This is slightly different from the unmountfs call sequence
	 * because we could be tearing down a partially set up mount.  In
	 * particular, if log_mount_finish fails we bail out without calling
	 * qm_unmount_quotas and therefore rely on qm_unmount to release the
	 * quota inodes.
	 */
	xfs_unmount_flush_inodes(mp);
 out_log_dealloc:
	xfs_log_mount_cancel(mp);
 out_inodegc_shrinker:
	unregister_shrinker(&mp->m_inodegc_shrinker);
 out_fail_wait:
	if (mp->m_logdev_targp && mp->m_logdev_targp != mp->m_ddev_targp)
		xfs_buftarg_drain(mp->m_logdev_targp);
	xfs_buftarg_drain(mp->m_ddev_targp);
 out_free_perag:
	xfs_free_perag(mp);
 out_free_dir:
	xfs_da_unmount(mp);
 out_remove_uuid:
	xfs_uuid_unmount(mp);
 out_remove_errortag:
	xfs_errortag_del(mp);
 out_remove_error_sysfs:
	xfs_error_sysfs_del(mp);
 out_remove_scrub_stats:
	xchk_stats_unregister(mp->m_scrub_stats);
	xfs_sysfs_del(&mp->m_stats.xs_kobj);
 out_remove_sysfs:
	xfs_sysfs_del(&mp->m_kobj);
 out:
	return error;
}

/*
 * This flushes out the inodes,dquots and the superblock, unmounts the
 * log and makes sure that incore structures are freed.
 */
void
xfs_unmountfs(
	struct xfs_mount	*mp)
{
	uint64_t		resblks;
	int			error;

	/*
	 * Perform all on-disk metadata updates required to inactivate inodes
	 * that the VFS evicted earlier in the unmount process.  Freeing inodes
	 * and discarding CoW fork preallocations can cause shape changes to
	 * the free inode and refcount btrees, respectively, so we must finish
	 * this before we discard the metadata space reservations.  Metadata
	 * inodes and the root directory do not require inactivation.
	 */
	xfs_inodegc_flush(mp);

	xfs_blockgc_stop(mp);
	xfs_fs_unreserve_ag_blocks(mp);
	xfs_qm_unmount_quotas(mp);
	xfs_rtunmount_inodes(mp);
	xfs_irele(mp->m_rootip);

	xfs_unmount_flush_inodes(mp);

	xfs_qm_unmount(mp);

	/*
	 * Unreserve any blocks we have so that when we unmount we don't account
	 * the reserved free space as used. This is really only necessary for
	 * lazy superblock counting because it trusts the incore superblock
	 * counters to be absolutely correct on clean unmount.
	 *
	 * We don't bother correcting this elsewhere for lazy superblock
	 * counting because on mount of an unclean filesystem we reconstruct the
	 * correct counter value and this is irrelevant.
	 *
	 * For non-lazy counter filesystems, this doesn't matter at all because
	 * we only every apply deltas to the superblock and hence the incore
	 * value does not matter....
	 */
	resblks = 0;
	error = xfs_reserve_blocks(mp, &resblks, NULL);
	if (error)
		xfs_warn(mp, "Unable to free reserved block pool. "
				"Freespace may not be correct on next mount.");
	xfs_unmount_check(mp);

	xfs_log_unmount(mp);
	xfs_da_unmount(mp);
	xfs_uuid_unmount(mp);

#if defined(DEBUG)
	xfs_errortag_clearall(mp);
#endif
	unregister_shrinker(&mp->m_inodegc_shrinker);
	xfs_free_perag(mp);

	xfs_errortag_del(mp);
	xfs_error_sysfs_del(mp);
	xchk_stats_unregister(mp->m_scrub_stats);
	xfs_sysfs_del(&mp->m_stats.xs_kobj);
	xfs_sysfs_del(&mp->m_kobj);
}

/*
 * Determine whether modifications can proceed. The caller specifies the minimum
 * freeze level for which modifications should not be allowed. This allows
 * certain operations to proceed while the freeze sequence is in progress, if
 * necessary.
 */
bool
xfs_fs_writable(
	struct xfs_mount	*mp,
	int			level)
{
	ASSERT(level > SB_UNFROZEN);
	if ((mp->m_super->s_writers.frozen >= level) ||
	    xfs_is_shutdown(mp) || xfs_is_readonly(mp))
		return false;

	return true;
}

/* Adjust m_fdblocks or m_frextents. */
int
xfs_mod_freecounter(
	struct xfs_mount	*mp,
	struct percpu_counter	*counter,
	int64_t			delta,
	bool			rsvd)
{
	int64_t			lcounter;
	long long		res_used;
	uint64_t		set_aside = 0;
	s32			batch;
	bool			has_resv_pool;

	ASSERT(counter == &mp->m_fdblocks || counter == &mp->m_frextents);
	has_resv_pool = (counter == &mp->m_fdblocks);
	if (rsvd)
		ASSERT(has_resv_pool);

	if (delta > 0) {
		/*
		 * If the reserve pool is depleted, put blocks back into it
		 * first. Most of the time the pool is full.
		 */
		if (likely(!has_resv_pool ||
			   mp->m_resblks == mp->m_resblks_avail)) {
			percpu_counter_add(counter, delta);
			return 0;
		}

		spin_lock(&mp->m_sb_lock);
		res_used = (long long)(mp->m_resblks - mp->m_resblks_avail);

		if (res_used > delta) {
			mp->m_resblks_avail += delta;
		} else {
			delta -= res_used;
			mp->m_resblks_avail = mp->m_resblks;
			percpu_counter_add(counter, delta);
		}
		spin_unlock(&mp->m_sb_lock);
		return 0;
	}

	/*
	 * Taking blocks away, need to be more accurate the closer we
	 * are to zero.
	 *
	 * If the counter has a value of less than 2 * max batch size,
	 * then make everything serialise as we are real close to
	 * ENOSPC.
	 */
	if (__percpu_counter_compare(counter, 2 * XFS_FDBLOCKS_BATCH,
				     XFS_FDBLOCKS_BATCH) < 0)
		batch = 1;
	else
		batch = XFS_FDBLOCKS_BATCH;

	/*
	 * Set aside allocbt blocks because these blocks are tracked as free
	 * space but not available for allocation. Technically this means that a
	 * single reservation cannot consume all remaining free space, but the
	 * ratio of allocbt blocks to usable free blocks should be rather small.
	 * The tradeoff without this is that filesystems that maintain high
	 * perag block reservations can over reserve physical block availability
	 * and fail physical allocation, which leads to much more serious
	 * problems (i.e. transaction abort, pagecache discards, etc.) than
	 * slightly premature -ENOSPC.
	 */
	if (has_resv_pool)
		set_aside = xfs_fdblocks_unavailable(mp);
	percpu_counter_add_batch(counter, delta, batch);
	if (__percpu_counter_compare(counter, set_aside,
				     XFS_FDBLOCKS_BATCH) >= 0) {
		/* we had space! */
		return 0;
	}

	/*
	 * lock up the sb for dipping into reserves before releasing the space
	 * that took us to ENOSPC.
	 */
	spin_lock(&mp->m_sb_lock);
	percpu_counter_add(counter, -delta);
	if (!has_resv_pool || !rsvd)
		goto fdblocks_enospc;

	lcounter = (long long)mp->m_resblks_avail + delta;
	if (lcounter >= 0) {
		mp->m_resblks_avail = lcounter;
		spin_unlock(&mp->m_sb_lock);
		return 0;
	}
	xfs_warn_once(mp,
"Reserve blocks depleted! Consider increasing reserve pool size.");

fdblocks_enospc:
	spin_unlock(&mp->m_sb_lock);
	return -ENOSPC;
}

/*
 * Used to free the superblock along various error paths.
 */
void
xfs_freesb(
	struct xfs_mount	*mp)
{
	struct xfs_buf		*bp = mp->m_sb_bp;

	xfs_buf_lock(bp);
	mp->m_sb_bp = NULL;
	xfs_buf_relse(bp);
}

/*
 * If the underlying (data/log/rt) device is readonly, there are some
 * operations that cannot proceed.
 */
int
xfs_dev_is_read_only(
	struct xfs_mount	*mp,
	char			*message)
{
	if (xfs_readonly_buftarg(mp->m_ddev_targp) ||
	    xfs_readonly_buftarg(mp->m_logdev_targp) ||
	    (mp->m_rtdev_targp && xfs_readonly_buftarg(mp->m_rtdev_targp))) {
		xfs_notice(mp, "%s required on read-only device.", message);
		xfs_notice(mp, "write access unavailable, cannot proceed.");
		return -EROFS;
	}
	return 0;
}

/* Force the summary counters to be recalculated at next mount. */
void
xfs_force_summary_recalc(
	struct xfs_mount	*mp)
{
	if (!xfs_has_lazysbcount(mp))
		return;

	xfs_fs_mark_sick(mp, XFS_SICK_FS_COUNTERS);
}

/*
 * Enable a log incompat feature flag in the primary superblock.  The caller
 * cannot have any other transactions in progress.
 */
int
xfs_add_incompat_log_feature(
	struct xfs_mount	*mp,
	uint32_t		feature)
{
	struct xfs_dsb		*dsb;
	int			error;

	ASSERT(hweight32(feature) == 1);
	ASSERT(!(feature & XFS_SB_FEAT_INCOMPAT_LOG_UNKNOWN));

	/*
	 * Force the log to disk and kick the background AIL thread to reduce
	 * the chances that the bwrite will stall waiting for the AIL to unpin
	 * the primary superblock buffer.  This isn't a data integrity
	 * operation, so we don't need a synchronous push.
	 */
	error = xfs_log_force(mp, XFS_LOG_SYNC);
	if (error)
		return error;
	xfs_ail_push_all(mp->m_ail);

	/*
	 * Lock the primary superblock buffer to serialize all callers that
	 * are trying to set feature bits.
	 */
	xfs_buf_lock(mp->m_sb_bp);
	xfs_buf_hold(mp->m_sb_bp);

	if (xfs_is_shutdown(mp)) {
		error = -EIO;
		goto rele;
	}

	if (xfs_sb_has_incompat_log_feature(&mp->m_sb, feature))
		goto rele;

	/*
	 * Write the primary superblock to disk immediately, because we need
	 * the log_incompat bit to be set in the primary super now to protect
	 * the log items that we're going to commit later.
	 */
	dsb = mp->m_sb_bp->b_addr;
	xfs_sb_to_disk(dsb, &mp->m_sb);
	dsb->sb_features_log_incompat |= cpu_to_be32(feature);
	error = xfs_bwrite(mp->m_sb_bp);
	if (error)
		goto shutdown;

	/*
	 * Add the feature bits to the incore superblock before we unlock the
	 * buffer.
	 */
	xfs_sb_add_incompat_log_features(&mp->m_sb, feature);
	xfs_buf_relse(mp->m_sb_bp);

	/* Log the superblock to disk. */
	return xfs_sync_sb(mp, false);
shutdown:
	xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
rele:
	xfs_buf_relse(mp->m_sb_bp);
	return error;
}

/*
 * Clear all the log incompat flags from the superblock.
 *
 * The caller cannot be in a transaction, must ensure that the log does not
 * contain any log items protected by any log incompat bit, and must ensure
 * that there are no other threads that depend on the state of the log incompat
 * feature flags in the primary super.
 *
 * Returns true if the superblock is dirty.
 */
bool
xfs_clear_incompat_log_features(
	struct xfs_mount	*mp)
{
	bool			ret = false;

	if (!xfs_has_crc(mp) ||
	    !xfs_sb_has_incompat_log_feature(&mp->m_sb,
				XFS_SB_FEAT_INCOMPAT_LOG_ALL) ||
	    xfs_is_shutdown(mp))
		return false;

	/*
	 * Update the incore superblock.  We synchronize on the primary super
	 * buffer lock to be consistent with the add function, though at least
	 * in theory this shouldn't be necessary.
	 */
	xfs_buf_lock(mp->m_sb_bp);
	xfs_buf_hold(mp->m_sb_bp);

	if (xfs_sb_has_incompat_log_feature(&mp->m_sb,
				XFS_SB_FEAT_INCOMPAT_LOG_ALL)) {
		xfs_sb_remove_incompat_log_features(&mp->m_sb);
		ret = true;
	}

	xfs_buf_relse(mp->m_sb_bp);
	return ret;
}

/*
 * Update the in-core delayed block counter.
 *
 * We prefer to update the counter without having to take a spinlock for every
 * counter update (i.e. batching).  Each change to delayed allocation
 * reservations can change can easily exceed the default percpu counter
 * batching, so we use a larger batch factor here.
 *
 * Note that we don't currently have any callers requiring fast summation
 * (e.g. percpu_counter_read) so we can use a big batch value here.
 */
#define XFS_DELALLOC_BATCH	(4096)
void
xfs_mod_delalloc(
	struct xfs_mount	*mp,
	int64_t			delta)
{
	percpu_counter_add_batch(&mp->m_delalloc_blks, delta,
			XFS_DELALLOC_BATCH);
}
