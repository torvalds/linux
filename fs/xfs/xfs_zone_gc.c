// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023-2025 Christoph Hellwig.
 * Copyright (c) 2024-2025, Western Digital Corporation or its affiliates.
 */
#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_trans.h"
#include "xfs_icache.h"
#include "xfs_rmap.h"
#include "xfs_rtbitmap.h"
#include "xfs_rtrmap_btree.h"
#include "xfs_zone_alloc.h"
#include "xfs_zone_priv.h"
#include "xfs_zones.h"
#include "xfs_trace.h"

/*
 * Implement Garbage Collection (GC) of partially used zoned.
 *
 * To support the purely sequential writes in each zone, zoned XFS needs to be
 * able to move data remaining in a zone out of it to reset the zone to prepare
 * for writing to it again.
 *
 * This is done by the GC thread implemented in this file.  To support that a
 * number of zones (XFS_GC_ZONES) is reserved from the user visible capacity to
 * write the garbage collected data into.
 *
 * Whenever the available space is below the chosen threshold, the GC thread
 * looks for potential non-empty but not fully used zones that are worth
 * reclaiming.  Once found the rmap for the victim zone is queried, and after
 * a bit of sorting to reduce fragmentation, the still live extents are read
 * into memory and written to the GC target zone, and the bmap btree of the
 * files is updated to point to the new location.  To avoid taking the IOLOCK
 * and MMAPLOCK for the entire GC process and thus affecting the latency of
 * user reads and writes to the files, the GC writes are speculative and the
 * I/O completion checks that no other writes happened for the affected regions
 * before remapping.
 *
 * Once a zone does not contain any valid data, be that through GC or user
 * block removal, it is queued for for a zone reset.  The reset operation
 * carefully ensures that the RT device cache is flushed and all transactions
 * referencing the rmap have been committed to disk.
 */

/*
 * Size of each GC scratch pad.  This is also the upper bound for each
 * GC I/O, which helps to keep latency down.
 */
#define XFS_GC_CHUNK_SIZE	SZ_1M

/*
 * Scratchpad data to read GCed data into.
 *
 * The offset member tracks where the next allocation starts, and freed tracks
 * the amount of space that is not used anymore.
 */
#define XFS_ZONE_GC_NR_SCRATCH	2
struct xfs_zone_scratch {
	struct folio			*folio;
	unsigned int			offset;
	unsigned int			freed;
};

/*
 * Chunk that is read and written for each GC operation.
 *
 * Note that for writes to actual zoned devices, the chunk can be split when
 * reaching the hardware limit.
 */
struct xfs_gc_bio {
	struct xfs_zone_gc_data		*data;

	/*
	 * Entry into the reading/writing/resetting list.  Only accessed from
	 * the GC thread, so no locking needed.
	 */
	struct list_head		entry;

	/*
	 * State of this gc_bio.  Done means the current I/O completed.
	 * Set from the bio end I/O handler, read from the GC thread.
	 */
	enum {
		XFS_GC_BIO_NEW,
		XFS_GC_BIO_DONE,
	} state;

	/*
	 * Pointer to the inode and byte range in the inode that this
	 * GC chunk is operating on.
	 */
	struct xfs_inode		*ip;
	loff_t				offset;
	unsigned int			len;

	/*
	 * Existing startblock (in the zone to be freed) and newly assigned
	 * daddr in the zone GCed into.
	 */
	xfs_fsblock_t			old_startblock;
	xfs_daddr_t			new_daddr;
	struct xfs_zone_scratch		*scratch;

	/* Are we writing to a sequential write required zone? */
	bool				is_seq;

	/* Open Zone being written to */
	struct xfs_open_zone		*oz;

	/* Bio used for reads and writes, including the bvec used by it */
	struct bio_vec			bv;
	struct bio			bio;	/* must be last */
};

#define XFS_ZONE_GC_RECS		1024

/* iterator, needs to be reinitialized for each victim zone */
struct xfs_zone_gc_iter {
	struct xfs_rtgroup		*victim_rtg;
	unsigned int			rec_count;
	unsigned int			rec_idx;
	xfs_agblock_t			next_startblock;
	struct xfs_rmap_irec		*recs;
};

/*
 * Per-mount GC state.
 */
struct xfs_zone_gc_data {
	struct xfs_mount		*mp;

	/* bioset used to allocate the gc_bios */
	struct bio_set			bio_set;

	/*
	 * Scratchpad used, and index to indicated which one is used.
	 */
	struct xfs_zone_scratch		scratch[XFS_ZONE_GC_NR_SCRATCH];
	unsigned int			scratch_idx;

	/*
	 * List of bios currently being read, written and reset.
	 * These lists are only accessed by the GC thread itself, and must only
	 * be processed in order.
	 */
	struct list_head		reading;
	struct list_head		writing;
	struct list_head		resetting;

	/*
	 * Iterator for the victim zone.
	 */
	struct xfs_zone_gc_iter		iter;
};

