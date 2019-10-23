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
 * Control flags for do_chunk_alloc's force field CHUNK_ALLOC_NO_FORCE means to
 * only allocate a chunk if we really need one.
 *
 * CHUNK_ALLOC_LIMITED means to only try and allocate one if we have very few
 * chunks already allocated.  This is used as part of the clustering code to
 * help make sure we have a good pool of storage to cluster in, without filling
 * the FS with empty chunks
 *
 * CHUNK_ALLOC_FORCE means it must try to allocate one
 */
enum btrfs_chunk_alloc_enum {
	CHUNK_ALLOC_NO_FORCE,
	CHUNK_ALLOC_LIMITED,
	CHUNK_ALLOC_FORCE,
};

struct btrfs_caching_control {
	struct list_head list;
	struct mutex mutex;
	wait_queue_head_t wait;
	struct btrfs_work work;
	struct btrfs_block_group_cache *block_group;
	u64 progress;
	refcount_t count;
};

/* Once caching_thread() finds this much free space, it will wake up waiters. */
#define CACHING_CTL_WAKE_UP SZ_2M

struct btrfs_block_group_cache {
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

	/* Usage count */
	atomic_t count;

	/*
	 * List of struct btrfs_free_clusters for this block group.
	 * Today it will only have one thing on it, but that may change
	 */
	struct list_head cluster_list;

	/* For delayed block group creation or deletion of empty block groups */
	struct list_head bg_list;

	/* For read-only block groups */
	struct list_head ro_list;

	atomic_t trimming;

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

	/* Record locked full stripes for RAID5/6 block group */
	struct btrfs_full_stripe_locks_tree full_stripe_locks_root;
};

#ifdef CONFIG_BTRFS_DEBUG
static inline int btrfs_should_fragment_free_space(
		struct btrfs_block_group_cache *block_group)
{
	struct btrfs_fs_info *fs_info = block_group->fs_info;

	return (btrfs_test_opt(fs_info, FRAGMENT_METADATA) &&
		block_group->flags & BTRFS_BLOCK_GROUP_METADATA) ||
	       (btrfs_test_opt(fs_info, FRAGMENT_DATA) &&
		block_group->flags &  BTRFS_BLOCK_GROUP_DATA);
}
#endif

struct btrfs_block_group_cache *btrfs_lookup_first_block_group(
		struct btrfs_fs_info *info, u64 bytenr);
struct btrfs_block_group_cache *btrfs_lookup_block_group(
		struct btrfs_fs_info *info, u64 bytenr);
struct btrfs_block_group_cache *btrfs_next_block_group(
		struct btrfs_block_group_cache *cache);
void btrfs_get_block_group(struct btrfs_block_group_cache *cache);
void btrfs_put_block_group(struct btrfs_block_group_cache *cache);
void btrfs_dec_block_group_reservations(struct btrfs_fs_info *fs_info,
					const u64 start);
void btrfs_wait_block_group_reservations(struct btrfs_block_group_cache *bg);
bool btrfs_inc_nocow_writers(struct btrfs_fs_info *fs_info, u64 bytenr);
void btrfs_dec_nocow_writers(struct btrfs_fs_info *fs_info, u64 bytenr);
void btrfs_wait_nocow_writers(struct btrfs_block_group_cache *bg);
void btrfs_wait_block_group_cache_progress(struct btrfs_block_group_cache *cache,
				           u64 num_bytes);
int btrfs_wait_block_group_cache_done(struct btrfs_block_group_cache *cache);
int btrfs_cache_block_group(struct btrfs_block_group_cache *cache,
			    int load_cache_only);
void btrfs_put_caching_control(struct btrfs_caching_control *ctl);
struct btrfs_caching_control *btrfs_get_caching_control(
		struct btrfs_block_group_cache *cache);
u64 add_new_free_space(struct btrfs_block_group_cache *block_group,
		       u64 start, u64 end);
struct btrfs_trans_handle *btrfs_start_trans_remove_block_group(
				struct btrfs_fs_info *fs_info,
				const u64 chunk_offset);
int btrfs_remove_block_group(struct btrfs_trans_handle *trans,
			     u64 group_start, struct extent_map *em);
void btrfs_delete_unused_bgs(struct btrfs_fs_info *fs_info);
void btrfs_mark_bg_unused(struct btrfs_block_group_cache *bg);
int btrfs_read_block_groups(struct btrfs_fs_info *info);
int btrfs_make_block_group(struct btrfs_trans_handle *trans, u64 bytes_used,
			   u64 type, u64 chunk_offset, u64 size);
void btrfs_create_pending_block_groups(struct btrfs_trans_handle *trans);
int btrfs_inc_block_group_ro(struct btrfs_block_group_cache *cache);
void btrfs_dec_block_group_ro(struct btrfs_block_group_cache *cache);
int btrfs_start_dirty_block_groups(struct btrfs_trans_handle *trans);
int btrfs_write_dirty_block_groups(struct btrfs_trans_handle *trans);
int btrfs_setup_space_cache(struct btrfs_trans_handle *trans);
int btrfs_update_block_group(struct btrfs_trans_handle *trans,
			     u64 bytenr, u64 num_bytes, int alloc);
int btrfs_add_reserved_bytes(struct btrfs_block_group_cache *cache,
			     u64 ram_bytes, u64 num_bytes, int delalloc);
void btrfs_free_reserved_bytes(struct btrfs_block_group_cache *cache,
			       u64 num_bytes, int delalloc);
int btrfs_chunk_alloc(struct btrfs_trans_handle *trans, u64 flags,
		      enum btrfs_chunk_alloc_enum force);
int btrfs_force_chunk_alloc(struct btrfs_trans_handle *trans, u64 type);
void check_system_chunk(struct btrfs_trans_handle *trans, const u64 type);
u64 btrfs_get_alloc_profile(struct btrfs_fs_info *fs_info, u64 orig_flags);
void btrfs_put_block_group_cache(struct btrfs_fs_info *info);
int btrfs_free_block_groups(struct btrfs_fs_info *info);

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

static inline int btrfs_block_group_cache_done(
		struct btrfs_block_group_cache *cache)
{
	smp_mb();
	return cache->cached == BTRFS_CACHE_FINISHED ||
		cache->cached == BTRFS_CACHE_ERROR;
}

#endif /* BTRFS_BLOCK_GROUP_H */
