// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023-2025 Christoph Hellwig.
 * Copyright (c) 2024-2025, Western Digital Corporation or its affiliates.
 */
#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_error.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_iomap.h"
#include "xfs_trans.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_bmap_btree.h"
#include "xfs_trans_space.h"
#include "xfs_refcount.h"
#include "xfs_rtbitmap.h"
#include "xfs_rtrmap_btree.h"
#include "xfs_zone_alloc.h"
#include "xfs_zone_priv.h"
#include "xfs_zones.h"
#include "xfs_trace.h"

void
xfs_open_zone_put(
	struct xfs_open_zone	*oz)
{
	if (atomic_dec_and_test(&oz->oz_ref)) {
		xfs_rtgroup_rele(oz->oz_rtg);
		kfree(oz);
	}
}

static inline uint32_t
xfs_zone_bucket(
	struct xfs_mount	*mp,
	uint32_t		used_blocks)
{
	return XFS_ZONE_USED_BUCKETS * used_blocks /
			mp->m_groups[XG_TYPE_RTG].blocks;
}

static inline void
xfs_zone_add_to_bucket(
	struct xfs_zone_info	*zi,
	xfs_rgnumber_t		rgno,
	uint32_t		to_bucket)
{
	__set_bit(rgno, zi->zi_used_bucket_bitmap[to_bucket]);
	zi->zi_used_bucket_entries[to_bucket]++;
}

static inline void
xfs_zone_remove_from_bucket(
	struct xfs_zone_info	*zi,
	xfs_rgnumber_t		rgno,
	uint32_t		from_bucket)
{
	__clear_bit(rgno, zi->zi_used_bucket_bitmap[from_bucket]);
	zi->zi_used_bucket_entries[from_bucket]--;
}

static void
xfs_zone_account_reclaimable(
	struct xfs_rtgroup	*rtg,
	uint32_t		freed)
{
	struct xfs_group	*xg = &rtg->rtg_group;
	struct xfs_mount	*mp = rtg_mount(rtg);
	struct xfs_zone_info	*zi = mp->m_zone_info;
	uint32_t		used = rtg_rmap(rtg)->i_used_blocks;
	xfs_rgnumber_t		rgno = rtg_rgno(rtg);
	uint32_t		from_bucket = xfs_zone_bucket(mp, used + freed);
	uint32_t		to_bucket = xfs_zone_bucket(mp, used);
	bool			was_full = (used + freed == rtg_blocks(rtg));

	/*
	 * This can be called from log recovery, where the zone_info structure
	 * hasn't been allocated yet.  Skip all work as xfs_mount_zones will
	 * add the zones to the right buckets before the file systems becomes
	 * active.
	 */
	if (!zi)
		return;

	if (!used) {
		/*
		 * The zone is now empty, remove it from the bottom bucket and
		 * trigger a reset.
		 */
		trace_xfs_zone_emptied(rtg);

		if (!was_full)
			xfs_group_clear_mark(xg, XFS_RTG_RECLAIMABLE);

		spin_lock(&zi->zi_used_buckets_lock);
		if (!was_full)
			xfs_zone_remove_from_bucket(zi, rgno, from_bucket);
		spin_unlock(&zi->zi_used_buckets_lock);

		spin_lock(&zi->zi_reset_list_lock);
		xg->xg_next_reset = zi->zi_reset_list;
		zi->zi_reset_list = xg;
		spin_unlock(&zi->zi_reset_list_lock);

		if (zi->zi_gc_thread)
			wake_up_process(zi->zi_gc_thread);
	} else if (was_full) {
		/*
		 * The zone transitioned from full, mark it up as reclaimable
		 * and wake up GC which might be waiting for zones to reclaim.
		 */
		spin_lock(&zi->zi_used_buckets_lock);
		xfs_zone_add_to_bucket(zi, rgno, to_bucket);
		spin_unlock(&zi->zi_used_buckets_lock);

		xfs_group_set_mark(xg, XFS_RTG_RECLAIMABLE);
		if (zi->zi_gc_thread && xfs_zoned_need_gc(mp))
			wake_up_process(zi->zi_gc_thread);
	} else if (to_bucket != from_bucket) {
		/*
		 * Move the zone to a new bucket if it dropped below the
		 * threshold.
		 */
		spin_lock(&zi->zi_used_buckets_lock);
		xfs_zone_add_to_bucket(zi, rgno, to_bucket);
		xfs_zone_remove_from_bucket(zi, rgno, from_bucket);
		spin_unlock(&zi->zi_used_buckets_lock);
	}
}

static void
xfs_open_zone_mark_full(
	struct xfs_open_zone	*oz)
{
	struct xfs_rtgroup	*rtg = oz->oz_rtg;
	struct xfs_mount	*mp = rtg_mount(rtg);
	struct xfs_zone_info	*zi = mp->m_zone_info;
	uint32_t		used = rtg_rmap(rtg)->i_used_blocks;

	trace_xfs_zone_full(rtg);

	WRITE_ONCE(rtg->rtg_open_zone, NULL);

	spin_lock(&zi->zi_open_zones_lock);
	if (oz->oz_is_gc) {
		ASSERT(current == zi->zi_gc_thread);
		zi->zi_open_gc_zone = NULL;
	} else {
		zi->zi_nr_open_zones--;
		list_del_init(&oz->oz_entry);
	}
	spin_unlock(&zi->zi_open_zones_lock);
	xfs_open_zone_put(oz);