/*
 * We aim to keep enough zones free in stock to fully use the open zone limit
 * for data placement purposes. Additionally, the m_zonegc_low_space tunable
 * can be set to make sure a fraction of the unused blocks are available for
 * writing.
 */
bool
xfs_zoned_need_gc(
	struct xfs_mount	*mp)
{
	s64			available, free, threshold;
	s32			remainder;

	if (!xfs_group_marked(mp, XG_TYPE_RTG, XFS_RTG_RECLAIMABLE))
		return false;

	available = xfs_estimate_freecounter(mp, XC_FREE_RTAVAILABLE);

	if (available <
	    mp->m_groups[XG_TYPE_RTG].blocks *
	    (mp->m_max_open_zones - XFS_OPEN_GC_ZONES))
		return true;

	free = xfs_estimate_freecounter(mp, XC_FREE_RTEXTENTS);

	threshold = div_s64_rem(free, 100, &remainder);
	threshold = threshold * mp->m_zonegc_low_space +
		    remainder * div_s64(mp->m_zonegc_low_space, 100);

	if (available < threshold)
		return true;

	return false;
}

static struct xfs_zone_gc_data *
xfs_zone_gc_data_alloc(
	struct xfs_mount	*mp)
{
	struct xfs_zone_gc_data	*data;
	int			i;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;
	data->iter.recs = kcalloc(XFS_ZONE_GC_RECS, sizeof(*data->iter.recs),
			GFP_KERNEL);
	if (!data->iter.recs)
		goto out_free_data;

	/*
	 * We actually only need a single bio_vec.  It would be nice to have
	 * a flag that only allocates the inline bvecs and not the separate
	 * bvec pool.
	 */
	if (bioset_init(&data->bio_set, 16, offsetof(struct xfs_gc_bio, bio),
			BIOSET_NEED_BVECS))
		goto out_free_recs;
	for (i = 0; i < XFS_ZONE_GC_NR_SCRATCH; i++) {
		data->scratch[i].folio =
			folio_alloc(GFP_KERNEL, get_order(XFS_GC_CHUNK_SIZE));
		if (!data->scratch[i].folio)
			goto out_free_scratch;
	}
	INIT_LIST_HEAD(&data->reading);
	INIT_LIST_HEAD(&data->writing);
	INIT_LIST_HEAD(&data->resetting);
	data->mp = mp;
	return data;

out_free_scratch:
	while (--i >= 0)
		folio_put(data->scratch[i].folio);
	bioset_exit(&data->bio_set);
out_free_recs:
	kfree(data->iter.recs);
out_free_data:
	kfree(data);
	return NULL;
}

static void
xfs_zone_gc_data_free(
	struct xfs_zone_gc_data	*data)
{
	int			i;

	for (i = 0; i < XFS_ZONE_GC_NR_SCRATCH; i++)
		folio_put(data->scratch[i].folio);
	bioset_exit(&data->bio_set);
	kfree(data->iter.recs);
	kfree(data);
}

static void
xfs_zone_gc_iter_init(
	struct xfs_zone_gc_iter	*iter,
	struct xfs_rtgroup	*victim_rtg)

{
	iter->next_startblock = 0;
	iter->rec_count = 0;
	iter->rec_idx = 0;
	iter->victim_rtg = victim_rtg;
}

/*
 * Query the rmap of the victim zone to gather the records to evacuate.
 */
static int
xfs_zone_gc_query_cb(
	struct xfs_btree_cur	*cur,
	const struct xfs_rmap_irec *irec,
	void			*private)
{
	struct xfs_zone_gc_iter	*iter = private;

	ASSERT(!XFS_RMAP_NON_INODE_OWNER(irec->rm_owner));
	ASSERT(!xfs_is_sb_inum(cur->bc_mp, irec->rm_owner));
	ASSERT(!(irec->rm_flags & (XFS_RMAP_ATTR_FORK | XFS_RMAP_BMBT_BLOCK)));

	iter->recs[iter->rec_count] = *irec;
	if (++iter->rec_count == XFS_ZONE_GC_RECS) {
		iter->next_startblock =
			irec->rm_startblock + irec->rm_blockcount;
		return 1;
	}
	return 0;
}

static int
xfs_zone_gc_rmap_rec_cmp(
	const void			*a,
	const void			*b)
{
	const struct xfs_rmap_irec	*reca = a;
	const struct xfs_rmap_irec	*recb = b;
	int				diff;

	diff = cmp_int(reca->rm_owner, recb->rm_owner);
	if (diff)
		return diff;
	return cmp_int(reca->rm_offset, recb->rm_offset);
}

static int
xfs_zone_gc_query(
	struct xfs_mount	*mp,
	struct xfs_zone_gc_iter	*iter)
{
	struct xfs_rtgroup	*rtg = iter->victim_rtg;
	struct xfs_rmap_irec	ri_low = { };
	struct xfs_rmap_irec	ri_high;
	struct xfs_btree_cur	*cur;
	struct xfs_trans	*tp;
	int			error;

