/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_SPACE_INFO_H
#define BTRFS_SPACE_INFO_H

#include <trace/events/btrfs.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/lockdep.h>
#include <linux/wait.h>
#include <linux/rwsem.h>
#include "volumes.h"

struct btrfs_fs_info;
struct btrfs_block_group;

/*
 * Different levels for to flush space when doing space reservations.
 *
 * The higher the level, the more methods we try to reclaim space.
 */
enum btrfs_reserve_flush_enum {
	/* If we are in the transaction, we can't flush anything.*/
	BTRFS_RESERVE_NO_FLUSH,

	/*
	 * Flush space by:
	 * - Running delayed inode items
	 * - Allocating a new chunk
	 */
	BTRFS_RESERVE_FLUSH_LIMIT,

	/*
	 * Flush space by:
	 * - Running delayed inode items
	 * - Running delayed refs
	 * - Running delalloc and waiting for ordered extents
	 * - Allocating a new chunk
	 * - Committing transaction
	 */
	BTRFS_RESERVE_FLUSH_EVICT,

	/*
	 * Flush space by above mentioned methods and by:
	 * - Running delayed iputs
	 * - Committing transaction
	 *
	 * Can be interrupted by a fatal signal.
	 */
	BTRFS_RESERVE_FLUSH_DATA,
	BTRFS_RESERVE_FLUSH_FREE_SPACE_INODE,
	BTRFS_RESERVE_FLUSH_ALL,

	/*
	 * Pretty much the same as FLUSH_ALL, but can also steal space from
	 * global rsv.
	 *
	 * Can be interrupted by a fatal signal.
	 */
	BTRFS_RESERVE_FLUSH_ALL_STEAL,

	/*
	 * This is for btrfs_use_block_rsv only.  We have exhausted our block
	 * rsv and our global block rsv.  This can happen for things like
	 * delalloc where we are overwriting a lot of extents with a single
	 * extent and didn't reserve enough space.  Alternatively it can happen
	 * with delalloc where we reserve 1 extents worth for a large extent but
	 * fragmentation leads to multiple extents being created.  This will
	 * give us the reservation in the case of
	 *
	 * if (num_bytes < (space_info->total_bytes -
	 *		    btrfs_space_info_used(space_info, false))
	 *
	 * Which ignores bytes_may_use.  This is potentially dangerous, but our
	 * reservation system is generally pessimistic so is able to absorb this
	 * style of mistake.
	 */
	BTRFS_RESERVE_FLUSH_EMERGENCY,
};

enum btrfs_flush_state {
	FLUSH_DELAYED_ITEMS_NR	= 1,
	FLUSH_DELAYED_ITEMS	= 2,
	FLUSH_DELAYED_REFS_NR	= 3,
	FLUSH_DELAYED_REFS	= 4,
	FLUSH_DELALLOC		= 5,
	FLUSH_DELALLOC_WAIT	= 6,
	FLUSH_DELALLOC_FULL	= 7,
	ALLOC_CHUNK		= 8,
	ALLOC_CHUNK_FORCE	= 9,
	RUN_DELAYED_IPUTS	= 10,
	COMMIT_TRANS		= 11,
};

struct btrfs_space_info {
	struct btrfs_fs_info *fs_info;
	spinlock_t lock;

	u64 total_bytes;	/* total bytes in the space,
				   this doesn't take mirrors into account */
	u64 bytes_used;		/* total bytes used,
				   this doesn't take mirrors into account */
	u64 bytes_pinned;	/* total bytes pinned, will be freed when the
				   transaction finishes */
	u64 bytes_reserved;	/* total bytes the allocator has reserved for
				   current allocations */
	u64 bytes_may_use;	/* number of bytes that may be used for
				   delalloc/allocations */
	u64 bytes_readonly;	/* total bytes that are read only */
	u64 bytes_zone_unusable;	/* total bytes that are unusable until
					   resetting the device zone */

	u64 max_extent_size;	/* This will hold the maximum extent size of
				   the space info if we had an ENOSPC in the
				   allocator. */
	/* Chunk size in bytes */
	u64 chunk_size;

	/*
	 * Once a block group drops below this threshold (percents) we'll
	 * schedule it for reclaim.
	 */
	int bg_reclaim_threshold;

	int clamp;		/* Used to scale our threshold for preemptive
				   flushing. The value is >> clamp, so turns
				   out to be a 2^clamp divisor. */

	unsigned int full:1;	/* indicates that we cannot allocate any more
				   chunks for this space */
	unsigned int chunk_alloc:1;	/* set if we are allocating a chunk */

	unsigned int flush:1;		/* set if we are trying to make space */

	unsigned int force_alloc;	/* set if we need to force a chunk
					   alloc for this space */

	u64 disk_used;		/* total bytes used on disk */
	u64 disk_total;		/* total bytes on disk, takes mirrors into
				   account */

	u64 flags;

	struct list_head list;
	/* Protected by the spinlock 'lock'. */
	struct list_head ro_bgs;
	struct list_head priority_tickets;
	struct list_head tickets;

	/*
	 * Size of space that needs to be reclaimed in order to satisfy pending
	 * tickets
	 */
	u64 reclaim_size;

	/*
	 * tickets_id just indicates the next ticket will be handled, so note
	 * it's not stored per ticket.
	 */
	u64 tickets_id;

	struct rw_semaphore groups_sem;
	/* for block groups in our same type */
	struct list_head block_groups[BTRFS_NR_RAID_TYPES];

	struct kobject kobj;
	struct kobject *block_group_kobjs[BTRFS_NR_RAID_TYPES];