	wake_up_all(&zi->zi_zone_wait);
	if (used < rtg_blocks(rtg))
		xfs_zone_account_reclaimable(rtg, rtg_blocks(rtg) - used);
}

static void
xfs_zone_record_blocks(
	struct xfs_trans	*tp,
	xfs_fsblock_t		fsbno,
	xfs_filblks_t		len,
	struct xfs_open_zone	*oz,
	bool			used)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_rtgroup	*rtg = oz->oz_rtg;
	struct xfs_inode	*rmapip = rtg_rmap(rtg);

	trace_xfs_zone_record_blocks(oz, xfs_rtb_to_rgbno(mp, fsbno), len);

	xfs_rtgroup_lock(rtg, XFS_RTGLOCK_RMAP);
	xfs_rtgroup_trans_join(tp, rtg, XFS_RTGLOCK_RMAP);
	if (used) {
		rmapip->i_used_blocks += len;
		ASSERT(rmapip->i_used_blocks <= rtg_blocks(rtg));
	} else {
		xfs_add_frextents(mp, len);
	}
	oz->oz_written += len;
	if (oz->oz_written == rtg_blocks(rtg))
		xfs_open_zone_mark_full(oz);
	xfs_trans_log_inode(tp, rmapip, XFS_ILOG_CORE);
}

static int
xfs_zoned_map_extent(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	struct xfs_bmbt_irec	*new,
	struct xfs_open_zone	*oz,
	xfs_fsblock_t		old_startblock)
{
	struct xfs_bmbt_irec	data;
	int			nmaps = 1;
	int			error;

	/* Grab the corresponding mapping in the data fork. */
	error = xfs_bmapi_read(ip, new->br_startoff, new->br_blockcount, &data,
			       &nmaps, 0);
	if (error)
		return error;

	/*
	 * Cap the update to the existing extent in the data fork because we can
	 * only overwrite one extent at a time.
	 */
	ASSERT(new->br_blockcount >= data.br_blockcount);
	new->br_blockcount = data.br_blockcount;

	/*
	 * If a data write raced with this GC write, keep the existing data in
	 * the data fork, mark our newly written GC extent as reclaimable, then
	 * move on to the next extent.
	 */
	if (old_startblock != NULLFSBLOCK &&
	    old_startblock != data.br_startblock)
		goto skip;

	trace_xfs_reflink_cow_remap_from(ip, new);
	trace_xfs_reflink_cow_remap_to(ip, &data);

	error = xfs_iext_count_extend(tp, ip, XFS_DATA_FORK,
			XFS_IEXT_REFLINK_END_COW_CNT);
	if (error)
		return error;

	if (data.br_startblock != HOLESTARTBLOCK) {
		ASSERT(data.br_startblock != DELAYSTARTBLOCK);
		ASSERT(!isnullstartblock(data.br_startblock));

		xfs_bmap_unmap_extent(tp, ip, XFS_DATA_FORK, &data);
		if (xfs_is_reflink_inode(ip)) {
			xfs_refcount_decrease_extent(tp, true, &data);
		} else {
			error = xfs_free_extent_later(tp, data.br_startblock,
					data.br_blockcount, NULL,
					XFS_AG_RESV_NONE,
					XFS_FREE_EXTENT_REALTIME);
			if (error)
				return error;
		}
	}

	xfs_zone_record_blocks(tp, new->br_startblock, new->br_blockcount, oz,
			true);

	/* Map the new blocks into the data fork. */
	xfs_bmap_map_extent(tp, ip, XFS_DATA_FORK, new);
	return 0;

skip:
	trace_xfs_reflink_cow_remap_skip(ip, new);
	xfs_zone_record_blocks(tp, new->br_startblock, new->br_blockcount, oz,
			false);
	return 0;
}

int
xfs_zoned_end_io(
	struct xfs_inode	*ip,
	xfs_off_t		offset,
	xfs_off_t		count,
	xfs_daddr_t		daddr,
	struct xfs_open_zone	*oz,
	xfs_fsblock_t		old_startblock)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		end_fsb = XFS_B_TO_FSB(mp, offset + count);
	struct xfs_bmbt_irec	new = {
		.br_startoff	= XFS_B_TO_FSBT(mp, offset),
		.br_startblock	= xfs_daddr_to_rtb(mp, daddr),
		.br_state	= XFS_EXT_NORM,
	};
	unsigned int		resblks =
		XFS_EXTENTADD_SPACE_RES(mp, XFS_DATA_FORK);
	struct xfs_trans	*tp;
	int			error;

	if (xfs_is_shutdown(mp))
		return -EIO;

	while (new.br_startoff < end_fsb) {
		new.br_blockcount = end_fsb - new.br_startoff;

		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_write, resblks, 0,
				XFS_TRANS_RESERVE | XFS_TRANS_RES_FDBLKS, &tp);
		if (error)
			return error;
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		xfs_trans_ijoin(tp, ip, 0);

		error = xfs_zoned_map_extent(tp, ip, &new, oz, old_startblock);
		if (error)
			xfs_trans_cancel(tp);
		else
			error = xfs_trans_commit(tp);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		if (error)
			return error;

		new.br_startoff += new.br_blockcount;
		new.br_startblock += new.br_blockcount;
		if (old_startblock != NULLFSBLOCK)
			old_startblock += new.br_blockcount;
	}

	return 0;
}