	ASSERT(iter->next_startblock <= rtg_blocks(rtg));
	if (iter->next_startblock == rtg_blocks(rtg))
		goto done;

	ASSERT(iter->next_startblock < rtg_blocks(rtg));
	ri_low.rm_startblock = iter->next_startblock;
	memset(&ri_high, 0xFF, sizeof(ri_high));

	iter->rec_idx = 0;
	iter->rec_count = 0;

	tp = xfs_trans_alloc_empty(mp);
	xfs_rtgroup_lock(rtg, XFS_RTGLOCK_RMAP);
	cur = xfs_rtrmapbt_init_cursor(tp, rtg);
	error = xfs_rmap_query_range(cur, &ri_low, &ri_high,
			xfs_zone_gc_query_cb, iter);
	xfs_rtgroup_unlock(rtg, XFS_RTGLOCK_RMAP);
	xfs_btree_del_cursor(cur, error < 0 ? error : 0);
	xfs_trans_cancel(tp);

	if (error < 0)
		return error;

	/*
	 * Sort the rmap records by inode number and increasing offset to
	 * defragment the mappings.
	 *
	 * This could be further enhanced by an even bigger look ahead window,
	 * but that's better left until we have better detection of changes to
	 * inode mapping to avoid the potential of GCing already dead data.
	 */
	sort(iter->recs, iter->rec_count, sizeof(iter->recs[0]),
			xfs_zone_gc_rmap_rec_cmp, NULL);

	if (error == 0) {
		/*
		 * We finished iterating through the zone.
		 */
		iter->next_startblock = rtg_blocks(rtg);
		if (iter->rec_count == 0)
			goto done;
	}

	return 0;
done:
	xfs_rtgroup_rele(iter->victim_rtg);
	iter->victim_rtg = NULL;
	return 0;
}

static bool
xfs_zone_gc_iter_next(
	struct xfs_mount	*mp,
	struct xfs_zone_gc_iter	*iter,
	struct xfs_rmap_irec	*chunk_rec,
	struct xfs_inode	**ipp)
{
	struct xfs_rmap_irec	*irec;
	int			error;

	if (!iter->victim_rtg)
		return false;

retry:
	if (iter->rec_idx == iter->rec_count) {
		error = xfs_zone_gc_query(mp, iter);
		if (error)
			goto fail;
		if (!iter->victim_rtg)
			return false;
	}

	irec = &iter->recs[iter->rec_idx];
	error = xfs_iget(mp, NULL, irec->rm_owner,
			XFS_IGET_UNTRUSTED | XFS_IGET_DONTCACHE, 0, ipp);
	if (error) {
		/*
		 * If the inode was already deleted, skip over it.
		 */
		if (error == -ENOENT) {
			iter->rec_idx++;
			goto retry;
		}
		goto fail;
	}

	if (!S_ISREG(VFS_I(*ipp)->i_mode) || !XFS_IS_REALTIME_INODE(*ipp)) {
		iter->rec_idx++;
		xfs_irele(*ipp);
		goto retry;
	}

	*chunk_rec = *irec;
	return true;

fail:
	xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
	return false;
}

static void
xfs_zone_gc_iter_advance(
	struct xfs_zone_gc_iter	*iter,
	xfs_extlen_t		count_fsb)
{
	struct xfs_rmap_irec	*irec = &iter->recs[iter->rec_idx];

	irec->rm_offset += count_fsb;
	irec->rm_startblock += count_fsb;
	irec->rm_blockcount -= count_fsb;
	if (!irec->rm_blockcount)
		iter->rec_idx++;
}

static struct xfs_rtgroup *
xfs_zone_gc_pick_victim_from(
	struct xfs_mount	*mp,
	uint32_t		bucket)
{
	struct xfs_zone_info	*zi = mp->m_zone_info;
	uint32_t		victim_used = U32_MAX;
	struct xfs_rtgroup	*victim_rtg = NULL;
	uint32_t		bit;

	if (!zi->zi_used_bucket_entries[bucket])
		return NULL;

	for_each_set_bit(bit, zi->zi_used_bucket_bitmap[bucket],
			mp->m_sb.sb_rgcount) {
		struct xfs_rtgroup *rtg = xfs_rtgroup_grab(mp, bit);

		if (!rtg)
			continue;

		/* skip zones that are just waiting for a reset */
		if (rtg_rmap(rtg)->i_used_blocks == 0 ||
		    rtg_rmap(rtg)->i_used_blocks >= victim_used) {
			xfs_rtgroup_rele(rtg);
			continue;
		}

		if (victim_rtg)
			xfs_rtgroup_rele(victim_rtg);
		victim_rtg = rtg;
		victim_used = rtg_rmap(rtg)->i_used_blocks;

		/*
		 * Any zone that is less than 1 percent used is fair game for
		 * instant reclaim. All of these zones are in the last
		 * bucket, so avoid the expensive division for the zones
		 * in the other buckets.
		 */
		if (bucket == 0 &&
		    rtg_rmap(rtg)->i_used_blocks < rtg_blocks(rtg) / 100)
			break;
	}

	return victim_rtg;
}

