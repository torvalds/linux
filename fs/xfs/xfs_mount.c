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
#include "xfs_defer.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_inode.h"
#include "xfs_dir2.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_rtalloc.h"
#include "xfs_bmap.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_log.h"
#include "xfs_error.h"
#include "xfs_quota.h"
#include "xfs_fsops.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_sysfs.h"
#include "xfs_rmap_btree.h"
#include "xfs_refcount_btree.h"
#include "xfs_reflink.h"
#include "xfs_extent_busy.h"


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

	if (mp->m_flags & XFS_MOUNT_NOUUID)
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
		xfs_uuid_table = kmem_realloc(xfs_uuid_table,
			(xfs_uuid_table_size + 1) * sizeof(*xfs_uuid_table),
			KM_SLEEP);
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

	if (mp->m_flags & XFS_MOUNT_NOUUID)
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


STATIC void
__xfs_free_perag(
	struct rcu_head	*head)
{
	struct xfs_perag *pag = container_of(head, struct xfs_perag, rcu_head);

	ASSERT(atomic_read(&pag->pag_ref) == 0);
	kmem_free(pag);
}

/*
 * Free up the per-ag resources associated with the mount structure.
 */
STATIC void
xfs_free_perag(
	xfs_mount_t	*mp)
{
	xfs_agnumber_t	agno;
	struct xfs_perag *pag;

	for (agno = 0; agno < mp->m_sb.sb_agcount; agno++) {
		spin_lock(&mp->m_perag_lock);
		pag = radix_tree_delete(&mp->m_perag_tree, agno);
		spin_unlock(&mp->m_perag_lock);
		ASSERT(pag);
		ASSERT(atomic_read(&pag->pag_ref) == 0);
		xfs_buf_hash_destroy(pag);
		mutex_destroy(&pag->pag_ici_reclaim_lock);
		call_rcu(&pag->rcu_head, __xfs_free_perag);
	}
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

int
xfs_initialize_perag(
	xfs_mount_t	*mp,
	xfs_agnumber_t	agcount,
	xfs_agnumber_t	*maxagi)
{
	xfs_agnumber_t	index;
	xfs_agnumber_t	first_initialised = NULLAGNUMBER;
	xfs_perag_t	*pag;
	int		error = -ENOMEM;

	/*
	 * Walk the current per-ag tree so we don't try to initialise AGs
	 * that already exist (growfs case). Allocate and insert all the
	 * AGs we don't find ready for initialisation.
	 */
	for (index = 0; index < agcount; index++) {
		pag = xfs_perag_get(mp, index);
		if (pag) {
			xfs_perag_put(pag);
			continue;
		}

		pag = kmem_zalloc(sizeof(*pag), KM_MAYFAIL);
		if (!pag)
			goto out_unwind_new_pags;
		pag->pag_agno = index;
		pag->pag_mount = mp;
		spin_lock_init(&pag->pag_ici_lock);
		mutex_init(&pag->pag_ici_reclaim_lock);
		INIT_RADIX_TREE(&pag->pag_ici_root, GFP_ATOMIC);
		if (xfs_buf_hash_init(pag))
			goto out_free_pag;
		init_waitqueue_head(&pag->pagb_wait);
		spin_lock_init(&pag->pagb_lock);
		pag->pagb_count = 0;
		pag->pagb_tree = RB_ROOT;

		if (radix_tree_preload(GFP_NOFS))
			goto out_hash_destroy;

		spin_lock(&mp->m_perag_lock);
		if (radix_tree_insert(&mp->m_perag_tree, index, pag)) {
			BUG();
			spin_unlock(&mp->m_perag_lock);
			radix_tree_preload_end();
			error = -EEXIST;
			goto out_hash_destroy;
		}
		spin_unlock(&mp->m_perag_lock);
		radix_tree_preload_end();
		/* first new pag is fully initialized */
		if (first_initialised == NULLAGNUMBER)
			first_initialised = index;
	}

	index = xfs_set_inode_alloc(mp, agcount);

	if (maxagi)
		*maxagi = index;

	mp->m_ag_prealloc_blocks = xfs_prealloc_blocks(mp);
	return 0;

out_hash_destroy:
	xfs_buf_hash_destroy(pag);
out_free_pag:
	mutex_destroy(&pag->pag_ici_reclaim_lock);
	kmem_free(pag);
out_unwind_new_pags:
	/* unwind any prior newly initialized pags */
	for (index = first_initialised; index < agcount; index++) {
		pag = radix_tree_delete(&mp->m_perag_tree, index);
		if (!pag)
			break;
		xfs_buf_hash_destroy(pag);
		mutex_destroy(&pag->pag_ici_reclaim_lock);
		kmem_free(pag);
	}
	return error;
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
	xfs_sb_from_disk(sbp, XFS_BUF_TO_SBP(bp));

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
 * Update alignment values based on mount options and sb values
 */
STATIC int
xfs_update_alignment(xfs_mount_t *mp)
{
	xfs_sb_t	*sbp = &(mp->m_sb);

	if (mp->m_dalign) {
		/*
		 * If stripe unit and stripe width are not multiples
		 * of the fs blocksize turn off alignment.
		 */
		if ((BBTOB(mp->m_dalign) & mp->m_blockmask) ||
		    (BBTOB(mp->m_swidth) & mp->m_blockmask)) {
			xfs_warn(mp,
		"alignment check failed: sunit/swidth vs. blocksize(%d)",
				sbp->sb_blocksize);
			return -EINVAL;
		} else {
			/*
			 * Convert the stripe unit and width to FSBs.
			 */
			mp->m_dalign = XFS_BB_TO_FSBT(mp, mp->m_dalign);
			if (mp->m_dalign && (sbp->sb_agblocks % mp->m_dalign)) {
				xfs_warn(mp,
			"alignment check failed: sunit/swidth vs. agsize(%d)",
					 sbp->sb_agblocks);
				return -EINVAL;
			} else if (mp->m_dalign) {
				mp->m_swidth = XFS_BB_TO_FSBT(mp, mp->m_swidth);
			} else {
				xfs_warn(mp,
			"alignment check failed: sunit(%d) less than bsize(%d)",
					 mp->m_dalign, sbp->sb_blocksize);
				return -EINVAL;
			}
		}

		/*
		 * Update superblock with new values
		 * and log changes
		 */
		if (xfs_sb_version_hasdalign(sbp)) {
			if (sbp->sb_unit != mp->m_dalign) {
				sbp->sb_unit = mp->m_dalign;
				mp->m_update_sb = true;
			}
			if (sbp->sb_width != mp->m_swidth) {
				sbp->sb_width = mp->m_swidth;
				mp->m_update_sb = true;
			}
		} else {
			xfs_warn(mp,
	"cannot change alignment: superblock does not support data alignment");
			return -EINVAL;
		}
	} else if ((mp->m_flags & XFS_MOUNT_NOALIGN) != XFS_MOUNT_NOALIGN &&
		    xfs_sb_version_hasdalign(&mp->m_sb)) {
			mp->m_dalign = sbp->sb_unit;
			mp->m_swidth = sbp->sb_width;
	}

	return 0;
}

/*
 * Set the maximum inode count for this filesystem
 */
STATIC void
xfs_set_maxicount(xfs_mount_t *mp)
{
	xfs_sb_t	*sbp = &(mp->m_sb);
	uint64_t	icount;

	if (sbp->sb_imax_pct) {
		/*
		 * Make sure the maximum inode count is a multiple
		 * of the units we allocate inodes in.
		 */
		icount = sbp->sb_dblocks * sbp->sb_imax_pct;
		do_div(icount, 100);
		do_div(icount, mp->m_ialloc_blks);
		mp->m_maxicount = (icount * mp->m_ialloc_blks)  <<
				   sbp->sb_inopblog;
	} else {
		mp->m_maxicount = 0;
	}
}

/*
 * Set the default minimum read and write sizes unless
 * already specified in a mount option.
 * We use smaller I/O sizes when the file system
 * is being used for NFS service (wsync mount option).
 */
STATIC void
xfs_set_rw_sizes(xfs_mount_t *mp)
{
	xfs_sb_t	*sbp = &(mp->m_sb);
	int		readio_log, writeio_log;

	if (!(mp->m_flags & XFS_MOUNT_DFLT_IOSIZE)) {
		if (mp->m_flags & XFS_MOUNT_WSYNC) {
			readio_log = XFS_WSYNC_READIO_LOG;
			writeio_log = XFS_WSYNC_WRITEIO_LOG;
		} else {
			readio_log = XFS_READIO_LOG_LARGE;
			writeio_log = XFS_WRITEIO_LOG_LARGE;
		}
	} else {
		readio_log = mp->m_readio_log;
		writeio_log = mp->m_writeio_log;
	}

	if (sbp->sb_blocklog > readio_log) {
		mp->m_readio_log = sbp->sb_blocklog;
	} else {
		mp->m_readio_log = readio_log;
	}
	mp->m_readio_blocks = 1 << (mp->m_readio_log - sbp->sb_blocklog);
	if (sbp->sb_blocklog > writeio_log) {
		mp->m_writeio_log = sbp->sb_blocklog;
	} else {
		mp->m_writeio_log = writeio_log;
	}
	mp->m_writeio_blocks = 1 << (mp->m_writeio_log - sbp->sb_blocklog);
}

/*
 * precalculate the low space thresholds for dynamic speculative preallocation.
 */
void
xfs_set_low_space_thresholds(
	struct xfs_mount	*mp)
{
	int i;

	for (i = 0; i < XFS_LOWSP_MAX; i++) {
		uint64_t space = mp->m_sb.sb_dblocks;

		do_div(space, 100);
		mp->m_low_space[i] = space * (i + 1);
	}
}


/*
 * Set whether we're using inode alignment.
 */
STATIC void
xfs_set_inoalignment(xfs_mount_t *mp)
{
	if (xfs_sb_version_hasalign(&mp->m_sb) &&
		mp->m_sb.sb_inoalignmt >= xfs_icluster_size_fsb(mp))
		mp->m_inoalign_mask = mp->m_sb.sb_inoalignmt - 1;
	else
		mp->m_inoalign_mask = 0;
	/*
	 * If we are using stripe alignment, check whether
	 * the stripe unit is a multiple of the inode alignment
	 */
	if (mp->m_dalign && mp->m_inoalign_mask &&
	    !(mp->m_dalign & mp->m_inoalign_mask))
		mp->m_sinoalign = mp->m_dalign;
	else
		mp->m_sinoalign = 0;
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
	if (XFS_LAST_UNMOUNT_WAS_CLEAN(mp) &&
	    (mp->m_sb.sb_fdblocks > mp->m_sb.sb_dblocks ||
	     !xfs_verify_icount(mp, mp->m_sb.sb_icount) ||
	     mp->m_sb.sb_ifree > mp->m_sb.sb_icount))
		mp->m_flags |= XFS_MOUNT_BAD_SUMMARY;

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
	if ((!xfs_sb_version_haslazysbcount(&mp->m_sb) ||
	     XFS_LAST_UNMOUNT_WAS_CLEAN(mp)) &&
	    !(mp->m_flags & XFS_MOUNT_BAD_SUMMARY))
		return 0;

	return xfs_initialize_perag_data(mp, mp->m_sb.sb_agcount);
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

		/*
		 * Re-check for ATTR2 in case it was found in bad_features2
		 * slot.
		 */
		if (xfs_sb_version_hasattr2(&mp->m_sb) &&
		   !(mp->m_flags & XFS_MOUNT_NOATTR2))
			mp->m_flags |= XFS_MOUNT_ATTR2;
	}

	if (xfs_sb_version_hasattr2(&mp->m_sb) &&
	   (mp->m_flags & XFS_MOUNT_NOATTR2)) {
		xfs_sb_version_removeattr2(&mp->m_sb);
		mp->m_update_sb = true;

		/* update sb_versionnum for the clearing of the morebits */
		if (!sbp->sb_features2)
			mp->m_update_sb = true;
	}

	/* always use v2 inodes by default now */
	if (!(mp->m_sb.sb_versionnum & XFS_SB_VERSION_NLINKBIT)) {
		mp->m_sb.sb_versionnum |= XFS_SB_VERSION_NLINKBIT;
		mp->m_update_sb = true;
	}

	/*
	 * Check if sb_agblocks is aligned at stripe boundary
	 * If sb_agblocks is NOT aligned turn off m_dalign since
	 * allocator alignment is within an ag, therefore ag has
	 * to be aligned at stripe boundary.
	 */
	error = xfs_update_alignment(mp);
	if (error)
		goto out;

	xfs_alloc_compute_maxlevels(mp);
	xfs_bmap_compute_maxlevels(mp, XFS_DATA_FORK);
	xfs_bmap_compute_maxlevels(mp, XFS_ATTR_FORK);
	xfs_ialloc_compute_maxlevels(mp);
	xfs_rmapbt_compute_maxlevels(mp);
	xfs_refcountbt_compute_maxlevels(mp);

	xfs_set_maxicount(mp);

	/* enable fail_at_unmount as default */
	mp->m_fail_unmount = true;

	error = xfs_sysfs_init(&mp->m_kobj, &xfs_mp_ktype, NULL, mp->m_fsname);
	if (error)
		goto out;

	error = xfs_sysfs_init(&mp->m_stats.xs_kobj, &xfs_stats_ktype,
			       &mp->m_kobj, "stats");
	if (error)
		goto out_remove_sysfs;

	error = xfs_error_sysfs_init(mp);
	if (error)
		goto out_del_stats;

	error = xfs_errortag_init(mp);
	if (error)
		goto out_remove_error_sysfs;

	error = xfs_uuid_mount(mp);
	if (error)
		goto out_remove_errortag;

	/*
	 * Set the minimum read and write sizes
	 */
	xfs_set_rw_sizes(mp);

	/* set the low space thresholds for dynamic preallocation */
	xfs_set_low_space_thresholds(mp);

	/*
	 * Set the inode cluster size.
	 * This may still be overridden by the file system
	 * block size if it is larger than the chosen cluster size.
	 *
	 * For v5 filesystems, scale the cluster size with the inode size to
	 * keep a constant ratio of inode per cluster buffer, but only if mkfs
	 * has set the inode alignment value appropriately for larger cluster
	 * sizes.
	 */
	mp->m_inode_cluster_size = XFS_INODE_BIG_CLUSTER_SIZE;
	if (xfs_sb_version_hascrc(&mp->m_sb)) {
		int	new_size = mp->m_inode_cluster_size;

		new_size *= mp->m_sb.sb_inodesize / XFS_DINODE_MIN_SIZE;
		if (mp->m_sb.sb_inoalignmt >= XFS_B_TO_FSBT(mp, new_size))
			mp->m_inode_cluster_size = new_size;
	}
	mp->m_blocks_per_cluster = xfs_icluster_size_fsb(mp);
	mp->m_inodes_per_cluster = XFS_FSB_TO_INO(mp, mp->m_blocks_per_cluster);
	mp->m_cluster_align = xfs_ialloc_cluster_alignment(mp);
	mp->m_cluster_align_inodes = XFS_FSB_TO_INO(mp, mp->m_cluster_align);

	/*
	 * If enabled, sparse inode chunk alignment is expected to match the
	 * cluster size. Full inode chunk alignment must match the chunk size,
	 * but that is checked on sb read verification...
	 */
	if (xfs_sb_version_hassparseinodes(&mp->m_sb) &&
	    mp->m_sb.sb_spino_align !=
			XFS_B_TO_FSBT(mp, mp->m_inode_cluster_size)) {
		xfs_warn(mp,
	"Sparse inode block alignment (%u) must match cluster size (%llu).",
			 mp->m_sb.sb_spino_align,
			 XFS_B_TO_FSBT(mp, mp->m_inode_cluster_size));
		error = -EINVAL;
		goto out_remove_uuid;
	}

	/*
	 * Set inode alignment fields
	 */
	xfs_set_inoalignment(mp);

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
	error = xfs_initialize_perag(mp, sbp->sb_agcount, &mp->m_maxagi);
	if (error) {
		xfs_warn(mp, "Failed per-ag init: %d", error);
		goto out_free_dir;
	}

	if (!sbp->sb_logblocks) {
		xfs_warn(mp, "no log defined");
		XFS_ERROR_REPORT("xfs_mountfs", XFS_ERRLEVEL_LOW, mp);
		error = -EFSCORRUPTED;
		goto out_free_perag;
	}

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
		goto out_fail_wait;
	}

	/* Make sure the summary counts are ok. */
	error = xfs_check_summary_counts(mp);
	if (error)
		goto out_log_dealloc;

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

	if (unlikely(!S_ISDIR(VFS_I(rip)->i_mode))) {
		xfs_warn(mp, "corrupted root inode %llu: not a directory",
			(unsigned long long)rip->i_ino);
		xfs_iunlock(rip, XFS_ILOCK_EXCL);
		XFS_ERROR_REPORT("xfs_mountfs_int(2)", XFS_ERRLEVEL_LOW,
				 mp);
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

	/*
	 * If this is a read-only mount defer the superblock updates until
	 * the next remount into writeable mode.  Otherwise we would never
	 * perform the update e.g. for the root filesystem.
	 */
	if (mp->m_update_sb && !(mp->m_flags & XFS_MOUNT_RDONLY)) {
		error = xfs_sync_sb(mp, false);
		if (error) {
			xfs_warn(mp, "failed to write sb changes");
			goto out_rtunmount;
		}
	}

	/*
	 * Initialise the XFS quota management subsystem for this mount
	 */
	if (XFS_IS_QUOTA_RUNNING(mp)) {
		error = xfs_qm_newmount(mp, &quotamount, &quotaflags);
		if (error)
			goto out_rtunmount;
	} else {
		ASSERT(!XFS_IS_QUOTA_ON(mp));

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
	 * read in.
	 */
	error = xfs_log_mount_finish(mp);
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
	if ((mp->m_flags & (XFS_MOUNT_RDONLY|XFS_MOUNT_NORECOVERY)) ==
							XFS_MOUNT_RDONLY) {
		xfs_quiesce_attr(mp);
	}

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
	if (!(mp->m_flags & XFS_MOUNT_RDONLY)) {
		resblks = xfs_default_resblks(mp);
		error = xfs_reserve_blocks(mp, &resblks, NULL);
		if (error)
			xfs_warn(mp,
	"Unable to allocate reserve blocks. Continuing without reserve pool.");

		/* Recover any CoW blocks that never got remapped. */
		error = xfs_reflink_recover_cow(mp);
		if (error) {
			xfs_err(mp,
	"Error %d recovering leftover CoW allocations.", error);
			xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
			goto out_quota;
		}

		/* Reserve AG blocks for future btree expansion. */
		error = xfs_fs_reserve_ag_blocks(mp);
		if (error && error != -ENOSPC)
			goto out_agresv;
	}

	return 0;

 out_agresv:
	xfs_fs_unreserve_ag_blocks(mp);
 out_quota:
	xfs_qm_unmount_quotas(mp);
 out_rtunmount:
	xfs_rtunmount_inodes(mp);
 out_rele_rip:
	xfs_irele(rip);
	/* Clean out dquots that might be in memory after quotacheck. */
	xfs_qm_unmount(mp);
	/*
	 * Cancel all delayed reclaim work and reclaim the inodes directly.
	 * We have to do this /after/ rtunmount and qm_unmount because those
	 * two will have scheduled delayed reclaim for the rt/quota inodes.
	 *
	 * This is slightly different from the unmountfs call sequence
	 * because we could be tearing down a partially set up mount.  In
	 * particular, if log_mount_finish fails we bail out without calling
	 * qm_unmount_quotas and therefore rely on qm_unmount to release the
	 * quota inodes.
	 */
	cancel_delayed_work_sync(&mp->m_reclaim_work);
	xfs_reclaim_inodes(mp, SYNC_WAIT);
 out_log_dealloc:
	mp->m_flags |= XFS_MOUNT_UNMOUNTING;
	xfs_log_mount_cancel(mp);
 out_fail_wait:
	if (mp->m_logdev_targp && mp->m_logdev_targp != mp->m_ddev_targp)
		xfs_wait_buftarg(mp->m_logdev_targp);
	xfs_wait_buftarg(mp->m_ddev_targp);
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
 out_del_stats:
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

	xfs_icache_disable_reclaim(mp);
	xfs_fs_unreserve_ag_blocks(mp);
	xfs_qm_unmount_quotas(mp);
	xfs_rtunmount_inodes(mp);
	xfs_irele(mp->m_rootip);

	/*
	 * We can potentially deadlock here if we have an inode cluster
	 * that has been freed has its buffer still pinned in memory because
	 * the transaction is still sitting in a iclog. The stale inodes
	 * on that buffer will have their flush locks held until the
	 * transaction hits the disk and the callbacks run. the inode
	 * flush takes the flush lock unconditionally and with nothing to
	 * push out the iclog we will never get that unlocked. hence we
	 * need to force the log first.
	 */
	xfs_log_force(mp, XFS_LOG_SYNC);

	/*
	 * Wait for all busy extents to be freed, including completion of
	 * any discard operation.
	 */
	xfs_extent_busy_wait_all(mp);
	flush_workqueue(xfs_discard_wq);

	/*
	 * We now need to tell the world we are unmounting. This will allow
	 * us to detect that the filesystem is going away and we should error
	 * out anything that we have been retrying in the background. This will
	 * prevent neverending retries in AIL pushing from hanging the unmount.
	 */
	mp->m_flags |= XFS_MOUNT_UNMOUNTING;

	/*
	 * Flush all pending changes from the AIL.
	 */
	xfs_ail_push_all_sync(mp->m_ail);

	/*
	 * And reclaim all inodes.  At this point there should be no dirty
	 * inodes and none should be pinned or locked, but use synchronous
	 * reclaim just to be sure. We can stop background inode reclaim
	 * here as well if it is still running.
	 */
	cancel_delayed_work_sync(&mp->m_reclaim_work);
	xfs_reclaim_inodes(mp, SYNC_WAIT);

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

	error = xfs_log_sbcount(mp);
	if (error)
		xfs_warn(mp, "Unable to update superblock counters. "
				"Freespace may not be correct on next mount.");


	xfs_log_unmount(mp);
	xfs_da_unmount(mp);
	xfs_uuid_unmount(mp);

#if defined(DEBUG)
	xfs_errortag_clearall(mp);
#endif
	xfs_free_perag(mp);

	xfs_errortag_del(mp);
	xfs_error_sysfs_del(mp);
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
	    XFS_FORCED_SHUTDOWN(mp) || (mp->m_flags & XFS_MOUNT_RDONLY))
		return false;

	return true;
}

/*
 * xfs_log_sbcount
 *
 * Sync the superblock counters to disk.
 *
 * Note this code can be called during the process of freezing, so we use the
 * transaction allocator that does not block when the transaction subsystem is
 * in its frozen state.
 */
int
xfs_log_sbcount(xfs_mount_t *mp)
{
	/* allow this to proceed during the freeze sequence... */
	if (!xfs_fs_writable(mp, SB_FREEZE_COMPLETE))
		return 0;

	/*
	 * we don't need to do this if we are updating the superblock
	 * counters on every modification.
	 */
	if (!xfs_sb_version_haslazysbcount(&mp->m_sb))
		return 0;

	return xfs_sync_sb(mp, true);
}

/*
 * Deltas for the inode count are +/-64, hence we use a large batch size
 * of 128 so we don't need to take the counter lock on every update.
 */
#define XFS_ICOUNT_BATCH	128
int
xfs_mod_icount(
	struct xfs_mount	*mp,
	int64_t			delta)
{
	percpu_counter_add_batch(&mp->m_icount, delta, XFS_ICOUNT_BATCH);
	if (__percpu_counter_compare(&mp->m_icount, 0, XFS_ICOUNT_BATCH) < 0) {
		ASSERT(0);
		percpu_counter_add(&mp->m_icount, -delta);
		return -EINVAL;
	}
	return 0;
}

int
xfs_mod_ifree(
	struct xfs_mount	*mp,
	int64_t			delta)
{
	percpu_counter_add(&mp->m_ifree, delta);
	if (percpu_counter_compare(&mp->m_ifree, 0) < 0) {
		ASSERT(0);
		percpu_counter_add(&mp->m_ifree, -delta);
		return -EINVAL;
	}
	return 0;
}

/*
 * Deltas for the block count can vary from 1 to very large, but lock contention
 * only occurs on frequent small block count updates such as in the delayed
 * allocation path for buffered writes (page a time updates). Hence we set
 * a large batch count (1024) to minimise global counter updates except when
 * we get near to ENOSPC and we have to be very accurate with our updates.
 */
#define XFS_FDBLOCKS_BATCH	1024
int
xfs_mod_fdblocks(
	struct xfs_mount	*mp,
	int64_t			delta,
	bool			rsvd)
{
	int64_t			lcounter;
	long long		res_used;
	s32			batch;

	if (delta > 0) {
		/*
		 * If the reserve pool is depleted, put blocks back into it
		 * first. Most of the time the pool is full.
		 */
		if (likely(mp->m_resblks == mp->m_resblks_avail)) {
			percpu_counter_add(&mp->m_fdblocks, delta);
			return 0;
		}

		spin_lock(&mp->m_sb_lock);
		res_used = (long long)(mp->m_resblks - mp->m_resblks_avail);

		if (res_used > delta) {
			mp->m_resblks_avail += delta;
		} else {
			delta -= res_used;
			mp->m_resblks_avail = mp->m_resblks;
			percpu_counter_add(&mp->m_fdblocks, delta);
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
	if (__percpu_counter_compare(&mp->m_fdblocks, 2 * XFS_FDBLOCKS_BATCH,
				     XFS_FDBLOCKS_BATCH) < 0)
		batch = 1;
	else
		batch = XFS_FDBLOCKS_BATCH;

	percpu_counter_add_batch(&mp->m_fdblocks, delta, batch);
	if (__percpu_counter_compare(&mp->m_fdblocks, mp->m_alloc_set_aside,
				     XFS_FDBLOCKS_BATCH) >= 0) {
		/* we had space! */
		return 0;
	}

	/*
	 * lock up the sb for dipping into reserves before releasing the space
	 * that took us to ENOSPC.
	 */
	spin_lock(&mp->m_sb_lock);
	percpu_counter_add(&mp->m_fdblocks, -delta);
	if (!rsvd)
		goto fdblocks_enospc;

	lcounter = (long long)mp->m_resblks_avail + delta;
	if (lcounter >= 0) {
		mp->m_resblks_avail = lcounter;
		spin_unlock(&mp->m_sb_lock);
		return 0;
	}
	printk_once(KERN_WARNING
		"Filesystem \"%s\": reserve blocks depleted! "
		"Consider increasing reserve pool size.",
		mp->m_fsname);
fdblocks_enospc:
	spin_unlock(&mp->m_sb_lock);
	return -ENOSPC;
}

int
xfs_mod_frextents(
	struct xfs_mount	*mp,
	int64_t			delta)
{
	int64_t			lcounter;
	int			ret = 0;

	spin_lock(&mp->m_sb_lock);
	lcounter = mp->m_sb.sb_frextents + delta;
	if (lcounter < 0)
		ret = -ENOSPC;
	else
		mp->m_sb.sb_frextents = lcounter;
	spin_unlock(&mp->m_sb_lock);
	return ret;
}

/*
 * xfs_getsb() is called to obtain the buffer for the superblock.
 * The buffer is returned locked and read in from disk.
 * The buffer should be released with a call to xfs_brelse().
 *
 * If the flags parameter is BUF_TRYLOCK, then we'll only return
 * the superblock buffer if it can be locked without sleeping.
 * If it can't then we'll return NULL.
 */
struct xfs_buf *
xfs_getsb(
	struct xfs_mount	*mp,
	int			flags)
{
	struct xfs_buf		*bp = mp->m_sb_bp;

	if (!xfs_buf_trylock(bp)) {
		if (flags & XBF_TRYLOCK)
			return NULL;
		xfs_buf_lock(bp);
	}

	xfs_buf_hold(bp);
	ASSERT(bp->b_flags & XBF_DONE);
	return bp;
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
	if (!xfs_sb_version_haslazysbcount(&mp->m_sb))
		return;

	spin_lock(&mp->m_sb_lock);
	mp->m_flags |= XFS_MOUNT_BAD_SUMMARY;
	spin_unlock(&mp->m_sb_lock);
}