/*
 * "Free" blocks allocated in a zone.
 *
 * Just decrement the used blocks counter and report the space as freed.
 */
int
xfs_zone_free_blocks(
	struct xfs_trans	*tp,
	struct xfs_rtgroup	*rtg,
	xfs_fsblock_t		fsbno,
	xfs_filblks_t		len)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_inode	*rmapip = rtg_rmap(rtg);

	xfs_assert_ilocked(rmapip, XFS_ILOCK_EXCL);

	if (len > rmapip->i_used_blocks) {
		xfs_err(mp,
"trying to free more blocks (%lld) than used counter (%u).",
			len, rmapip->i_used_blocks);
		ASSERT(len <= rmapip->i_used_blocks);
		xfs_rtginode_mark_sick(rtg, XFS_RTGI_RMAP);
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
		return -EFSCORRUPTED;
	}

	trace_xfs_zone_free_blocks(rtg, xfs_rtb_to_rgbno(mp, fsbno), len);

	rmapip->i_used_blocks -= len;
	/*
	 * Don't add open zones to the reclaimable buckets.  The I/O completion
	 * for writing the last block will take care of accounting for already
	 * unused blocks instead.
	 */
	if (!READ_ONCE(rtg->rtg_open_zone))
		xfs_zone_account_reclaimable(rtg, len);
	xfs_add_frextents(mp, len);
	xfs_trans_log_inode(tp, rmapip, XFS_ILOG_CORE);
	return 0;
}

/*
 * Check if the zone containing the data just before the offset we are
 * writing to is still open and has space.
 */
static struct xfs_open_zone *
xfs_last_used_zone(
	struct iomap_ioend	*ioend)
{
	struct xfs_inode	*ip = XFS_I(ioend->io_inode);
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSB(mp, ioend->io_offset);
	struct xfs_rtgroup	*rtg = NULL;
	struct xfs_open_zone	*oz = NULL;
	struct xfs_iext_cursor	icur;
	struct xfs_bmbt_irec	got;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	if (!xfs_iext_lookup_extent_before(ip, &ip->i_df, &offset_fsb,
				&icur, &got)) {
		xfs_iunlock(ip, XFS_ILOCK_SHARED);
		return NULL;
	}
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	rtg = xfs_rtgroup_grab(mp, xfs_rtb_to_rgno(mp, got.br_startblock));
	if (!rtg)
		return NULL;

	xfs_ilock(rtg_rmap(rtg), XFS_ILOCK_SHARED);
	oz = READ_ONCE(rtg->rtg_open_zone);
	if (oz && (oz->oz_is_gc || !atomic_inc_not_zero(&oz->oz_ref)))
		oz = NULL;
	xfs_iunlock(rtg_rmap(rtg), XFS_ILOCK_SHARED);

	xfs_rtgroup_rele(rtg);
	return oz;
}

static struct xfs_group *
xfs_find_free_zone(
	struct xfs_mount	*mp,
	unsigned long		start,
	unsigned long		end)
{
	struct xfs_zone_info	*zi = mp->m_zone_info;
	XA_STATE		(xas, &mp->m_groups[XG_TYPE_RTG].xa, start);
	struct xfs_group	*xg;

	xas_lock(&xas);
	xas_for_each_marked(&xas, xg, end, XFS_RTG_FREE)
		if (atomic_inc_not_zero(&xg->xg_active_ref))
			goto found;
	xas_unlock(&xas);
	return NULL;

found:
	xas_clear_mark(&xas, XFS_RTG_FREE);
	atomic_dec(&zi->zi_nr_free_zones);
	zi->zi_free_zone_cursor = xg->xg_gno;
	xas_unlock(&xas);
	return xg;
}

static struct xfs_open_zone *
xfs_init_open_zone(
	struct xfs_rtgroup	*rtg,
	xfs_rgblock_t		write_pointer,
	enum rw_hint		write_hint,
	bool			is_gc)
{
	struct xfs_open_zone	*oz;

	oz = kzalloc(sizeof(*oz), GFP_NOFS | __GFP_NOFAIL);
	spin_lock_init(&oz->oz_alloc_lock);
	atomic_set(&oz->oz_ref, 1);
	oz->oz_rtg = rtg;
	oz->oz_write_pointer = write_pointer;
	oz->oz_written = write_pointer;
	oz->oz_write_hint = write_hint;
	oz->oz_is_gc = is_gc;

	/*
	 * All dereferences of rtg->rtg_open_zone hold the ILOCK for the rmap
	 * inode, but we don't really want to take that here because we are
	 * under the zone_list_lock.  Ensure the pointer is only set for a fully
	 * initialized open zone structure so that a racy lookup finding it is
	 * fine.
	 */
	WRITE_ONCE(rtg->rtg_open_zone, oz);
	return oz;
}

/*
 * Find a completely free zone, open it, and return a reference.
 */
struct xfs_open_zone *
xfs_open_zone(
	struct xfs_mount	*mp,
	enum rw_hint		write_hint,
	bool			is_gc)
{
	struct xfs_zone_info	*zi = mp->m_zone_info;
	struct xfs_group	*xg;

	xg = xfs_find_free_zone(mp, zi->zi_free_zone_cursor, ULONG_MAX);
	if (!xg)
		xg = xfs_find_free_zone(mp, 0, zi->zi_free_zone_cursor);
	if (!xg)
		return NULL;

	set_current_state(TASK_RUNNING);
	return xfs_init_open_zone(to_rtg(xg), 0, write_hint, is_gc);
}