/*
 * Iterate through all zones marked as reclaimable and find a candidate to
 * reclaim.
 */
static bool
xfs_zone_gc_select_victim(
	struct xfs_zone_gc_data	*data)
{
	struct xfs_zone_gc_iter	*iter = &data->iter;
	struct xfs_mount	*mp = data->mp;
	struct xfs_zone_info	*zi = mp->m_zone_info;
	struct xfs_rtgroup	*victim_rtg = NULL;
	unsigned int		bucket;

	if (xfs_is_shutdown(mp))
		return false;

	if (iter->victim_rtg)
		return true;

	/*
	 * Don't start new work if we are asked to stop or park.
	 */
	if (kthread_should_stop() || kthread_should_park())
		return false;

	if (!xfs_zoned_need_gc(mp))
		return false;

	spin_lock(&zi->zi_used_buckets_lock);
	for (bucket = 0; bucket < XFS_ZONE_USED_BUCKETS; bucket++) {
		victim_rtg = xfs_zone_gc_pick_victim_from(mp, bucket);
		if (victim_rtg)
			break;
	}
	spin_unlock(&zi->zi_used_buckets_lock);

	if (!victim_rtg)
		return false;

	trace_xfs_zone_gc_select_victim(victim_rtg, bucket);
	xfs_zone_gc_iter_init(iter, victim_rtg);
	return true;
}

static struct xfs_open_zone *
xfs_zone_gc_steal_open(
	struct xfs_zone_info	*zi)
{
	struct xfs_open_zone	*oz, *found = NULL;

	spin_lock(&zi->zi_open_zones_lock);
	list_for_each_entry(oz, &zi->zi_open_zones, oz_entry) {
		if (!found || oz->oz_allocated < found->oz_allocated)
			found = oz;
	}

	if (found) {
		found->oz_is_gc = true;
		list_del_init(&found->oz_entry);
		zi->zi_nr_open_zones--;
	}

	spin_unlock(&zi->zi_open_zones_lock);
	return found;
}

static struct xfs_open_zone *
xfs_zone_gc_select_target(
	struct xfs_mount	*mp)
{
	struct xfs_zone_info	*zi = mp->m_zone_info;
	struct xfs_open_zone	*oz = zi->zi_open_gc_zone;

	/*
	 * We need to wait for pending writes to finish.
	 */
	if (oz && oz->oz_written < rtg_blocks(oz->oz_rtg))
		return NULL;

	ASSERT(zi->zi_nr_open_zones <=
		mp->m_max_open_zones - XFS_OPEN_GC_ZONES);
	oz = xfs_open_zone(mp, WRITE_LIFE_NOT_SET, true);
	if (oz)
		trace_xfs_zone_gc_target_opened(oz->oz_rtg);
	spin_lock(&zi->zi_open_zones_lock);
	zi->zi_open_gc_zone = oz;
	spin_unlock(&zi->zi_open_zones_lock);
	return oz;
}

/*
 * Ensure we have a valid open zone to write the GC data to.
 *
 * If the current target zone has space keep writing to it, else first wait for
 * all pending writes and then pick a new one.
 */
static struct xfs_open_zone *
xfs_zone_gc_ensure_target(
	struct xfs_mount	*mp)
{
	struct xfs_open_zone	*oz = mp->m_zone_info->zi_open_gc_zone;

	if (!oz || oz->oz_allocated == rtg_blocks(oz->oz_rtg))
		return xfs_zone_gc_select_target(mp);
	return oz;
}

static unsigned int
xfs_zone_gc_scratch_available(
	struct xfs_zone_gc_data	*data)
{
	return XFS_GC_CHUNK_SIZE - data->scratch[data->scratch_idx].offset;
}

static bool
xfs_zone_gc_space_available(
	struct xfs_zone_gc_data	*data)
{
	struct xfs_open_zone	*oz;

	oz = xfs_zone_gc_ensure_target(data->mp);
	if (!oz)
		return false;
	return oz->oz_allocated < rtg_blocks(oz->oz_rtg) &&
		xfs_zone_gc_scratch_available(data);
}

static void
xfs_zone_gc_end_io(
	struct bio		*bio)
{
	struct xfs_gc_bio	*chunk =
		container_of(bio, struct xfs_gc_bio, bio);
	struct xfs_zone_gc_data	*data = chunk->data;

	WRITE_ONCE(chunk->state, XFS_GC_BIO_DONE);
	wake_up_process(data->mp->m_zone_info->zi_gc_thread);
}

