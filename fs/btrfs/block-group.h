/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_BLOCK_GROUP_H
#define BTRFS_BLOCK_GROUP_H

#include "free-space-cache.h"

enum btrfs_disk_cache_state {
	BTRFS_DC_WRITTEN,
	BTRFS_DC_ERROR,
	BTRFS_DC_CLEAR,
	BTRFS_DC_SETUP,
};

/*
 * This describes the state of the block_group for async discard.  This is due
 * to the two pass nature of it where extent discarding is prioritized over
 * bitmap discarding.  BTRFS_DISCARD_RESET_CURSOR is set when we are resetting
 * between lists to prevent contention for discard state variables
 * (eg. discard_cursor).
 */
enum btrfs_discard_state {
	BTRFS_DISCARD_EXTENTS,
	BTRFS_DISCARD_BITMAPS,
	BTRFS_DISCARD_RESET_CURSOR,
};

/*
 * Control flags for do_chunk_alloc's force field CHUNK_ALLOC_NO_FORCE means to
 * only allocate a chunk if we really need one.
 *
 * CHUNK_ALLOC_LIMITED means to only try and allocate one if we have very few
 * chunks already allocated.  This is used as part of the clustering code to
 * help make sure we have a good pool of storage to cluster in, without filling
 * the FS with empty chunks
 *
 * CHUNK_ALLOC_FORCE means it must try to allocate one
 *
 * CHUNK_ALLOC_FORCE_FOR_EXTENT like CHUNK_ALLOC_FORCE but called from
 * find_free_extent() that also activaes the zone
 */
enum btrfs_chunk_alloc_enum {
	CHUNK_ALLOC_NO_FORCE,
	CHUNK_ALLOC_LIMITED,
	CHUNK_ALLOC_FORCE,
	CHUNK_ALLOC_FORCE_FOR_EXTENT,
};

struct btrfs_caching_control {
	struct list_head list;
	struct mutex mutex;
	wait_queue_head_t wait;
	struct btrfs_work work;
	struct btrfs_block_group *block_group;
	u64 progress;
	refcount_t count;
};

/* Once caching_thread() finds this much free space, it will wake up waiters. */
#define CACHING_CTL_WAKE_UP SZ_2M

struct btrfs_block_group {
	struct btrfs_fs_info *fs_info;
	struct inode *inode;
	spinlock_t lock;
	u64 start;
	u64 length;
	u64 pinned;
	u64 reserved;
	u64 used;
	u64 delalloc_bytes;
	u64 bytes_super;
	u64 flags;
	u64 cache_generation;
	u64 global_root_id;

	/*
	 * If the free space extent count exceeds this number, convert the block
	 * group to bitmaps.
	 */
	u32 bitmap_high_thresh;

	/*
	 * If the free space extent count drops below this number, convert the
	 * block group back to extents.
	 */
	u32 bitmap_low_thresh;

	/*
	 * It is just used for the delayed data space allocation because
	 * only the data space allocation and the relative metadata update
	 * can be done cross the transaction.
	 */
	struct rw_semaphore data_rwsem;

	/* For raid56, this is a full stripe, without parity */
	unsigned long full_stripe_len;

	unsigned int ro;
	unsigned int iref:1;
	unsigned int has_caching_ctl:1;
	unsigned int removed:1;
	unsigned int to_copy:1;
	unsigned int relocating_repair:1;
	unsigned int chunk_item_inserted:1;
	unsigned int zone_is_active:1;
	unsigned int zoned_data_reloc_ongoing:1;

	int disk_cache_state;

	/* Cache tracking stuff */
	int cached;
	struct btrfs_caching_control *caching_ctl;
	u64 last_byte_to_unpin;

	struct btrfs_space_info *space_info;

	/* Free space cache stuff */
	struct btrfs_free_space_ctl *free_space_ctl;

	/* Block group cache stuff */
	struct rb_node cache_node;

	/* For block groups in the same raid type */
	struct list_head list;

	refcount_t refs;

	/*
	 * List of struct btrfs_free_clusters for this block group.
	 * Today it will only have one thing on it, but that may change
	 */
	struct list_head cluster_list;

	/* For delayed block group creation or deletion of empty block groups */
	struct list_head bg_list;

	/* For read-only block groups */
	struct list_head ro_list;

	/*
	 * When non-zero it means the block group's logical address and its
	 * device extents can not be reused for future block group allocations
	 * until the counter goes down to 0. This is to prevent them from being
	 * reused while some task is still using the block group after it was
	 * deleted - we want to make sure they can only be reused for new block
	 * groups after that task is done with the deleted block group.
	 */
	atomic_t frozen;