static struct xfs_open_zone *
xfs_try_open_zone(
	struct xfs_mount	*mp,
	enum rw_hint		write_hint)
{
	struct xfs_zone_info	*zi = mp->m_zone_info;
	struct xfs_open_zone	*oz;

	if (zi->zi_nr_open_zones >= mp->m_max_open_zones - XFS_OPEN_GC_ZONES)
		return NULL;
	if (atomic_read(&zi->zi_nr_free_zones) <
	    XFS_GC_ZONES - XFS_OPEN_GC_ZONES)
		return NULL;

	/*
	 * Increment the open zone count to reserve our slot before dropping
	 * zi_open_zones_lock.
	 */
	zi->zi_nr_open_zones++;
	spin_unlock(&zi->zi_open_zones_lock);
	oz = xfs_open_zone(mp, write_hint, false);
	spin_lock(&zi->zi_open_zones_lock);
	if (!oz) {
		zi->zi_nr_open_zones--;
		return NULL;
	}

	atomic_inc(&oz->oz_ref);
	list_add_tail(&oz->oz_entry, &zi->zi_open_zones);

	/*
	 * If this was the last free zone, other waiters might be waiting
	 * on us to write to it as well.
	 */
	wake_up_all(&zi->zi_zone_wait);

	if (xfs_zoned_need_gc(mp))
		wake_up_process(zi->zi_gc_thread);

	trace_xfs_zone_opened(oz->oz_rtg);
	return oz;
}

/*
 * For data with short or medium lifetime, try to colocated it into an
 * already open zone with a matching temperature.
 */
static bool
xfs_colocate_eagerly(
	enum rw_hint		file_hint)
{
	switch (file_hint) {
	case WRITE_LIFE_MEDIUM:
	case WRITE_LIFE_SHORT:
	case WRITE_LIFE_NONE:
		return true;
	default:
		return false;
	}
}

static bool
xfs_good_hint_match(
	struct xfs_open_zone	*oz,
	enum rw_hint		file_hint)
{
	switch (oz->oz_write_hint) {
	case WRITE_LIFE_LONG:
	case WRITE_LIFE_EXTREME:
		/* colocate long and extreme */
		if (file_hint == WRITE_LIFE_LONG ||
		    file_hint == WRITE_LIFE_EXTREME)
			return true;
		break;
	case WRITE_LIFE_MEDIUM:
		/* colocate medium with medium */
		if (file_hint == WRITE_LIFE_MEDIUM)
			return true;
		break;
	case WRITE_LIFE_SHORT:
	case WRITE_LIFE_NONE:
	case WRITE_LIFE_NOT_SET:
		/* colocate short and none */
		if (file_hint <= WRITE_LIFE_SHORT)
			return true;
		break;
	}
	return false;
}

static bool
xfs_try_use_zone(
	struct xfs_zone_info	*zi,
	enum rw_hint		file_hint,
	struct xfs_open_zone	*oz,
	bool			lowspace)
{
	if (oz->oz_write_pointer == rtg_blocks(oz->oz_rtg))
		return false;
	if (!lowspace && !xfs_good_hint_match(oz, file_hint))
		return false;
	if (!atomic_inc_not_zero(&oz->oz_ref))
		return false;

	/*
	 * If we have a hint set for the data, use that for the zone even if
	 * some data was written already without any hint set, but don't change
	 * the temperature after that as that would make little sense without
	 * tracking per-temperature class written block counts, which is
	 * probably overkill anyway.
	 */
	if (file_hint != WRITE_LIFE_NOT_SET &&
	    oz->oz_write_hint == WRITE_LIFE_NOT_SET)
		oz->oz_write_hint = file_hint;

	/*
	 * If we couldn't match by inode or life time we just pick the first
	 * zone with enough space above.  For that we want the least busy zone
	 * for some definition of "least" busy.  For now this simple LRU
	 * algorithm that rotates every zone to the end of the list will do it,
	 * even if it isn't exactly cache friendly.
	 */
	if (!list_is_last(&oz->oz_entry, &zi->zi_open_zones))
		list_move_tail(&oz->oz_entry, &zi->zi_open_zones);
	return true;
}

static struct xfs_open_zone *
xfs_select_open_zone_lru(
	struct xfs_zone_info	*zi,
	enum rw_hint		file_hint,
	bool			lowspace)
{
	struct xfs_open_zone	*oz;

	lockdep_assert_held(&zi->zi_open_zones_lock);

	list_for_each_entry(oz, &zi->zi_open_zones, oz_entry)
		if (xfs_try_use_zone(zi, file_hint, oz, lowspace))
			return oz;

	cond_resched_lock(&zi->zi_open_zones_lock);
	return NULL;
}

static struct xfs_open_zone *
xfs_select_open_zone_mru(
	struct xfs_zone_info	*zi,
	enum rw_hint		file_hint)
{
	struct xfs_open_zone	*oz;

	lockdep_assert_held(&zi->zi_open_zones_lock);