static struct xfs_open_zone *
xfs_zone_gc_alloc_blocks(
	struct xfs_zone_gc_data	*data,
	xfs_extlen_t		*count_fsb,
	xfs_daddr_t		*daddr,
	bool			*is_seq)
{
	struct xfs_mount	*mp = data->mp;
	struct xfs_open_zone	*oz;

	oz = xfs_zone_gc_ensure_target(mp);
	if (!oz)
		return NULL;

	*count_fsb = min(*count_fsb,
		XFS_B_TO_FSB(mp, xfs_zone_gc_scratch_available(data)));

	/*
	 * Directly allocate GC blocks from the reserved pool.
	 *
	 * If we'd take them from the normal pool we could be stealing blocks
	 * from a regular writer, which would then have to wait for GC and
	 * deadlock.
	 */
	spin_lock(&mp->m_sb_lock);
	*count_fsb = min(*count_fsb,
			rtg_blocks(oz->oz_rtg) - oz->oz_allocated);
	*count_fsb = min3(*count_fsb,
			mp->m_free[XC_FREE_RTEXTENTS].res_avail,
			mp->m_free[XC_FREE_RTAVAILABLE].res_avail);
	mp->m_free[XC_FREE_RTEXTENTS].res_avail -= *count_fsb;
	mp->m_free[XC_FREE_RTAVAILABLE].res_avail -= *count_fsb;
	spin_unlock(&mp->m_sb_lock);

	if (!*count_fsb)
		return NULL;

	*daddr = xfs_gbno_to_daddr(&oz->oz_rtg->rtg_group, 0);
	*is_seq = bdev_zone_is_seq(mp->m_rtdev_targp->bt_bdev, *daddr);
	if (!*is_seq)
		*daddr += XFS_FSB_TO_BB(mp, oz->oz_allocated);
	oz->oz_allocated += *count_fsb;
	atomic_inc(&oz->oz_ref);
	return oz;
}

static bool
xfs_zone_gc_start_chunk(
	struct xfs_zone_gc_data	*data)
{
	struct xfs_zone_gc_iter	*iter = &data->iter;
	struct xfs_mount	*mp = data->mp;
	struct block_device	*bdev = mp->m_rtdev_targp->bt_bdev;
	struct xfs_open_zone	*oz;
	struct xfs_rmap_irec	irec;
	struct xfs_gc_bio	*chunk;
	struct xfs_inode	*ip;
	struct bio		*bio;
	xfs_daddr_t		daddr;
	bool			is_seq;

	if (xfs_is_shutdown(mp))
		return false;

	if (!xfs_zone_gc_iter_next(mp, iter, &irec, &ip))
		return false;
	oz = xfs_zone_gc_alloc_blocks(data, &irec.rm_blockcount, &daddr,
			&is_seq);
	if (!oz) {
		xfs_irele(ip);
		return false;
	}

	bio = bio_alloc_bioset(bdev, 1, REQ_OP_READ, GFP_NOFS, &data->bio_set);

	chunk = container_of(bio, struct xfs_gc_bio, bio);
	chunk->ip = ip;
	chunk->offset = XFS_FSB_TO_B(mp, irec.rm_offset);
	chunk->len = XFS_FSB_TO_B(mp, irec.rm_blockcount);
	chunk->old_startblock =
		xfs_rgbno_to_rtb(iter->victim_rtg, irec.rm_startblock);
	chunk->new_daddr = daddr;
	chunk->is_seq = is_seq;
	chunk->scratch = &data->scratch[data->scratch_idx];
	chunk->data = data;
	chunk->oz = oz;

	bio->bi_iter.bi_sector = xfs_rtb_to_daddr(mp, chunk->old_startblock);
	bio->bi_end_io = xfs_zone_gc_end_io;
	bio_add_folio_nofail(bio, chunk->scratch->folio, chunk->len,
			chunk->scratch->offset);
	chunk->scratch->offset += chunk->len;
	if (chunk->scratch->offset == XFS_GC_CHUNK_SIZE) {
		data->scratch_idx =
			(data->scratch_idx + 1) % XFS_ZONE_GC_NR_SCRATCH;
	}
	WRITE_ONCE(chunk->state, XFS_GC_BIO_NEW);
	list_add_tail(&chunk->entry, &data->reading);
	xfs_zone_gc_iter_advance(iter, irec.rm_blockcount);

	submit_bio(bio);
	return true;
}

static void
xfs_zone_gc_free_chunk(
	struct xfs_gc_bio	*chunk)
{
	list_del(&chunk->entry);
	xfs_open_zone_put(chunk->oz);
	xfs_irele(chunk->ip);
	bio_put(&chunk->bio);
}

static void
xfs_zone_gc_submit_write(
	struct xfs_zone_gc_data	*data,
	struct xfs_gc_bio	*chunk)
{
	if (chunk->is_seq) {
		chunk->bio.bi_opf &= ~REQ_OP_WRITE;
		chunk->bio.bi_opf |= REQ_OP_ZONE_APPEND;
	}
	chunk->bio.bi_iter.bi_sector = chunk->new_daddr;
	chunk->bio.bi_end_io = xfs_zone_gc_end_io;
	submit_bio(&chunk->bio);
}

