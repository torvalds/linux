/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XFS_ZONE_PRIV_H
#define _XFS_ZONE_PRIV_H

struct xfs_open_zone {
	/*
	 * Entry in the open zone list and refcount.  Protected by
	 * zi_open_zones_lock in struct xfs_zone_info.
	 */
	struct list_head	oz_entry;
	atomic_t		oz_ref;

	/*
	 * oz_allocated is the amount of space already allocated out of the zone
	 * and is protected by oz_alloc_lock.
	 *
	 * For conventional zones it also is the offset of the next write.
	 */
	spinlock_t		oz_alloc_lock;
	xfs_rgblock_t		oz_allocated;

	/*
	 * oz_written is the number of blocks for which we've received a write
	 * completion.  oz_written must always be <= oz_allocated and is
	 * protected by the ILOCK of the rmap inode.
	 */
	xfs_rgblock_t		oz_written;

	/*
	 * Write hint (data temperature) assigned to this zone, or
	 * WRITE_LIFE_NOT_SET if none was set.
	 */
	enum rw_hint		oz_write_hint;

	/*
	 * Is this open zone used for garbage collection?  There can only be a
	 * single open GC zone, which is pointed to by zi_open_gc_zone in
	 * struct xfs_zone_info.  Constant over the life time of an open zone.
	 */
	bool			oz_is_gc;

	/*
	 * Pointer to the RT groups structure for this open zone.  Constant over
	 * the life time of an open zone.
	 */
	struct xfs_rtgroup	*oz_rtg;
};

/*
 * Number of bitmap buckets to track reclaimable zones.  There are 10 buckets
 * so that each 10% of the usable capacity get their own bucket and GC can
 * only has to walk the bitmaps of the lesser used zones if there are any.
 */
#define XFS_ZONE_USED_BUCKETS		10u

struct xfs_zone_info {
	/*
	 * List of pending space reservations:
	 */
	spinlock_t		zi_reservation_lock;
	struct list_head	zi_reclaim_reservations;

	/*
	 * List and number of open zones:
	 */
	spinlock_t		zi_open_zones_lock;
	struct list_head	zi_open_zones;
	unsigned int		zi_nr_open_zones;

	/*
	 * Free zone search cursor and number of free zones:
	 */
	unsigned long		zi_free_zone_cursor;
	atomic_t		zi_nr_free_zones;

	/*
	 * Wait queue to wait for free zones or open zone resources to become
	 * available:
	 */
	wait_queue_head_t	zi_zone_wait;

	/*
	 * Pointer to the GC thread, and the current open zone used by GC
	 * (if any).
	 *
	 * zi_open_gc_zone is mostly private to the GC thread, but can be read
	 * for debugging from other threads, in which case zi_open_zones_lock
	 * must be taken to access it.
	 */
	struct task_struct      *zi_gc_thread;
	struct xfs_open_zone	*zi_open_gc_zone;

	/*
	 * List of zones that need a reset:
	 */
	spinlock_t		zi_reset_list_lock;
	struct xfs_group	*zi_reset_list;

	/*
	 * A set of bitmaps to bucket-sort reclaimable zones by used blocks to help
	 * garbage collection to quickly find the best candidate for reclaim.
	 */
	spinlock_t		zi_used_buckets_lock;
	unsigned int		zi_used_bucket_entries[XFS_ZONE_USED_BUCKETS];
	unsigned long		*zi_used_bucket_bitmap[XFS_ZONE_USED_BUCKETS];

};

struct xfs_open_zone *xfs_open_zone(struct xfs_mount *mp,
		enum rw_hint write_hint, bool is_gc);

int xfs_zone_gc_reset_sync(struct xfs_rtgroup *rtg);
bool xfs_zoned_need_gc(struct xfs_mount *mp);
int xfs_zone_gc_mount(struct xfs_mount *mp);
void xfs_zone_gc_unmount(struct xfs_mount *mp);

void xfs_zoned_resv_wake_all(struct xfs_mount *mp);

#endif /* _XFS_ZONE_PRIV_H */