	/* For discard operations */
	struct list_head discard_list;
	int discard_index;
	u64 discard_eligible_time;
	u64 discard_cursor;
	enum btrfs_discard_state discard_state;

	/* For dirty block groups */
	struct list_head dirty_list;
	struct list_head io_list;

	struct btrfs_io_ctl io_ctl;

	/*
	 * Incremented when doing extent allocations and holding a read lock
	 * on the space_info's groups_sem semaphore.
	 * Decremented when an ordered extent that represents an IO against this
	 * block group's range is created (after it's added to its inode's
	 * root's list of ordered extents) or immediately after the allocation
	 * if it's a metadata extent or fallocate extent (for these cases we
	 * don't create ordered extents).
	 */
	atomic_t reservations;

	/*
	 * Incremented while holding the spinlock *lock* by a task checking if
	 * it can perform a nocow write (incremented if the value for the *ro*
	 * field is 0). Decremented by such tasks once they create an ordered
	 * extent or before that if some error happens before reaching that step.
	 * This is to prevent races between block group relocation and nocow
	 * writes through direct IO.
	 */
	atomic_t nocow_writers;

	/* Lock for free space tree operations. */
	struct mutex free_space_lock;

	/*
	 * Does the block group need to be added to the free space tree?
	 * Protected by free_space_lock.
	 */
	int needs_free_space;

	/* Flag indicating this block group is placed on a sequential zone */
	bool seq_zone;

	/*
	 * Number of extents in this block group used for swap files.
	 * All accesses protected by the spinlock 'lock'.
	 */
	int swap_extents;

	/* Record locked full stripes for RAID5/6 block group */
	struct btrfs_full_stripe_locks_tree full_stripe_locks_root;

	/*
	 * Allocation offset for the block group to implement sequential
	 * allocation. This is used only on a zoned filesystem.
	 */
	u64 alloc_offset;
	u64 zone_unusable;
	u64 zone_capacity;
	u64 meta_write_pointer;
	struct map_lookup *physical_map;
	struct list_head active_bg_list;
	struct work_struct zone_finish_work;
	struct extent_buffer *last_eb;
};

static inline u64 btrfs_block_group_end(struct btrfs_block_group *block_group)
{
	return (block_group->start + block_group->length);
}

static inline bool btrfs_is_block_group_data_only(
					struct btrfs_block_group *block_group)
{
	/*
	 * In mixed mode the fragmentation is expected to be high, lowering the
	 * efficiency, so only proper data block groups are considered.
	 */
	return (block_group->flags & BTRFS_BLOCK_GROUP_DATA) &&
	       !(block_group->flags & BTRFS_BLOCK_GROUP_METADATA);
}

#ifdef CONFIG_BTRFS_DEBUG
static inline int btrfs_should_fragment_free_space(
		struct btrfs_block_group *block_group)
{
	struct btrfs_fs_info *fs_info = block_group->fs_info;

	return (btrfs_test_opt(fs_info, FRAGMENT_METADATA) &&
		block_group->flags & BTRFS_BLOCK_GROUP_METADATA) ||
	       (btrfs_test_opt(fs_info, FRAGMENT_DATA) &&
		block_group->flags &  BTRFS_BLOCK_GROUP_DATA);
}
#endif

struct btrfs_block_group *btrfs_lookup_first_block_group(
		struct btrfs_fs_info *info, u64 bytenr);
struct btrfs_block_group *btrfs_lookup_block_group(
		struct btrfs_fs_info *info, u64 bytenr);
struct btrfs_block_group *btrfs_next_block_group(
		struct btrfs_block_group *cache);
void btrfs_get_block_group(struct btrfs_block_group *cache);
void btrfs_put_block_group(struct btrfs_block_group *cache);
void btrfs_dec_block_group_reservations(struct btrfs_fs_info *fs_info,
					const u64 start);
void btrfs_wait_block_group_reservations(struct btrfs_block_group *bg);
struct btrfs_block_group *btrfs_inc_nocow_writers(struct btrfs_fs_info *fs_info,
						  u64 bytenr);
void btrfs_dec_nocow_writers(struct btrfs_block_group *bg);
void btrfs_wait_nocow_writers(struct btrfs_block_group *bg);
void btrfs_wait_block_group_cache_progress(struct btrfs_block_group *cache,
				           u64 num_bytes);
int btrfs_wait_block_group_cache_done(struct btrfs_block_group *cache);
int btrfs_cache_block_group(struct btrfs_block_group *cache,
			    int load_cache_only);