static struct xfs_gc_bio *
xfs_zone_gc_split_write(
	struct xfs_zone_gc_data	*data,
	struct xfs_gc_bio	*chunk)
{
	struct queue_limits	*lim =
		&bdev_get_queue(chunk->bio.bi_bdev)->limits;
	struct xfs_gc_bio	*split_chunk;
	int			split_sectors;
	unsigned int		split_len;
	struct bio		*split;
	unsigned int		nsegs;

	if (!chunk->is_seq)
		return NULL;

	split_sectors = bio_split_rw_at(&chunk->bio, lim, &nsegs,
			lim->max_zone_append_sectors << SECTOR_SHIFT);
	if (!split_sectors)
		return NULL;

	/* ensure the split chunk is still block size aligned */
	split_sectors = ALIGN_DOWN(split_sectors << SECTOR_SHIFT,
			data->mp->m_sb.sb_blocksize) >> SECTOR_SHIFT;
	split_len = split_sectors << SECTOR_SHIFT;

	split = bio_split(&chunk->bio, split_sectors, GFP_NOFS, &data->bio_set);
	split_chunk = container_of(split, struct xfs_gc_bio, bio);
	split_chunk->data = data;
	ihold(VFS_I(chunk->ip));
	split_chunk->ip = chunk->ip;
	split_chunk->is_seq = chunk->is_seq;
	split_chunk->scratch = chunk->scratch;
	split_chunk->offset = chunk->offset;
	split_chunk->len = split_len;
	split_chunk->old_startblock = chunk->old_startblock;
	split_chunk->new_daddr = chunk->new_daddr;
	split_chunk->oz = chunk->oz;
	atomic_inc(&chunk->oz->oz_ref);

	chunk->offset += split_len;
	chunk->len -= split_len;
	chunk->old_startblock += XFS_B_TO_FSB(data->mp, split_len);

	/* add right before the original chunk */
	WRITE_ONCE(split_chunk->state, XFS_GC_BIO_NEW);
	list_add_tail(&split_chunk->entry, &chunk->entry);
	return split_chunk;
}

static void
xfs_zone_gc_write_chunk(
	struct xfs_gc_bio	*chunk)
{
	struct xfs_zone_gc_data	*data = chunk->data;
	struct xfs_mount	*mp = chunk->ip->i_mount;
	phys_addr_t		bvec_paddr =
		bvec_phys(bio_first_bvec_all(&chunk->bio));
	struct xfs_gc_bio	*split_chunk;

	if (chunk->bio.bi_status)
		xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
	if (xfs_is_shutdown(mp)) {
		xfs_zone_gc_free_chunk(chunk);
		return;
	}

	WRITE_ONCE(chunk->state, XFS_GC_BIO_NEW);
	list_move_tail(&chunk->entry, &data->writing);

	bio_reset(&chunk->bio, mp->m_rtdev_targp->bt_bdev, REQ_OP_WRITE);
	bio_add_folio_nofail(&chunk->bio, chunk->scratch->folio, chunk->len,
			offset_in_folio(chunk->scratch->folio, bvec_paddr));

	while ((split_chunk = xfs_zone_gc_split_write(data, chunk)))
		xfs_zone_gc_submit_write(data, split_chunk);
	xfs_zone_gc_submit_write(data, chunk);
}

static void
xfs_zone_gc_finish_chunk(
	struct xfs_gc_bio	*chunk)
{
	uint			iolock = XFS_IOLOCK_EXCL | XFS_MMAPLOCK_EXCL;
	struct xfs_inode	*ip = chunk->ip;
	struct xfs_mount	*mp = ip->i_mount;
	int			error;

	if (chunk->bio.bi_status)
		xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
	if (xfs_is_shutdown(mp)) {
		xfs_zone_gc_free_chunk(chunk);
		return;
	}

	chunk->scratch->freed += chunk->len;
	if (chunk->scratch->freed == chunk->scratch->offset) {
		chunk->scratch->offset = 0;
		chunk->scratch->freed = 0;
	}

	/*
	 * Cycle through the iolock and wait for direct I/O and layouts to
	 * ensure no one is reading from the old mapping before it goes away.
	 *
	 * Note that xfs_zoned_end_io() below checks that no other writer raced
	 * with us to update the mapping by checking that the old startblock
	 * didn't change.
	 */
	xfs_ilock(ip, iolock);
	error = xfs_break_layouts(VFS_I(ip), &iolock, BREAK_UNMAP);
	if (!error)
		inode_dio_wait(VFS_I(ip));
	xfs_iunlock(ip, iolock);
	if (error)
		goto free;

	if (chunk->is_seq)
		chunk->new_daddr = chunk->bio.bi_iter.bi_sector;
	error = xfs_zoned_end_io(ip, chunk->offset, chunk->len,
			chunk->new_daddr, chunk->oz, chunk->old_startblock);
free:
	if (error)
		xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
	xfs_zone_gc_free_chunk(chunk);
}