	list_for_each_entry_reverse(oz, &zi->zi_open_zones, oz_entry)
		if (xfs_try_use_zone(zi, file_hint, oz, false))
			return oz;

	cond_resched_lock(&zi->zi_open_zones_lock);
	return NULL;
}

static inline enum rw_hint xfs_inode_write_hint(struct xfs_inode *ip)
{
	if (xfs_has_nolifetime(ip->i_mount))
		return WRITE_LIFE_NOT_SET;
	return VFS_I(ip)->i_write_hint;
}

/*
 * Try to pack inodes that are written back after they were closed tight instead
 * of trying to open new zones for them or spread them to the least recently
 * used zone.  This optimizes the data layout for workloads that untar or copy
 * a lot of small files.  Right now this does not separate multiple such
 * streams.
 */
static inline bool xfs_zoned_pack_tight(struct xfs_inode *ip)
{
	return !inode_is_open_for_write(VFS_I(ip)) &&
		!(ip->i_diflags & XFS_DIFLAG_APPEND);
}

/*
 * Pick a new zone for writes.
 *
 * If we aren't using up our budget of open zones just open a new one from the
 * freelist.  Else try to find one that matches the expected data lifetime.  If
 * we don't find one that is good pick any zone that is available.
 */
static struct xfs_open_zone *
xfs_select_zone_nowait(
	struct xfs_mount	*mp,
	enum rw_hint		write_hint,
	bool			pack_tight)
{
	struct xfs_zone_info	*zi = mp->m_zone_info;
	struct xfs_open_zone	*oz = NULL;

	if (xfs_is_shutdown(mp))
		return NULL;

	/*
	 * Try to fill up open zones with matching temperature if available.  It
	 * is better to try to co-locate data when this is favorable, so we can
	 * activate empty zones when it is statistically better to separate
	 * data.
	 */
	spin_lock(&zi->zi_open_zones_lock);
	if (xfs_colocate_eagerly(write_hint))
		oz = xfs_select_open_zone_lru(zi, write_hint, false);
	else if (pack_tight)
		oz = xfs_select_open_zone_mru(zi, write_hint);
	if (oz)
		goto out_unlock;

	/*
	 * See if we can open a new zone and use that.
	 */
	oz = xfs_try_open_zone(mp, write_hint);
	if (oz)
		goto out_unlock;

	/*
	 * Try to colocate cold data with other cold data if we failed to open a
	 * new zone for it.
	 */
	if (write_hint != WRITE_LIFE_NOT_SET &&
	    !xfs_colocate_eagerly(write_hint))
		oz = xfs_select_open_zone_lru(zi, write_hint, false);
	if (!oz)
		oz = xfs_select_open_zone_lru(zi, WRITE_LIFE_NOT_SET, false);
	if (!oz)
		oz = xfs_select_open_zone_lru(zi, WRITE_LIFE_NOT_SET, true);
out_unlock:
	spin_unlock(&zi->zi_open_zones_lock);
	return oz;
}

static struct xfs_open_zone *
xfs_select_zone(
	struct xfs_mount	*mp,
	enum rw_hint		write_hint,
	bool			pack_tight)
{
	struct xfs_zone_info	*zi = mp->m_zone_info;
	DEFINE_WAIT		(wait);
	struct xfs_open_zone	*oz;

	oz = xfs_select_zone_nowait(mp, write_hint, pack_tight);
	if (oz)
		return oz;

	for (;;) {
		prepare_to_wait(&zi->zi_zone_wait, &wait, TASK_UNINTERRUPTIBLE);
		oz = xfs_select_zone_nowait(mp, write_hint, pack_tight);
		if (oz)
			break;
		schedule();
	}
	finish_wait(&zi->zi_zone_wait, &wait);
	return oz;
}

static unsigned int
xfs_zone_alloc_blocks(
	struct xfs_open_zone	*oz,
	xfs_filblks_t		count_fsb,
	sector_t		*sector,
	bool			*is_seq)
{
	struct xfs_rtgroup	*rtg = oz->oz_rtg;
	struct xfs_mount	*mp = rtg_mount(rtg);
	xfs_rgblock_t		rgbno;

	spin_lock(&oz->oz_alloc_lock);
	count_fsb = min3(count_fsb, XFS_MAX_BMBT_EXTLEN,
		(xfs_filblks_t)rtg_blocks(rtg) - oz->oz_write_pointer);
	if (!count_fsb) {
		spin_unlock(&oz->oz_alloc_lock);
		return 0;
	}
	rgbno = oz->oz_write_pointer;
	oz->oz_write_pointer += count_fsb;
	spin_unlock(&oz->oz_alloc_lock);

	trace_xfs_zone_alloc_blocks(oz, rgbno, count_fsb);

	*sector = xfs_gbno_to_daddr(&rtg->rtg_group, 0);
	*is_seq = bdev_zone_is_seq(mp->m_rtdev_targp->bt_bdev, *sector);
	if (!*is_seq)
		*sector += XFS_FSB_TO_BB(mp, rgbno);
	return XFS_FSB_TO_B(mp, count_fsb);
}

void
xfs_mark_rtg_boundary(
	struct iomap_ioend	*ioend)
{
	struct xfs_mount	*mp = XFS_I(ioend->io_inode)->i_mount;
	sector_t		sector = ioend->io_bio.bi_iter.bi_sector;