	/*
	 * Monotonically increasing counter of block group reclaim attempts
	 * Exposed in /sys/fs/<uuid>/allocation/<type>/reclaim_count
	 */
	u64 reclaim_count;

	/*
	 * Monotonically increasing counter of reclaimed bytes
	 * Exposed in /sys/fs/<uuid>/allocation/<type>/reclaim_bytes
	 */
	u64 reclaim_bytes;

	/*
	 * Monotonically increasing counter of reclaim errors
	 * Exposed in /sys/fs/<uuid>/allocation/<type>/reclaim_errors
	 */
	u64 reclaim_errors;

	/*
	 * If true, use the dynamic relocation threshold, instead of the
	 * fixed bg_reclaim_threshold.
	 */
	bool dynamic_reclaim;

	/*
	 * Periodically check all block groups against the reclaim
	 * threshold in the cleaner thread.
	 */
	bool periodic_reclaim;

	/*
	 * Periodic reclaim should be a no-op if a space_info hasn't
	 * freed any space since the last time we tried.
	 */
	bool periodic_reclaim_ready;

	/*
	 * Net bytes freed or allocated since the last reclaim pass.
	 */
	s64 reclaimable_bytes;
};

struct reserve_ticket {
	u64 bytes;
	int error;
	bool steal;
	struct list_head list;
	wait_queue_head_t wait;
};

static inline bool btrfs_mixed_space_info(const struct btrfs_space_info *space_info)
{
	return ((space_info->flags & BTRFS_BLOCK_GROUP_METADATA) &&
		(space_info->flags & BTRFS_BLOCK_GROUP_DATA));
}

/*
 *
 * Declare a helper function to detect underflow of various space info members
 */
#define DECLARE_SPACE_INFO_UPDATE(name, trace_name)			\
static inline void							\
btrfs_space_info_update_##name(struct btrfs_fs_info *fs_info,		\
			       struct btrfs_space_info *sinfo,		\
			       s64 bytes)				\
{									\
	const u64 abs_bytes = (bytes < 0) ? -bytes : bytes;		\
	lockdep_assert_held(&sinfo->lock);				\
	trace_update_##name(fs_info, sinfo, sinfo->name, bytes);	\
	trace_btrfs_space_reservation(fs_info, trace_name,		\
				      sinfo->flags, abs_bytes,		\
				      bytes > 0);			\
	if (bytes < 0 && sinfo->name < -bytes) {			\
		WARN_ON(1);						\
		sinfo->name = 0;					\
		return;							\
	}								\
	sinfo->name += bytes;						\
}

DECLARE_SPACE_INFO_UPDATE(bytes_may_use, "space_info");
DECLARE_SPACE_INFO_UPDATE(bytes_pinned, "pinned");
DECLARE_SPACE_INFO_UPDATE(bytes_zone_unusable, "zone_unusable");

int btrfs_init_space_info(struct btrfs_fs_info *fs_info);
void btrfs_add_bg_to_space_info(struct btrfs_fs_info *info,
				struct btrfs_block_group *block_group);
void btrfs_update_space_info_chunk_size(struct btrfs_space_info *space_info,
					u64 chunk_size);
struct btrfs_space_info *btrfs_find_space_info(struct btrfs_fs_info *info,
					       u64 flags);
u64 __pure btrfs_space_info_used(const struct btrfs_space_info *s_info,
			  bool may_use_included);
void btrfs_clear_space_info_full(struct btrfs_fs_info *info);
void btrfs_dump_space_info(struct btrfs_fs_info *fs_info,
			   struct btrfs_space_info *info, u64 bytes,
			   int dump_block_groups);
int btrfs_reserve_metadata_bytes(struct btrfs_fs_info *fs_info,
				 struct btrfs_space_info *space_info,
				 u64 orig_bytes,
				 enum btrfs_reserve_flush_enum flush);
void btrfs_try_granting_tickets(struct btrfs_fs_info *fs_info,
				struct btrfs_space_info *space_info);
int btrfs_can_overcommit(struct btrfs_fs_info *fs_info,
			 const struct btrfs_space_info *space_info, u64 bytes,
			 enum btrfs_reserve_flush_enum flush);

static inline void btrfs_space_info_free_bytes_may_use(
				struct btrfs_fs_info *fs_info,
				struct btrfs_space_info *space_info,
				u64 num_bytes)
{
	spin_lock(&space_info->lock);
	btrfs_space_info_update_bytes_may_use(fs_info, space_info, -num_bytes);
	btrfs_try_granting_tickets(fs_info, space_info);
	spin_unlock(&space_info->lock);
}
int btrfs_reserve_data_bytes(struct btrfs_fs_info *fs_info, u64 bytes,
			     enum btrfs_reserve_flush_enum flush);
void btrfs_dump_space_info_for_trans_abort(struct btrfs_fs_info *fs_info);
void btrfs_init_async_reclaim_work(struct btrfs_fs_info *fs_info);
u64 btrfs_account_ro_block_groups_free_space(struct btrfs_space_info *sinfo);

void btrfs_space_info_update_reclaimable(struct btrfs_space_info *space_info, s64 bytes);
void btrfs_set_periodic_reclaim_ready(struct btrfs_space_info *space_info, bool ready);
bool btrfs_should_periodic_reclaim(struct btrfs_space_info *space_info);
int btrfs_calc_reclaim_threshold(const struct btrfs_space_info *space_info);
void btrfs_reclaim_sweep(const struct btrfs_fs_info *fs_info);

#endif /* BTRFS_SPACE_INFO_H */