static void
xfs_zone_gc_finish_reset(
	struct xfs_gc_bio	*chunk)
{
	struct xfs_rtgroup	*rtg = chunk->bio.bi_private;
	struct xfs_mount	*mp = rtg_mount(rtg);
	struct xfs_zone_info	*zi = mp->m_zone_info;

	if (chunk->bio.bi_status) {
		xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
		goto out;
	}

	xfs_group_set_mark(&rtg->rtg_group, XFS_RTG_FREE);
	atomic_inc(&zi->zi_nr_free_zones);

	xfs_zoned_add_available(mp, rtg_blocks(rtg));

	wake_up_all(&zi->zi_zone_wait);
out:
	list_del(&chunk->entry);
	bio_put(&chunk->bio);
}

static bool
xfs_zone_gc_prepare_reset(
	struct bio		*bio,
	struct xfs_rtgroup	*rtg)
{
	trace_xfs_zone_reset(rtg);

	ASSERT(rtg_rmap(rtg)->i_used_blocks == 0);
	bio->bi_iter.bi_sector = xfs_gbno_to_daddr(&rtg->rtg_group, 0);
	if (!bdev_zone_is_seq(bio->bi_bdev, bio->bi_iter.bi_sector)) {
		if (!bdev_max_discard_sectors(bio->bi_bdev))
			return false;
		bio->bi_opf = REQ_OP_DISCARD | REQ_SYNC;
		bio->bi_iter.bi_size =
			XFS_FSB_TO_B(rtg_mount(rtg), rtg_blocks(rtg));
	}

	return true;
}

int
xfs_zone_gc_reset_sync(
	struct xfs_rtgroup	*rtg)
{
	int			error = 0;
	struct bio		bio;

	bio_init(&bio, rtg_mount(rtg)->m_rtdev_targp->bt_bdev, NULL, 0,
			REQ_OP_ZONE_RESET);
	if (xfs_zone_gc_prepare_reset(&bio, rtg))
		error = submit_bio_wait(&bio);
	bio_uninit(&bio);

	return error;
}

static void
xfs_zone_gc_reset_zones(
	struct xfs_zone_gc_data	*data,
	struct xfs_group	*reset_list)
{
	struct xfs_group	*next = reset_list;

	if (blkdev_issue_flush(data->mp->m_rtdev_targp->bt_bdev) < 0) {
		xfs_force_shutdown(data->mp, SHUTDOWN_META_IO_ERROR);
		return;
	}

	do {
		struct xfs_rtgroup	*rtg = to_rtg(next);
		struct xfs_gc_bio	*chunk;
		struct bio		*bio;

		xfs_log_force_inode(rtg_rmap(rtg));

		next = rtg_group(rtg)->xg_next_reset;
		rtg_group(rtg)->xg_next_reset = NULL;

		bio = bio_alloc_bioset(rtg_mount(rtg)->m_rtdev_targp->bt_bdev,
				0, REQ_OP_ZONE_RESET, GFP_NOFS, &data->bio_set);
		bio->bi_private = rtg;
		bio->bi_end_io = xfs_zone_gc_end_io;

		chunk = container_of(bio, struct xfs_gc_bio, bio);
		chunk->data = data;
		WRITE_ONCE(chunk->state, XFS_GC_BIO_NEW);
		list_add_tail(&chunk->entry, &data->resetting);

		/*
		 * Also use the bio to drive the state machine when neither
		 * zone reset nor discard is supported to keep things simple.
		 */
		if (xfs_zone_gc_prepare_reset(bio, rtg))
			submit_bio(bio);
		else
			bio_endio(bio);
	} while (next);
}

/*
 * Handle the work to read and write data for GC and to reset the zones,
 * including handling all completions.
 *
 * Note that the order of the chunks is preserved so that we don't undo the
 * optimal order established by xfs_zone_gc_query().
 */
static bool
xfs_zone_gc_handle_work(
	struct xfs_zone_gc_data	*data)
{
	struct xfs_zone_info	*zi = data->mp->m_zone_info;
	struct xfs_gc_bio	*chunk, *next;
	struct xfs_group	*reset_list;
	struct blk_plug		plug;

	spin_lock(&zi->zi_reset_list_lock);
	reset_list = zi->zi_reset_list;
	zi->zi_reset_list = NULL;
	spin_unlock(&zi->zi_reset_list_lock);

	if (!xfs_zone_gc_select_victim(data) ||
	    !xfs_zone_gc_space_available(data)) {
		if (list_empty(&data->reading) &&
		    list_empty(&data->writing) &&
		    list_empty(&data->resetting) &&
		    !reset_list)
			return false;
	}

	__set_current_state(TASK_RUNNING);
	try_to_freeze();

	if (reset_list)
		xfs_zone_gc_reset_zones(data, reset_list);

	list_for_each_entry_safe(chunk, next, &data->resetting, entry) {
		if (READ_ONCE(chunk->state) != XFS_GC_BIO_DONE)
			break;
		xfs_zone_gc_finish_reset(chunk);
	}