	if (xfs_rtb_to_rgbno(mp, xfs_daddr_to_rtb(mp, sector)) == 0)
		ioend->io_flags |= IOMAP_IOEND_BOUNDARY;
}

static void
xfs_submit_zoned_bio(
	struct iomap_ioend	*ioend,
	struct xfs_open_zone	*oz,
	bool			is_seq)
{
	ioend->io_bio.bi_iter.bi_sector = ioend->io_sector;
	ioend->io_private = oz;
	atomic_inc(&oz->oz_ref); /* for xfs_zoned_end_io */

	if (is_seq) {
		ioend->io_bio.bi_opf &= ~REQ_OP_WRITE;
		ioend->io_bio.bi_opf |= REQ_OP_ZONE_APPEND;
	} else {
		xfs_mark_rtg_boundary(ioend);
	}

	submit_bio(&ioend->io_bio);
}

void
xfs_zone_alloc_and_submit(
	struct iomap_ioend	*ioend,
	struct xfs_open_zone	**oz)
{
	struct xfs_inode	*ip = XFS_I(ioend->io_inode);
	struct xfs_mount	*mp = ip->i_mount;
	enum rw_hint		write_hint = xfs_inode_write_hint(ip);
	bool			pack_tight = xfs_zoned_pack_tight(ip);
	unsigned int		alloc_len;
	struct iomap_ioend	*split;
	bool			is_seq;

	if (xfs_is_shutdown(mp))
		goto out_error;

	/*
	 * If we don't have a cached zone in this write context, see if the
	 * last extent before the one we are writing to points to an active
	 * zone.  If so, just continue writing to it.
	 */
	if (!*oz && ioend->io_offset)
		*oz = xfs_last_used_zone(ioend);
	if (!*oz) {
select_zone:
		*oz = xfs_select_zone(mp, write_hint, pack_tight);
		if (!*oz)
			goto out_error;
	}

	alloc_len = xfs_zone_alloc_blocks(*oz, XFS_B_TO_FSB(mp, ioend->io_size),
			&ioend->io_sector, &is_seq);
	if (!alloc_len) {
		xfs_open_zone_put(*oz);
		goto select_zone;
	}

	while ((split = iomap_split_ioend(ioend, alloc_len, is_seq))) {
		if (IS_ERR(split))
			goto out_split_error;
		alloc_len -= split->io_bio.bi_iter.bi_size;
		xfs_submit_zoned_bio(split, *oz, is_seq);
		if (!alloc_len) {
			xfs_open_zone_put(*oz);
			goto select_zone;
		}
	}

	xfs_submit_zoned_bio(ioend, *oz, is_seq);
	return;

out_split_error:
	ioend->io_bio.bi_status = errno_to_blk_status(PTR_ERR(split));
out_error:
	bio_io_error(&ioend->io_bio);
}

/*
 * Wake up all threads waiting for a zoned space allocation when the file system
 * is shut down.
 */
void
xfs_zoned_wake_all(
	struct xfs_mount	*mp)
{
	/*
	 * Don't wake up if there is no m_zone_info.  This is complicated by the
	 * fact that unmount can't atomically clear m_zone_info and thus we need
	 * to check SB_ACTIVE for that, but mount temporarily enables SB_ACTIVE
	 * during log recovery so we can't entirely rely on that either.
	 */
	if ((mp->m_super->s_flags & SB_ACTIVE) && mp->m_zone_info)
		wake_up_all(&mp->m_zone_info->zi_zone_wait);
}

/*
 * Check if @rgbno in @rgb is a potentially valid block.  It might still be
 * unused, but that information is only found in the rmap.
 */
bool
xfs_zone_rgbno_is_valid(
	struct xfs_rtgroup	*rtg,
	xfs_rgnumber_t		rgbno)
{
	lockdep_assert_held(&rtg_rmap(rtg)->i_lock);

	if (rtg->rtg_open_zone)
		return rgbno < rtg->rtg_open_zone->oz_write_pointer;
	return !xa_get_mark(&rtg_mount(rtg)->m_groups[XG_TYPE_RTG].xa,
			rtg_rgno(rtg), XFS_RTG_FREE);
}

static void
xfs_free_open_zones(
	struct xfs_zone_info	*zi)
{
	struct xfs_open_zone	*oz;

	spin_lock(&zi->zi_open_zones_lock);
	while ((oz = list_first_entry_or_null(&zi->zi_open_zones,
			struct xfs_open_zone, oz_entry))) {
		list_del(&oz->oz_entry);
		xfs_open_zone_put(oz);
	}
	spin_unlock(&zi->zi_open_zones_lock);
}

struct xfs_init_zones {
	struct xfs_mount	*mp;
	uint64_t		available;
	uint64_t		reclaimable;
};

static int
xfs_init_zone(
	struct xfs_init_zones	*iz,
	struct xfs_rtgroup	*rtg,
	struct blk_zone		*zone)
{
	struct xfs_mount	*mp = rtg_mount(rtg);
	struct xfs_zone_info	*zi = mp->m_zone_info;
	uint64_t		used = rtg_rmap(rtg)->i_used_blocks;
	xfs_rgblock_t		write_pointer, highest_rgbno;
	int			error;

	if (zone && !xfs_zone_validate(zone, rtg, &write_pointer))
		return -EFSCORRUPTED;