void btrfs_put_caching_control(struct btrfs_caching_control *ctl);
struct btrfs_caching_control *btrfs_get_caching_control(
		struct btrfs_block_group *cache);
u64 add_new_free_space(struct btrfs_block_group *block_group,
		       u64 start, u64 end);
struct btrfs_trans_handle *btrfs_start_trans_remove_block_group(
				struct btrfs_fs_info *fs_info,
				const u64 chunk_offset);
int btrfs_remove_block_group(struct btrfs_trans_handle *trans,
			     u64 group_start, struct extent_map *em);
void btrfs_delete_unused_bgs(struct btrfs_fs_info *fs_info);
void btrfs_mark_bg_unused(struct btrfs_block_group *bg);
void btrfs_reclaim_bgs_work(struct work_struct *work);
void btrfs_reclaim_bgs(struct btrfs_fs_info *fs_info);
void btrfs_mark_bg_to_reclaim(struct btrfs_block_group *bg);
int btrfs_read_block_groups(struct btrfs_fs_info *info);
struct btrfs_block_group *btrfs_make_block_group(struct btrfs_trans_handle *trans,
						 u64 bytes_used, u64 type,
						 u64 chunk_offset, u64 size);
void btrfs_create_pending_block_groups(struct btrfs_trans_handle *trans);
int btrfs_inc_block_group_ro(struct btrfs_block_group *cache,
			     bool do_chunk_alloc);
void btrfs_dec_block_group_ro(struct btrfs_block_group *cache);
int btrfs_start_dirty_block_groups(struct btrfs_trans_handle *trans);
int btrfs_write_dirty_block_groups(struct btrfs_trans_handle *trans);
int btrfs_setup_space_cache(struct btrfs_trans_handle *trans);
int btrfs_update_block_group(struct btrfs_trans_handle *trans,
			     u64 bytenr, u64 num_bytes, bool alloc);
int btrfs_add_reserved_bytes(struct btrfs_block_group *cache,
			     u64 ram_bytes, u64 num_bytes, int delalloc);
void btrfs_free_reserved_bytes(struct btrfs_block_group *cache,
			       u64 num_bytes, int delalloc);
int btrfs_chunk_alloc(struct btrfs_trans_handle *trans, u64 flags,
		      enum btrfs_chunk_alloc_enum force);
int btrfs_force_chunk_alloc(struct btrfs_trans_handle *trans, u64 type);
void check_system_chunk(struct btrfs_trans_handle *trans, const u64 type);
void btrfs_reserve_chunk_metadata(struct btrfs_trans_handle *trans,
				  bool is_item_insertion);
u64 btrfs_get_alloc_profile(struct btrfs_fs_info *fs_info, u64 orig_flags);
void btrfs_put_block_group_cache(struct btrfs_fs_info *info);
int btrfs_free_block_groups(struct btrfs_fs_info *info);
void btrfs_wait_space_cache_v1_finished(struct btrfs_block_group *cache,
				struct btrfs_caching_control *caching_ctl);
int btrfs_rmap_block(struct btrfs_fs_info *fs_info, u64 chunk_start,
		       struct block_device *bdev, u64 physical, u64 **logical,
		       int *naddrs, int *stripe_len);

static inline u64 btrfs_data_alloc_profile(struct btrfs_fs_info *fs_info)
{
	return btrfs_get_alloc_profile(fs_info, BTRFS_BLOCK_GROUP_DATA);
}

static inline u64 btrfs_metadata_alloc_profile(struct btrfs_fs_info *fs_info)
{
	return btrfs_get_alloc_profile(fs_info, BTRFS_BLOCK_GROUP_METADATA);
}

static inline u64 btrfs_system_alloc_profile(struct btrfs_fs_info *fs_info)
{
	return btrfs_get_alloc_profile(fs_info, BTRFS_BLOCK_GROUP_SYSTEM);
}

static inline int btrfs_block_group_done(struct btrfs_block_group *cache)
{
	smp_mb();
	return cache->cached == BTRFS_CACHE_FINISHED ||
		cache->cached == BTRFS_CACHE_ERROR;
}

void btrfs_freeze_block_group(struct btrfs_block_group *cache);
void btrfs_unfreeze_block_group(struct btrfs_block_group *cache);

bool btrfs_inc_block_group_swap_extents(struct btrfs_block_group *bg);
void btrfs_dec_block_group_swap_extents(struct btrfs_block_group *bg, int amount);

#endif /* BTRFS_BLOCK_GROUP_H */