	list_for_each_entry_safe(chunk, next, &data->writing, entry) {
		if (READ_ONCE(chunk->state) != XFS_GC_BIO_DONE)
			break;
		xfs_zone_gc_finish_chunk(chunk);
	}

	blk_start_plug(&plug);
	list_for_each_entry_safe(chunk, next, &data->reading, entry) {
		if (READ_ONCE(chunk->state) != XFS_GC_BIO_DONE)
			break;
		xfs_zone_gc_write_chunk(chunk);
	}
	blk_finish_plug(&plug);

	blk_start_plug(&plug);
	while (xfs_zone_gc_start_chunk(data))
		;
	blk_finish_plug(&plug);
	return true;
}

/*
 * Note that the current GC algorithm would break reflinks and thus duplicate
 * data that was shared by multiple owners before.  Because of that reflinks
 * are currently not supported on zoned file systems and can't be created or
 * mounted.
 */
static int
xfs_zoned_gcd(
	void			*private)
{
	struct xfs_zone_gc_data	*data = private;
	struct xfs_mount	*mp = data->mp;
	struct xfs_zone_info	*zi = mp->m_zone_info;
	unsigned int		nofs_flag;

	nofs_flag = memalloc_nofs_save();
	set_freezable();

	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE | TASK_FREEZABLE);
		xfs_set_zonegc_running(mp);
		if (xfs_zone_gc_handle_work(data))
			continue;

		if (list_empty(&data->reading) &&
		    list_empty(&data->writing) &&
		    list_empty(&data->resetting) &&
		    !zi->zi_reset_list) {
			xfs_clear_zonegc_running(mp);
			xfs_zoned_resv_wake_all(mp);

			if (kthread_should_stop()) {
				__set_current_state(TASK_RUNNING);
				break;
			}

			if (kthread_should_park()) {
				__set_current_state(TASK_RUNNING);
				kthread_parkme();
				continue;
			}
		}

		schedule();
	}
	xfs_clear_zonegc_running(mp);

	if (data->iter.victim_rtg)
		xfs_rtgroup_rele(data->iter.victim_rtg);

	memalloc_nofs_restore(nofs_flag);
	xfs_zone_gc_data_free(data);
	return 0;
}

void
xfs_zone_gc_start(
	struct xfs_mount	*mp)
{
	if (xfs_has_zoned(mp))
		kthread_unpark(mp->m_zone_info->zi_gc_thread);
}

void
xfs_zone_gc_stop(
	struct xfs_mount	*mp)
{
	if (xfs_has_zoned(mp))
		kthread_park(mp->m_zone_info->zi_gc_thread);
}

int
xfs_zone_gc_mount(
	struct xfs_mount	*mp)
{
	struct xfs_zone_info	*zi = mp->m_zone_info;
	struct xfs_zone_gc_data	*data;
	struct xfs_open_zone	*oz;
	int			error;

	/*
	 * If there are no free zones available for GC, pick the open zone with
	 * the least used space to GC into.  This should only happen after an
	 * unclean shutdown near ENOSPC while GC was ongoing.
	 *
	 * We also need to do this for the first gc zone allocation if we
	 * unmounted while at the open limit.
	 */
	if (!xfs_group_marked(mp, XG_TYPE_RTG, XFS_RTG_FREE) ||
	    zi->zi_nr_open_zones == mp->m_max_open_zones)
		oz = xfs_zone_gc_steal_open(zi);
	else
		oz = xfs_open_zone(mp, WRITE_LIFE_NOT_SET, true);
	if (!oz) {
		xfs_warn(mp, "unable to allocate a zone for gc");
		error = -EIO;
		goto out;
	}

	trace_xfs_zone_gc_target_opened(oz->oz_rtg);
	zi->zi_open_gc_zone = oz;

	data = xfs_zone_gc_data_alloc(mp);
	if (!data) {
		error = -ENOMEM;
		goto out_put_gc_zone;
	}

	mp->m_zone_info->zi_gc_thread = kthread_create(xfs_zoned_gcd, data,
			"xfs-zone-gc/%s", mp->m_super->s_id);
	if (IS_ERR(mp->m_zone_info->zi_gc_thread)) {
		xfs_warn(mp, "unable to create zone gc thread");
		error = PTR_ERR(mp->m_zone_info->zi_gc_thread);
		goto out_free_gc_data;
	}

	/* xfs_zone_gc_start will unpark for rw mounts */
	kthread_park(mp->m_zone_info->zi_gc_thread);
	return 0;

out_free_gc_data:
	kfree(data);
out_put_gc_zone:
	xfs_open_zone_put(zi->zi_open_gc_zone);
out:
	return error;
}

void
xfs_zone_gc_unmount(
	struct xfs_mount	*mp)
{
	struct xfs_zone_info	*zi = mp->m_zone_info;

	kthread_stop(zi->zi_gc_thread);
	if (zi->zi_open_gc_zone)
		xfs_open_zone_put(zi->zi_open_gc_zone);
}