	/*
	 * For sequential write required zones we retrieved the hardware write
	 * pointer above.
	 *
	 * For conventional zones or conventional devices we don't have that
	 * luxury.  Instead query the rmap to find the highest recorded block
	 * and set the write pointer to the block after that.  In case of a
	 * power loss this misses blocks where the data I/O has completed but
	 * not recorded in the rmap yet, and it also rewrites blocks if the most
	 * recently written ones got deleted again before unmount, but this is
	 * the best we can do without hardware support.
	 */
	if (!zone || zone->cond == BLK_ZONE_COND_NOT_WP) {
		xfs_rtgroup_lock(rtg, XFS_RTGLOCK_RMAP);
		highest_rgbno = xfs_rtrmap_highest_rgbno(rtg);
		if (highest_rgbno == NULLRGBLOCK)
			write_pointer = 0;
		else
			write_pointer = highest_rgbno + 1;
		xfs_rtgroup_unlock(rtg, XFS_RTGLOCK_RMAP);
	}

	/*
	 * If there are no used blocks, but the zone is not in empty state yet
	 * we lost power before the zoned reset.  In that case finish the work
	 * here.
	 */
	if (write_pointer == rtg_blocks(rtg) && used == 0) {
		error = xfs_zone_gc_reset_sync(rtg);
		if (error)
			return error;
		write_pointer = 0;
	}

	if (write_pointer == 0) {
		/* zone is empty */
		atomic_inc(&zi->zi_nr_free_zones);
		xfs_group_set_mark(&rtg->rtg_group, XFS_RTG_FREE);
		iz->available += rtg_blocks(rtg);
	} else if (write_pointer < rtg_blocks(rtg)) {
		/* zone is open */
		struct xfs_open_zone *oz;

		atomic_inc(&rtg_group(rtg)->xg_active_ref);
		oz = xfs_init_open_zone(rtg, write_pointer, WRITE_LIFE_NOT_SET,
				false);
		list_add_tail(&oz->oz_entry, &zi->zi_open_zones);
		zi->zi_nr_open_zones++;

		iz->available += (rtg_blocks(rtg) - write_pointer);
		iz->reclaimable += write_pointer - used;
	} else if (used < rtg_blocks(rtg)) {
		/* zone fully written, but has freed blocks */
		xfs_zone_account_reclaimable(rtg, rtg_blocks(rtg) - used);
		iz->reclaimable += (rtg_blocks(rtg) - used);
	}

	return 0;
}

static int
xfs_get_zone_info_cb(
	struct blk_zone		*zone,
	unsigned int		idx,
	void			*data)
{
	struct xfs_init_zones	*iz = data;
	struct xfs_mount	*mp = iz->mp;
	xfs_fsblock_t		zsbno = xfs_daddr_to_rtb(mp, zone->start);
	xfs_rgnumber_t		rgno;
	struct xfs_rtgroup	*rtg;
	int			error;

	if (xfs_rtb_to_rgbno(mp, zsbno) != 0) {
		xfs_warn(mp, "mismatched zone start 0x%llx.", zsbno);
		return -EFSCORRUPTED;
	}

	rgno = xfs_rtb_to_rgno(mp, zsbno);
	rtg = xfs_rtgroup_grab(mp, rgno);
	if (!rtg) {
		xfs_warn(mp, "realtime group not found for zone %u.", rgno);
		return -EFSCORRUPTED;
	}
	error = xfs_init_zone(iz, rtg, zone);
	xfs_rtgroup_rele(rtg);
	return error;
}

/*
 * Calculate the max open zone limit based on the of number of
 * backing zones available
 */
static inline uint32_t
xfs_max_open_zones(
	struct xfs_mount	*mp)
{
	unsigned int		max_open, max_open_data_zones;
	/*
	 * We need two zones for every open data zone,
	 * one in reserve as we don't reclaim open zones. One data zone
	 * and its spare is included in XFS_MIN_ZONES.
	 */
	max_open_data_zones = (mp->m_sb.sb_rgcount - XFS_MIN_ZONES) / 2 + 1;
	max_open = max_open_data_zones + XFS_OPEN_GC_ZONES;

	/*
	 * Cap the max open limit to 1/4 of available space
	 */
	max_open = min(max_open, mp->m_sb.sb_rgcount / 4);

	return max(XFS_MIN_OPEN_ZONES, max_open);
}

/*
 * Normally we use the open zone limit that the device reports.  If there is
 * none let the user pick one from the command line.
 *
 * If the device doesn't report an open zone limit and there is no override,
 * allow to hold about a quarter of the zones open.  In theory we could allow
 * all to be open, but at that point we run into GC deadlocks because we can't
 * reclaim open zones.
 *
 * When used on conventional SSDs a lower open limit is advisable as we'll
 * otherwise overwhelm the FTL just as much as a conventional block allocator.
 *
 * Note: To debug the open zone management code, force max_open to 1 here.
 */
static int
xfs_calc_open_zones(
	struct xfs_mount	*mp)
{
	struct block_device	*bdev = mp->m_rtdev_targp->bt_bdev;
	unsigned int		bdev_open_zones = bdev_max_open_zones(bdev);

	if (!mp->m_max_open_zones) {
		if (bdev_open_zones)
			mp->m_max_open_zones = bdev_open_zones;
		else
			mp->m_max_open_zones = xfs_max_open_zones(mp);
	}

	if (mp->m_max_open_zones < XFS_MIN_OPEN_ZONES) {
		xfs_notice(mp, "need at least %u open zones.",
			XFS_MIN_OPEN_ZONES);
		return -EIO;
	}

	if (bdev_open_zones && bdev_open_zones < mp->m_max_open_zones) {
		mp->m_max_open_zones = bdev_open_zones;
		xfs_info(mp, "limiting open zones to %u due to hardware limit.\n",
			bdev_open_zones);
	}

	if (mp->m_max_open_zones > xfs_max_open_zones(mp)) {
		mp->m_max_open_zones = xfs_max_open_zones(mp);
		xfs_info(mp,
"limiting open zones to %u due to total zone count (%u)",
			mp->m_max_open_zones, mp->m_sb.sb_rgcount);
	}

	return 0;
}

static unsigned long *
xfs_alloc_bucket_bitmap(
	struct xfs_mount	*mp)
{
	return kvmalloc_array(BITS_TO_LONGS(mp->m_sb.sb_rgcount),
			sizeof(unsigned long), GFP_KERNEL | __GFP_ZERO);
}

static struct xfs_zone_info *
xfs_alloc_zone_info(
	struct xfs_mount	*mp)
{
	struct xfs_zone_info	*zi;
	int			i;

	zi = kzalloc(sizeof(*zi), GFP_KERNEL);
	if (!zi)
		return NULL;
	INIT_LIST_HEAD(&zi->zi_open_zones);
	INIT_LIST_HEAD(&zi->zi_reclaim_reservations);
	spin_lock_init(&zi->zi_reset_list_lock);
	spin_lock_init(&zi->zi_open_zones_lock);
	spin_lock_init(&zi->zi_reservation_lock);
	init_waitqueue_head(&zi->zi_zone_wait);
	spin_lock_init(&zi->zi_used_buckets_lock);
	for (i = 0; i < XFS_ZONE_USED_BUCKETS; i++) {
		zi->zi_used_bucket_bitmap[i] = xfs_alloc_bucket_bitmap(mp);
		if (!zi->zi_used_bucket_bitmap[i])
			goto out_free_bitmaps;
	}
	return zi;

out_free_bitmaps:
	while (--i > 0)
		kvfree(zi->zi_used_bucket_bitmap[i]);
	kfree(zi);
	return NULL;
}

static void
xfs_free_zone_info(
	struct xfs_zone_info	*zi)
{
	int			i;

	xfs_free_open_zones(zi);
	for (i = 0; i < XFS_ZONE_USED_BUCKETS; i++)
		kvfree(zi->zi_used_bucket_bitmap[i]);
	kfree(zi);
}

int
xfs_mount_zones(
	struct xfs_mount	*mp)
{
	struct xfs_init_zones	iz = {
		.mp		= mp,
	};
	struct xfs_buftarg	*bt = mp->m_rtdev_targp;
	int			error;

	if (!bt) {
		xfs_notice(mp, "RT device missing.");
		return -EINVAL;
	}

	if (!xfs_has_rtgroups(mp) || !xfs_has_rmapbt(mp)) {
		xfs_notice(mp, "invalid flag combination.");
		return -EFSCORRUPTED;
	}
	if (mp->m_sb.sb_rextsize != 1) {
		xfs_notice(mp, "zoned file systems do not support rextsize.");
		return -EFSCORRUPTED;
	}
	if (mp->m_sb.sb_rgcount < XFS_MIN_ZONES) {
		xfs_notice(mp,
"zoned file systems need to have at least %u zones.", XFS_MIN_ZONES);
		return -EFSCORRUPTED;
	}

	error = xfs_calc_open_zones(mp);
	if (error)
		return error;

	mp->m_zone_info = xfs_alloc_zone_info(mp);
	if (!mp->m_zone_info)
		return -ENOMEM;

	xfs_info(mp, "%u zones of %u blocks size (%u max open)",
		 mp->m_sb.sb_rgcount, mp->m_groups[XG_TYPE_RTG].blocks,
		 mp->m_max_open_zones);
	trace_xfs_zones_mount(mp);

	if (bdev_is_zoned(bt->bt_bdev)) {
		error = blkdev_report_zones(bt->bt_bdev,
				XFS_FSB_TO_BB(mp, mp->m_sb.sb_rtstart),
				mp->m_sb.sb_rgcount, xfs_get_zone_info_cb, &iz);
		if (error < 0)
			goto out_free_zone_info;
	} else {
		struct xfs_rtgroup	*rtg = NULL;

		while ((rtg = xfs_rtgroup_next(mp, rtg))) {
			error = xfs_init_zone(&iz, rtg, NULL);
			if (error)
				goto out_free_zone_info;
		}
	}

	xfs_set_freecounter(mp, XC_FREE_RTAVAILABLE, iz.available);
	xfs_set_freecounter(mp, XC_FREE_RTEXTENTS,
			iz.available + iz.reclaimable);

	error = xfs_zone_gc_mount(mp);
	if (error)
		goto out_free_zone_info;
	return 0;

out_free_zone_info:
	xfs_free_zone_info(mp->m_zone_info);
	return error;
}

void
xfs_unmount_zones(
	struct xfs_mount	*mp)
{
	xfs_zone_gc_unmount(mp);
	xfs_free_zone_info(mp->m_zone_info);
}
