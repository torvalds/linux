/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#ifndef BTRFS_CTREE_H
#define BTRFS_CTREE_H

#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/highmem.h>
#include <linux/fs.h>
#include <linux/rwsem.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/backing-dev.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <trace/events/btrfs.h>
#include <asm/unaligned.h>
#include <linux/pagemap.h>
#include <linux/btrfs.h>
#include <linux/btrfs_tree.h>
#include <linux/workqueue.h>
#include <linux/security.h>
#include <linux/sizes.h>
#include <linux/dynamic_debug.h>
#include <linux/refcount.h>
#include <linux/crc32c.h>
#include <linux/iomap.h>
#include "extent-io-tree.h"
#include "extent_io.h"
#include "extent_map.h"
#include "async-thread.h"
#include "block-rsv.h"
#include "locking.h"

struct btrfs_trans_handle;
struct btrfs_transaction;
struct btrfs_pending_snapshot;
struct btrfs_delayed_ref_root;
struct btrfs_space_info;
struct btrfs_block_group;
extern struct kmem_cache *btrfs_trans_handle_cachep;
extern struct kmem_cache *btrfs_bit_radix_cachep;
extern struct kmem_cache *btrfs_path_cachep;
extern struct kmem_cache *btrfs_free_space_cachep;
extern struct kmem_cache *btrfs_free_space_bitmap_cachep;
struct btrfs_ordered_sum;
struct btrfs_ref;
struct btrfs_bio;

#define BTRFS_MAGIC 0x4D5F53665248425FULL /* ascii _BHRfS_M, no null */

/*
 * Maximum number of mirrors that can be available for all profiles counting
 * the target device of dev-replace as one. During an active device replace
 * procedure, the target device of the copy operation is a mirror for the
 * filesystem data as well that can be used to read data in order to repair
 * read errors on other disks.
 *
 * Current value is derived from RAID1C4 with 4 copies.
 */
#define BTRFS_MAX_MIRRORS (4 + 1)

#define BTRFS_MAX_LEVEL 8

#define BTRFS_OLDEST_GENERATION	0ULL

/*
 * we can actually store much bigger names, but lets not confuse the rest
 * of linux
 */
#define BTRFS_NAME_LEN 255

/*
 * Theoretical limit is larger, but we keep this down to a sane
 * value. That should limit greatly the possibility of collisions on
 * inode ref items.
 */
#define BTRFS_LINK_MAX 65535U

#define BTRFS_EMPTY_DIR_SIZE 0

/* ioprio of readahead is set to idle */
#define BTRFS_IOPRIO_READA (IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0))

#define BTRFS_DIRTY_METADATA_THRESH	SZ_32M

/*
 * Use large batch size to reduce overhead of metadata updates.  On the reader
 * side, we only read it when we are close to ENOSPC and the read overhead is
 * mostly related to the number of CPUs, so it is OK to use arbitrary large
 * value here.
 */
#define BTRFS_TOTAL_BYTES_PINNED_BATCH	SZ_128M

#define BTRFS_MAX_EXTENT_SIZE SZ_128M

/*
 * Deltas are an effective way to populate global statistics.  Give macro names
 * to make it clear what we're doing.  An example is discard_extents in
 * btrfs_free_space_ctl.
 */
#define BTRFS_STAT_NR_ENTRIES	2
#define BTRFS_STAT_CURR		0
#define BTRFS_STAT_PREV		1

/*
 * Count how many BTRFS_MAX_EXTENT_SIZE cover the @size
 */
static inline u32 count_max_extents(u64 size)
{
	return div_u64(size + BTRFS_MAX_EXTENT_SIZE - 1, BTRFS_MAX_EXTENT_SIZE);
}

static inline unsigned long btrfs_chunk_item_size(int num_stripes)
{
	BUG_ON(num_stripes == 0);
	return sizeof(struct btrfs_chunk) +
		sizeof(struct btrfs_stripe) * (num_stripes - 1);
}

/*
 * Runtime (in-memory) states of filesystem
 */
enum {
	/* Global indicator of serious filesystem errors */
	BTRFS_FS_STATE_ERROR,
	/*
	 * Filesystem is being remounted, allow to skip some operations, like
	 * defrag
	 */
	BTRFS_FS_STATE_REMOUNTING,
	/* Filesystem in RO mode */
	BTRFS_FS_STATE_RO,
	/* Track if a transaction abort has been reported on this filesystem */
	BTRFS_FS_STATE_TRANS_ABORTED,
	/*
	 * Bio operations should be blocked on this filesystem because a source
	 * or target device is being destroyed as part of a device replace
	 */
	BTRFS_FS_STATE_DEV_REPLACING,
	/* The btrfs_fs_info created for self-tests */
	BTRFS_FS_STATE_DUMMY_FS_INFO,

	BTRFS_FS_STATE_NO_CSUMS,

	/* Indicates there was an error cleaning up a log tree. */
	BTRFS_FS_STATE_LOG_CLEANUP_ERROR,
};

#define BTRFS_BACKREF_REV_MAX		256
#define BTRFS_BACKREF_REV_SHIFT		56
#define BTRFS_BACKREF_REV_MASK		(((u64)BTRFS_BACKREF_REV_MAX - 1) << \
					 BTRFS_BACKREF_REV_SHIFT)

#define BTRFS_OLD_BACKREF_REV		0
#define BTRFS_MIXED_BACKREF_REV		1

/*
 * every tree block (leaf or node) starts with this header.
 */
struct btrfs_header {
	/* these first four must match the super block */
	u8 csum[BTRFS_CSUM_SIZE];
	u8 fsid[BTRFS_FSID_SIZE]; /* FS specific uuid */
	__le64 bytenr; /* which block this node is supposed to live in */
	__le64 flags;

	/* allowed to be different from the super from here on down */
	u8 chunk_tree_uuid[BTRFS_UUID_SIZE];
	__le64 generation;
	__le64 owner;
	__le32 nritems;
	u8 level;
} __attribute__ ((__packed__));

/*
 * this is a very generous portion of the super block, giving us
 * room to translate 14 chunks with 3 stripes each.
 */
#define BTRFS_SYSTEM_CHUNK_ARRAY_SIZE 2048

/*
 * just in case we somehow lose the roots and are not able to mount,
 * we store an array of the roots from previous transactions
 * in the super.
 */
#define BTRFS_NUM_BACKUP_ROOTS 4
struct btrfs_root_backup {
	__le64 tree_root;
	__le64 tree_root_gen;

	__le64 chunk_root;
	__le64 chunk_root_gen;

	__le64 extent_root;
	__le64 extent_root_gen;

	__le64 fs_root;
	__le64 fs_root_gen;

	__le64 dev_root;
	__le64 dev_root_gen;

	__le64 csum_root;
	__le64 csum_root_gen;

	__le64 total_bytes;
	__le64 bytes_used;
	__le64 num_devices;
	/* future */
	__le64 unused_64[4];

	u8 tree_root_level;
	u8 chunk_root_level;
	u8 extent_root_level;
	u8 fs_root_level;
	u8 dev_root_level;
	u8 csum_root_level;
	/* future and to align */
	u8 unused_8[10];
} __attribute__ ((__packed__));

#define BTRFS_SUPER_INFO_OFFSET			SZ_64K
#define BTRFS_SUPER_INFO_SIZE			4096

/*
 * the super block basically lists the main trees of the FS
 * it currently lacks any block count etc etc
 */
struct btrfs_super_block {
	/* the first 4 fields must match struct btrfs_header */
	u8 csum[BTRFS_CSUM_SIZE];
	/* FS specific UUID, visible to user */
	u8 fsid[BTRFS_FSID_SIZE];
	__le64 bytenr; /* this block number */
	__le64 flags;

	/* allowed to be different from the btrfs_header from here own down */
	__le64 magic;
	__le64 generation;
	__le64 root;
	__le64 chunk_root;
	__le64 log_root;

	/* this will help find the new super based on the log root */
	__le64 log_root_transid;
	__le64 total_bytes;
	__le64 bytes_used;
	__le64 root_dir_objectid;
	__le64 num_devices;
	__le32 sectorsize;
	__le32 nodesize;
	__le32 __unused_leafsize;
	__le32 stripesize;
	__le32 sys_chunk_array_size;
	__le64 chunk_root_generation;
	__le64 compat_flags;
	__le64 compat_ro_flags;
	__le64 incompat_flags;
	__le16 csum_type;
	u8 root_level;
	u8 chunk_root_level;
	u8 log_root_level;
	struct btrfs_dev_item dev_item;

	char label[BTRFS_LABEL_SIZE];

	__le64 cache_generation;
	__le64 uuid_tree_generation;

	/* the UUID written into btree blocks */
	u8 metadata_uuid[BTRFS_FSID_SIZE];

	/* future expansion */
	__le64 reserved[28];
	u8 sys_chunk_array[BTRFS_SYSTEM_CHUNK_ARRAY_SIZE];
	struct btrfs_root_backup super_roots[BTRFS_NUM_BACKUP_ROOTS];

	/* Padded to 4096 bytes */
	u8 padding[565];
} __attribute__ ((__packed__));
static_assert(sizeof(struct btrfs_super_block) == BTRFS_SUPER_INFO_SIZE);

/*
 * Compat flags that we support.  If any incompat flags are set other than the
 * ones specified below then we will fail to mount
 */
#define BTRFS_FEATURE_COMPAT_SUPP		0ULL
#define BTRFS_FEATURE_COMPAT_SAFE_SET		0ULL
#define BTRFS_FEATURE_COMPAT_SAFE_CLEAR		0ULL

#define BTRFS_FEATURE_COMPAT_RO_SUPP			\
	(BTRFS_FEATURE_COMPAT_RO_FREE_SPACE_TREE |	\
	 BTRFS_FEATURE_COMPAT_RO_FREE_SPACE_TREE_VALID | \
	 BTRFS_FEATURE_COMPAT_RO_VERITY)

#define BTRFS_FEATURE_COMPAT_RO_SAFE_SET	0ULL
#define BTRFS_FEATURE_COMPAT_RO_SAFE_CLEAR	0ULL

#define BTRFS_FEATURE_INCOMPAT_SUPP			\
	(BTRFS_FEATURE_INCOMPAT_MIXED_BACKREF |		\
	 BTRFS_FEATURE_INCOMPAT_DEFAULT_SUBVOL |	\
	 BTRFS_FEATURE_INCOMPAT_MIXED_GROUPS |		\
	 BTRFS_FEATURE_INCOMPAT_BIG_METADATA |		\
	 BTRFS_FEATURE_INCOMPAT_COMPRESS_LZO |		\
	 BTRFS_FEATURE_INCOMPAT_COMPRESS_ZSTD |		\
	 BTRFS_FEATURE_INCOMPAT_RAID56 |		\
	 BTRFS_FEATURE_INCOMPAT_EXTENDED_IREF |		\
	 BTRFS_FEATURE_INCOMPAT_SKINNY_METADATA |	\
	 BTRFS_FEATURE_INCOMPAT_NO_HOLES	|	\
	 BTRFS_FEATURE_INCOMPAT_METADATA_UUID	|	\
	 BTRFS_FEATURE_INCOMPAT_RAID1C34	|	\
	 BTRFS_FEATURE_INCOMPAT_ZONED)

#define BTRFS_FEATURE_INCOMPAT_SAFE_SET			\
	(BTRFS_FEATURE_INCOMPAT_EXTENDED_IREF)
#define BTRFS_FEATURE_INCOMPAT_SAFE_CLEAR		0ULL

/*
 * A leaf is full of items. offset and size tell us where to find
 * the item in the leaf (relative to the start of the data area)
 */
struct btrfs_item {
	struct btrfs_disk_key key;
	__le32 offset;
	__le32 size;
} __attribute__ ((__packed__));

/*
 * leaves have an item area and a data area:
 * [item0, item1....itemN] [free space] [dataN...data1, data0]
 *
 * The data is separate from the items to get the keys closer together
 * during searches.
 */
struct btrfs_leaf {
	struct btrfs_header header;
	struct btrfs_item items[];
} __attribute__ ((__packed__));

/*
 * all non-leaf blocks are nodes, they hold only keys and pointers to
 * other blocks
 */
struct btrfs_key_ptr {
	struct btrfs_disk_key key;
	__le64 blockptr;
	__le64 generation;
} __attribute__ ((__packed__));

struct btrfs_node {
	struct btrfs_header header;
	struct btrfs_key_ptr ptrs[];
} __attribute__ ((__packed__));

/* Read ahead values for struct btrfs_path.reada */
enum {
	READA_NONE,
	READA_BACK,
	READA_FORWARD,
	/*
	 * Similar to READA_FORWARD but unlike it:
	 *
	 * 1) It will trigger readahead even for leaves that are not close to
	 *    each other on disk;
	 * 2) It also triggers readahead for nodes;
	 * 3) During a search, even when a node or leaf is already in memory, it
	 *    will still trigger readahead for other nodes and leaves that follow
	 *    it.
	 *
	 * This is meant to be used only when we know we are iterating over the
	 * entire tree or a very large part of it.
	 */
	READA_FORWARD_ALWAYS,
};

/*
 * btrfs_paths remember the path taken from the root down to the leaf.
 * level 0 is always the leaf, and nodes[1...BTRFS_MAX_LEVEL] will point
 * to any other levels that are present.
 *
 * The slots array records the index of the item or block pointer
 * used while walking the tree.
 */
struct btrfs_path {
	struct extent_buffer *nodes[BTRFS_MAX_LEVEL];
	int slots[BTRFS_MAX_LEVEL];
	/* if there is real range locking, this locks field will change */
	u8 locks[BTRFS_MAX_LEVEL];
	u8 reada;
	/* keep some upper locks as we walk down */
	u8 lowest_level;

	/*
	 * set by btrfs_split_item, tells search_slot to keep all locks
	 * and to force calls to keep space in the nodes
	 */
	unsigned int search_for_split:1;
	unsigned int keep_locks:1;
	unsigned int skip_locking:1;
	unsigned int search_commit_root:1;
	unsigned int need_commit_sem:1;
	unsigned int skip_release_on_error:1;
	/*
	 * Indicate that new item (btrfs_search_slot) is extending already
	 * existing item and ins_len contains only the data size and not item
	 * header (ie. sizeof(struct btrfs_item) is not included).
	 */
	unsigned int search_for_extension:1;
};
#define BTRFS_MAX_EXTENT_ITEM_SIZE(r) ((BTRFS_LEAF_DATA_SIZE(r->fs_info) >> 4) - \
					sizeof(struct btrfs_item))
struct btrfs_dev_replace {
	u64 replace_state;	/* see #define above */
	time64_t time_started;	/* seconds since 1-Jan-1970 */
	time64_t time_stopped;	/* seconds since 1-Jan-1970 */
	atomic64_t num_write_errors;
	atomic64_t num_uncorrectable_read_errors;

	u64 cursor_left;
	u64 committed_cursor_left;
	u64 cursor_left_last_write_of_item;
	u64 cursor_right;

	u64 cont_reading_from_srcdev_mode;	/* see #define above */

	int is_valid;
	int item_needs_writeback;
	struct btrfs_device *srcdev;
	struct btrfs_device *tgtdev;

	struct mutex lock_finishing_cancel_unmount;
	struct rw_semaphore rwsem;

	struct btrfs_scrub_progress scrub_progress;

	struct percpu_counter bio_counter;
	wait_queue_head_t replace_wait;
};

/*
 * free clusters are used to claim free space in relatively large chunks,
 * allowing us to do less seeky writes. They are used for all metadata
 * allocations. In ssd_spread mode they are also used for data allocations.
 */
struct btrfs_free_cluster {
	spinlock_t lock;
	spinlock_t refill_lock;
	struct rb_root root;

	/* largest extent in this cluster */
	u64 max_size;

	/* first extent starting offset */
	u64 window_start;

	/* We did a full search and couldn't create a cluster */
	bool fragmented;

	struct btrfs_block_group *block_group;
	/*
	 * when a cluster is allocated from a block group, we put the
	 * cluster onto a list in the block group so that it can
	 * be freed before the block group is freed.
	 */
	struct list_head block_group_list;
};

enum btrfs_caching_type {
	BTRFS_CACHE_NO,
	BTRFS_CACHE_STARTED,
	BTRFS_CACHE_FAST,
	BTRFS_CACHE_FINISHED,
	BTRFS_CACHE_ERROR,
};

/*
 * Tree to record all locked full stripes of a RAID5/6 block group
 */
struct btrfs_full_stripe_locks_tree {
	struct rb_root root;
	struct mutex lock;
};

/* Discard control. */
/*
 * Async discard uses multiple lists to differentiate the discard filter
 * parameters.  Index 0 is for completely free block groups where we need to
 * ensure the entire block group is trimmed without being lossy.  Indices
 * afterwards represent monotonically decreasing discard filter sizes to
 * prioritize what should be discarded next.
 */
#define BTRFS_NR_DISCARD_LISTS		3
#define BTRFS_DISCARD_INDEX_UNUSED	0
#define BTRFS_DISCARD_INDEX_START	1

struct btrfs_discard_ctl {
	struct workqueue_struct *discard_workers;
	struct delayed_work work;
	spinlock_t lock;
	struct btrfs_block_group *block_group;
	struct list_head discard_list[BTRFS_NR_DISCARD_LISTS];
	u64 prev_discard;
	u64 prev_discard_time;
	atomic_t discardable_extents;
	atomic64_t discardable_bytes;
	u64 max_discard_size;
	u64 delay_ms;
	u32 iops_limit;
	u32 kbps_limit;
	u64 discard_extent_bytes;
	u64 discard_bitmap_bytes;
	atomic64_t discard_bytes_saved;
};

void btrfs_init_async_reclaim_work(struct btrfs_fs_info *fs_info);

/* fs_info */
struct reloc_control;
struct btrfs_device;
struct btrfs_fs_devices;
struct btrfs_balance_control;
struct btrfs_delayed_root;

/*
 * Block group or device which contains an active swapfile. Used for preventing
 * unsafe operations while a swapfile is active.
 *
 * These are sorted on (ptr, inode) (note that a block group or device can
 * contain more than one swapfile). We compare the pointer values because we
 * don't actually care what the object is, we just need a quick check whether
 * the object exists in the rbtree.
 */
struct btrfs_swapfile_pin {
	struct rb_node node;
	void *ptr;
	struct inode *inode;
	/*
	 * If true, ptr points to a struct btrfs_block_group. Otherwise, ptr
	 * points to a struct btrfs_device.
	 */
	bool is_block_group;
	/*
	 * Only used when 'is_block_group' is true and it is the number of
	 * extents used by a swapfile for this block group ('ptr' field).
	 */
	int bg_extent_count;
};

bool btrfs_pinned_by_swapfile(struct btrfs_fs_info *fs_info, void *ptr);

enum {
	BTRFS_FS_CLOSING_START,
	BTRFS_FS_CLOSING_DONE,
	BTRFS_FS_LOG_RECOVERING,
	BTRFS_FS_OPEN,
	BTRFS_FS_QUOTA_ENABLED,
	BTRFS_FS_UPDATE_UUID_TREE_GEN,
	BTRFS_FS_CREATING_FREE_SPACE_TREE,
	BTRFS_FS_BTREE_ERR,
	BTRFS_FS_LOG1_ERR,
	BTRFS_FS_LOG2_ERR,
	BTRFS_FS_QUOTA_OVERRIDE,
	/* Used to record internally whether fs has been frozen */
	BTRFS_FS_FROZEN,
	/*
	 * Indicate that balance has been set up from the ioctl and is in the
	 * main phase. The fs_info::balance_ctl is initialized.
	 */
	BTRFS_FS_BALANCE_RUNNING,

	/*
	 * Indicate that relocation of a chunk has started, it's set per chunk
	 * and is toggled between chunks.
	 */
	BTRFS_FS_RELOC_RUNNING,

	/* Indicate that the cleaner thread is awake and doing something. */
	BTRFS_FS_CLEANER_RUNNING,

	/*
	 * The checksumming has an optimized version and is considered fast,
	 * so we don't need to offload checksums to workqueues.
	 */
	BTRFS_FS_CSUM_IMPL_FAST,

	/* Indicate that the discard workqueue can service discards. */
	BTRFS_FS_DISCARD_RUNNING,

	/* Indicate that we need to cleanup space cache v1 */
	BTRFS_FS_CLEANUP_SPACE_CACHE_V1,

	/* Indicate that we can't trust the free space tree for caching yet */
	BTRFS_FS_FREE_SPACE_TREE_UNTRUSTED,

	/* Indicate whether there are any tree modification log users */
	BTRFS_FS_TREE_MOD_LOG_USERS,

	/* Indicate that we want the transaction kthread to commit right now. */
	BTRFS_FS_COMMIT_TRANS,

	/* Indicate we have half completed snapshot deletions pending. */
	BTRFS_FS_UNFINISHED_DROPS,

#if BITS_PER_LONG == 32
	/* Indicate if we have error/warn message printed on 32bit systems */
	BTRFS_FS_32BIT_ERROR,
	BTRFS_FS_32BIT_WARN,
#endif
};

/*
 * Exclusive operations (device replace, resize, device add/remove, balance)
 */
enum btrfs_exclusive_operation {
	BTRFS_EXCLOP_NONE,
	BTRFS_EXCLOP_BALANCE_PAUSED,
	BTRFS_EXCLOP_BALANCE,
	BTRFS_EXCLOP_DEV_ADD,
	BTRFS_EXCLOP_DEV_REMOVE,
	BTRFS_EXCLOP_DEV_REPLACE,
	BTRFS_EXCLOP_RESIZE,
	BTRFS_EXCLOP_SWAP_ACTIVATE,
};

struct btrfs_fs_info {
	u8 chunk_tree_uuid[BTRFS_UUID_SIZE];
	unsigned long flags;
	struct btrfs_root *tree_root;
	struct btrfs_root *chunk_root;
	struct btrfs_root *dev_root;
	struct btrfs_root *fs_root;
	struct btrfs_root *quota_root;
	struct btrfs_root *uuid_root;
	struct btrfs_root *data_reloc_root;

	/* the log root tree is a directory of all the other log roots */
	struct btrfs_root *log_root_tree;

	/* The tree that holds the global roots (csum, extent, etc) */
	rwlock_t global_root_lock;
	struct rb_root global_root_tree;

	spinlock_t fs_roots_radix_lock;
	struct radix_tree_root fs_roots_radix;

	/* block group cache stuff */
	spinlock_t block_group_cache_lock;
	u64 first_logical_byte;
	struct rb_root block_group_cache_tree;

	/* keep track of unallocated space */
	atomic64_t free_chunk_space;

	/* Track ranges which are used by log trees blocks/logged data extents */
	struct extent_io_tree excluded_extents;

	/* logical->physical extent mapping */
	struct extent_map_tree mapping_tree;

	/*
	 * block reservation for extent, checksum, root tree and
	 * delayed dir index item
	 */
	struct btrfs_block_rsv global_block_rsv;
	/* block reservation for metadata operations */
	struct btrfs_block_rsv trans_block_rsv;
	/* block reservation for chunk tree */
	struct btrfs_block_rsv chunk_block_rsv;
	/* block reservation for delayed operations */
	struct btrfs_block_rsv delayed_block_rsv;
	/* block reservation for delayed refs */
	struct btrfs_block_rsv delayed_refs_rsv;

	struct btrfs_block_rsv empty_block_rsv;

	u64 generation;
	u64 last_trans_committed;
	/*
	 * Generation of the last transaction used for block group relocation
	 * since the filesystem was last mounted (or 0 if none happened yet).
	 * Must be written and read while holding btrfs_fs_info::commit_root_sem.
	 */
	u64 last_reloc_trans;
	u64 avg_delayed_ref_runtime;

	/*
	 * this is updated to the current trans every time a full commit
	 * is required instead of the faster short fsync log commits
	 */
	u64 last_trans_log_full_commit;
	unsigned long mount_opt;
	/*
	 * Track requests for actions that need to be done during transaction
	 * commit (like for some mount options).
	 */
	unsigned long pending_changes;
	unsigned long compress_type:4;
	unsigned int compress_level;
	u32 commit_interval;
	/*
	 * It is a suggestive number, the read side is safe even it gets a
	 * wrong number because we will write out the data into a regular
	 * extent. The write side(mount/remount) is under ->s_umount lock,
	 * so it is also safe.
	 */
	u64 max_inline;

	struct btrfs_transaction *running_transaction;
	wait_queue_head_t transaction_throttle;
	wait_queue_head_t transaction_wait;
	wait_queue_head_t transaction_blocked_wait;
	wait_queue_head_t async_submit_wait;

	/*
	 * Used to protect the incompat_flags, compat_flags, compat_ro_flags
	 * when they are updated.
	 *
	 * Because we do not clear the flags for ever, so we needn't use
	 * the lock on the read side.
	 *
	 * We also needn't use the lock when we mount the fs, because
	 * there is no other task which will update the flag.
	 */
	spinlock_t super_lock;
	struct btrfs_super_block *super_copy;
	struct btrfs_super_block *super_for_commit;
	struct super_block *sb;
	struct inode *btree_inode;
	struct mutex tree_log_mutex;
	struct mutex transaction_kthread_mutex;
	struct mutex cleaner_mutex;
	struct mutex chunk_mutex;

	/*
	 * this is taken to make sure we don't set block groups ro after
	 * the free space cache has been allocated on them
	 */
	struct mutex ro_block_group_mutex;

	/* this is used during read/modify/write to make sure
	 * no two ios are trying to mod the same stripe at the same
	 * time
	 */
	struct btrfs_stripe_hash_table *stripe_hash_table;

	/*
	 * this protects the ordered operations list only while we are
	 * processing all of the entries on it.  This way we make
	 * sure the commit code doesn't find the list temporarily empty
	 * because another function happens to be doing non-waiting preflush
	 * before jumping into the main commit.
	 */
	struct mutex ordered_operations_mutex;

	struct rw_semaphore commit_root_sem;

	struct rw_semaphore cleanup_work_sem;

	struct rw_semaphore subvol_sem;

	spinlock_t trans_lock;
	/*
	 * the reloc mutex goes with the trans lock, it is taken
	 * during commit to protect us from the relocation code
	 */
	struct mutex reloc_mutex;

	struct list_head trans_list;
	struct list_head dead_roots;
	struct list_head caching_block_groups;

	spinlock_t delayed_iput_lock;
	struct list_head delayed_iputs;
	atomic_t nr_delayed_iputs;
	wait_queue_head_t delayed_iputs_wait;

	atomic64_t tree_mod_seq;

	/* this protects tree_mod_log and tree_mod_seq_list */
	rwlock_t tree_mod_log_lock;
	struct rb_root tree_mod_log;
	struct list_head tree_mod_seq_list;

	atomic_t async_delalloc_pages;

	/*
	 * this is used to protect the following list -- ordered_roots.
	 */
	spinlock_t ordered_root_lock;

	/*
	 * all fs/file tree roots in which there are data=ordered extents
	 * pending writeback are added into this list.
	 *
	 * these can span multiple transactions and basically include
	 * every dirty data page that isn't from nodatacow
	 */
	struct list_head ordered_roots;

	struct mutex delalloc_root_mutex;
	spinlock_t delalloc_root_lock;
	/* all fs/file tree roots that have delalloc inodes. */
	struct list_head delalloc_roots;

	/*
	 * there is a pool of worker threads for checksumming during writes
	 * and a pool for checksumming after reads.  This is because readers
	 * can run with FS locks held, and the writers may be waiting for
	 * those locks.  We don't want ordering in the pending list to cause
	 * deadlocks, and so the two are serviced separately.
	 *
	 * A third pool does submit_bio to avoid deadlocking with the other
	 * two
	 */
	struct btrfs_workqueue *workers;
	struct btrfs_workqueue *delalloc_workers;
	struct btrfs_workqueue *flush_workers;
	struct btrfs_workqueue *endio_workers;
	struct btrfs_workqueue *endio_meta_workers;
	struct btrfs_workqueue *endio_raid56_workers;
	struct btrfs_workqueue *rmw_workers;
	struct btrfs_workqueue *endio_meta_write_workers;
	struct btrfs_workqueue *endio_write_workers;
	struct btrfs_workqueue *endio_freespace_worker;
	struct btrfs_workqueue *caching_workers;

	/*
	 * fixup workers take dirty pages that didn't properly go through
	 * the cow mechanism and make them safe to write.  It happens
	 * for the sys_munmap function call path
	 */
	struct btrfs_workqueue *fixup_workers;
	struct btrfs_workqueue *delayed_workers;

	struct task_struct *transaction_kthread;
	struct task_struct *cleaner_kthread;
	u32 thread_pool_size;

	struct kobject *space_info_kobj;
	struct kobject *qgroups_kobj;

	/* used to keep from writing metadata until there is a nice batch */
	struct percpu_counter dirty_metadata_bytes;
	struct percpu_counter delalloc_bytes;
	struct percpu_counter ordered_bytes;
	s32 dirty_metadata_batch;
	s32 delalloc_batch;

	struct list_head dirty_cowonly_roots;

	struct btrfs_fs_devices *fs_devices;

	/*
	 * The space_info list is effectively read only after initial
	 * setup.  It is populated at mount time and cleaned up after
	 * all block groups are removed.  RCU is used to protect it.
	 */
	struct list_head space_info;

	struct btrfs_space_info *data_sinfo;

	struct reloc_control *reloc_ctl;

	/* data_alloc_cluster is only used in ssd_spread mode */
	struct btrfs_free_cluster data_alloc_cluster;

	/* all metadata allocations go through this cluster */
	struct btrfs_free_cluster meta_alloc_cluster;

	/* auto defrag inodes go here */
	spinlock_t defrag_inodes_lock;
	struct rb_root defrag_inodes;
	atomic_t defrag_running;

	/* Used to protect avail_{data, metadata, system}_alloc_bits */
	seqlock_t profiles_lock;
	/*
	 * these three are in extended format (availability of single
	 * chunks is denoted by BTRFS_AVAIL_ALLOC_BIT_SINGLE bit, other
	 * types are denoted by corresponding BTRFS_BLOCK_GROUP_* bits)
	 */
	u64 avail_data_alloc_bits;
	u64 avail_metadata_alloc_bits;
	u64 avail_system_alloc_bits;

	/* restriper state */
	spinlock_t balance_lock;
	struct mutex balance_mutex;
	atomic_t balance_pause_req;
	atomic_t balance_cancel_req;
	struct btrfs_balance_control *balance_ctl;
	wait_queue_head_t balance_wait_q;

	/* Cancellation requests for chunk relocation */
	atomic_t reloc_cancel_req;

	u32 data_chunk_allocations;
	u32 metadata_ratio;

	void *bdev_holder;

	/* private scrub information */
	struct mutex scrub_lock;
	atomic_t scrubs_running;
	atomic_t scrub_pause_req;
	atomic_t scrubs_paused;
	atomic_t scrub_cancel_req;
	wait_queue_head_t scrub_pause_wait;
	/*
	 * The worker pointers are NULL iff the refcount is 0, ie. scrub is not
	 * running.
	 */
	refcount_t scrub_workers_refcnt;
	struct btrfs_workqueue *scrub_workers;
	struct btrfs_workqueue *scrub_wr_completion_workers;
	struct btrfs_workqueue *scrub_parity_workers;
	struct btrfs_subpage_info *subpage_info;

	struct btrfs_discard_ctl discard_ctl;

#ifdef CONFIG_BTRFS_FS_CHECK_INTEGRITY
	u32 check_integrity_print_mask;
#endif
	/* is qgroup tracking in a consistent state? */
	u64 qgroup_flags;

	/* holds configuration and tracking. Protected by qgroup_lock */
	struct rb_root qgroup_tree;
	spinlock_t qgroup_lock;

	/*
	 * used to avoid frequently calling ulist_alloc()/ulist_free()
	 * when doing qgroup accounting, it must be protected by qgroup_lock.
	 */
	struct ulist *qgroup_ulist;

	/*
	 * Protect user change for quota operations. If a transaction is needed,
	 * it must be started before locking this lock.
	 */
	struct mutex qgroup_ioctl_lock;

	/* list of dirty qgroups to be written at next commit */
	struct list_head dirty_qgroups;

	/* used by qgroup for an efficient tree traversal */
	u64 qgroup_seq;

	/* qgroup rescan items */
	struct mutex qgroup_rescan_lock; /* protects the progress item */
	struct btrfs_key qgroup_rescan_progress;
	struct btrfs_workqueue *qgroup_rescan_workers;
	struct completion qgroup_rescan_completion;
	struct btrfs_work qgroup_rescan_work;
	bool qgroup_rescan_running;	/* protected by qgroup_rescan_lock */

	/* filesystem state */
	unsigned long fs_state;

	struct btrfs_delayed_root *delayed_root;

	/* Extent buffer radix tree */
	spinlock_t buffer_lock;
	/* Entries are eb->start / sectorsize */
	struct radix_tree_root buffer_radix;

	/* next backup root to be overwritten */
	int backup_root_index;

	/* device replace state */
	struct btrfs_dev_replace dev_replace;

	struct semaphore uuid_tree_rescan_sem;

	/* Used to reclaim the metadata space in the background. */
	struct work_struct async_reclaim_work;
	struct work_struct async_data_reclaim_work;
	struct work_struct preempt_reclaim_work;

	/* Reclaim partially filled block groups in the background */
	struct work_struct reclaim_bgs_work;
	struct list_head reclaim_bgs;
	int bg_reclaim_threshold;

	spinlock_t unused_bgs_lock;
	struct list_head unused_bgs;
	struct mutex unused_bg_unpin_mutex;
	/* Protect block groups that are going to be deleted */
	struct mutex reclaim_bgs_lock;

	/* Cached block sizes */
	u32 nodesize;
	u32 sectorsize;
	/* ilog2 of sectorsize, use to avoid 64bit division */
	u32 sectorsize_bits;
	u32 csum_size;
	u32 csums_per_leaf;
	u32 stripesize;

	/* Block groups and devices containing active swapfiles. */
	spinlock_t swapfile_pins_lock;
	struct rb_root swapfile_pins;

	struct crypto_shash *csum_shash;

	/* Type of exclusive operation running, protected by super_lock */
	enum btrfs_exclusive_operation exclusive_operation;

	/*
	 * Zone size > 0 when in ZONED mode, otherwise it's used for a check
	 * if the mode is enabled
	 */
	union {
		u64 zone_size;
		u64 zoned;
	};

	struct mutex zoned_meta_io_lock;
	spinlock_t treelog_bg_lock;
	u64 treelog_bg;

	/*
	 * Start of the dedicated data relocation block group, protected by
	 * relocation_bg_lock.
	 */
	spinlock_t relocation_bg_lock;
	u64 data_reloc_bg;

	spinlock_t zone_active_bgs_lock;
	struct list_head zone_active_bgs;

#ifdef CONFIG_BTRFS_FS_REF_VERIFY
	spinlock_t ref_verify_lock;
	struct rb_root block_tree;
#endif

#ifdef CONFIG_BTRFS_DEBUG
	struct kobject *debug_kobj;
	struct kobject *discard_debug_kobj;
	struct list_head allocated_roots;

	spinlock_t eb_leak_lock;
	struct list_head allocated_ebs;
#endif
};

static inline struct btrfs_fs_info *btrfs_sb(struct super_block *sb)
{
	return sb->s_fs_info;
}

/*
 * The state of btrfs root
 */
enum {
	/*
	 * btrfs_record_root_in_trans is a multi-step process, and it can race
	 * with the balancing code.   But the race is very small, and only the
	 * first time the root is added to each transaction.  So IN_TRANS_SETUP
	 * is used to tell us when more checks are required
	 */
	BTRFS_ROOT_IN_TRANS_SETUP,

	/*
	 * Set if tree blocks of this root can be shared by other roots.
	 * Only subvolume trees and their reloc trees have this bit set.
	 * Conflicts with TRACK_DIRTY bit.
	 *
	 * This affects two things:
	 *
	 * - How balance works
	 *   For shareable roots, we need to use reloc tree and do path
	 *   replacement for balance, and need various pre/post hooks for
	 *   snapshot creation to handle them.
	 *
	 *   While for non-shareable trees, we just simply do a tree search
	 *   with COW.
	 *
	 * - How dirty roots are tracked
	 *   For shareable roots, btrfs_record_root_in_trans() is needed to
	 *   track them, while non-subvolume roots have TRACK_DIRTY bit, they
	 *   don't need to set this manually.
	 */
	BTRFS_ROOT_SHAREABLE,
	BTRFS_ROOT_TRACK_DIRTY,
	BTRFS_ROOT_IN_RADIX,
	BTRFS_ROOT_ORPHAN_ITEM_INSERTED,
	BTRFS_ROOT_DEFRAG_RUNNING,
	BTRFS_ROOT_FORCE_COW,
	BTRFS_ROOT_MULTI_LOG_TASKS,
	BTRFS_ROOT_DIRTY,
	BTRFS_ROOT_DELETING,

	/*
	 * Reloc tree is orphan, only kept here for qgroup delayed subtree scan
	 *
	 * Set for the subvolume tree owning the reloc tree.
	 */
	BTRFS_ROOT_DEAD_RELOC_TREE,
	/* Mark dead root stored on device whose cleanup needs to be resumed */
	BTRFS_ROOT_DEAD_TREE,
	/* The root has a log tree. Used for subvolume roots and the tree root. */
	BTRFS_ROOT_HAS_LOG_TREE,
	/* Qgroup flushing is in progress */
	BTRFS_ROOT_QGROUP_FLUSHING,
	/* We started the orphan cleanup for this root. */
	BTRFS_ROOT_ORPHAN_CLEANUP,
	/* This root has a drop operation that was started previously. */
	BTRFS_ROOT_UNFINISHED_DROP,
};

static inline void btrfs_wake_unfinished_drop(struct btrfs_fs_info *fs_info)
{
	clear_and_wake_up_bit(BTRFS_FS_UNFINISHED_DROPS, &fs_info->flags);
}

/*
 * Record swapped tree blocks of a subvolume tree for delayed subtree trace
 * code. For detail check comment in fs/btrfs/qgroup.c.
 */
struct btrfs_qgroup_swapped_blocks {
	spinlock_t lock;
	/* RM_EMPTY_ROOT() of above blocks[] */
	bool swapped;
	struct rb_root blocks[BTRFS_MAX_LEVEL];
};

/*
 * in ram representation of the tree.  extent_root is used for all allocations
 * and for the extent tree extent_root root.
 */
struct btrfs_root {
	struct rb_node rb_node;

	struct extent_buffer *node;

	struct extent_buffer *commit_root;
	struct btrfs_root *log_root;
	struct btrfs_root *reloc_root;

	unsigned long state;
	struct btrfs_root_item root_item;
	struct btrfs_key root_key;
	struct btrfs_fs_info *fs_info;
	struct extent_io_tree dirty_log_pages;

	struct mutex objectid_mutex;

	spinlock_t accounting_lock;
	struct btrfs_block_rsv *block_rsv;

	struct mutex log_mutex;
	wait_queue_head_t log_writer_wait;
	wait_queue_head_t log_commit_wait[2];
	struct list_head log_ctxs[2];
	/* Used only for log trees of subvolumes, not for the log root tree */
	atomic_t log_writers;
	atomic_t log_commit[2];
	/* Used only for log trees of subvolumes, not for the log root tree */
	atomic_t log_batch;
	int log_transid;
	/* No matter the commit succeeds or not*/
	int log_transid_committed;
	/* Just be updated when the commit succeeds. */
	int last_log_commit;
	pid_t log_start_pid;

	u64 last_trans;

	u32 type;

	u64 free_objectid;

	struct btrfs_key defrag_progress;
	struct btrfs_key defrag_max;

	/* The dirty list is only used by non-shareable roots */
	struct list_head dirty_list;

	struct list_head root_list;

	spinlock_t log_extents_lock[2];
	struct list_head logged_list[2];

	spinlock_t inode_lock;
	/* red-black tree that keeps track of in-memory inodes */
	struct rb_root inode_tree;

	/*
	 * radix tree that keeps track of delayed nodes of every inode,
	 * protected by inode_lock
	 */
	struct radix_tree_root delayed_nodes_tree;
	/*
	 * right now this just gets used so that a root has its own devid
	 * for stat.  It may be used for more later
	 */
	dev_t anon_dev;

	spinlock_t root_item_lock;
	refcount_t refs;

	struct mutex delalloc_mutex;
	spinlock_t delalloc_lock;
	/*
	 * all of the inodes that have delalloc bytes.  It is possible for
	 * this list to be empty even when there is still dirty data=ordered
	 * extents waiting to finish IO.
	 */
	struct list_head delalloc_inodes;
	struct list_head delalloc_root;
	u64 nr_delalloc_inodes;

	struct mutex ordered_extent_mutex;
	/*
	 * this is used by the balancing code to wait for all the pending
	 * ordered extents
	 */
	spinlock_t ordered_extent_lock;

	/*
	 * all of the data=ordered extents pending writeback
	 * these can span multiple transactions and basically include
	 * every dirty data page that isn't from nodatacow
	 */
	struct list_head ordered_extents;
	struct list_head ordered_root;
	u64 nr_ordered_extents;

	/*
	 * Not empty if this subvolume root has gone through tree block swap
	 * (relocation)
	 *
	 * Will be used by reloc_control::dirty_subvol_roots.
	 */
	struct list_head reloc_dirty_list;

	/*
	 * Number of currently running SEND ioctls to prevent
	 * manipulation with the read-only status via SUBVOL_SETFLAGS
	 */
	int send_in_progress;
	/*
	 * Number of currently running deduplication operations that have a
	 * destination inode belonging to this root. Protected by the lock
	 * root_item_lock.
	 */
	int dedupe_in_progress;
	/* For exclusion of snapshot creation and nocow writes */
	struct btrfs_drew_lock snapshot_lock;

	atomic_t snapshot_force_cow;

	/* For qgroup metadata reserved space */
	spinlock_t qgroup_meta_rsv_lock;
	u64 qgroup_meta_rsv_pertrans;
	u64 qgroup_meta_rsv_prealloc;
	wait_queue_head_t qgroup_flush_wait;

	/* Number of active swapfiles */
	atomic_t nr_swapfiles;

	/* Record pairs of swapped blocks for qgroup */
	struct btrfs_qgroup_swapped_blocks swapped_blocks;

	/* Used only by log trees, when logging csum items */
	struct extent_io_tree log_csum_range;

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
	u64 alloc_bytenr;
#endif

#ifdef CONFIG_BTRFS_DEBUG
	struct list_head leak_list;
#endif
};

/*
 * Structure that conveys information about an extent that is going to replace
 * all the extents in a file range.
 */
struct btrfs_replace_extent_info {
	u64 disk_offset;
	u64 disk_len;
	u64 data_offset;
	u64 data_len;
	u64 file_offset;
	/* Pointer to a file extent item of type regular or prealloc. */
	char *extent_buf;
	/*
	 * Set to true when attempting to replace a file range with a new extent
	 * described by this structure, set to false when attempting to clone an
	 * existing extent into a file range.
	 */
	bool is_new_extent;
	/* Meaningful only if is_new_extent is true. */
	int qgroup_reserved;
	/*
	 * Meaningful only if is_new_extent is true.
	 * Used to track how many extent items we have already inserted in a
	 * subvolume tree that refer to the extent described by this structure,
	 * so that we know when to create a new delayed ref or update an existing
	 * one.
	 */
	int insertions;
};

/* Arguments for btrfs_drop_extents() */
struct btrfs_drop_extents_args {
	/* Input parameters */

	/*
	 * If NULL, btrfs_drop_extents() will allocate and free its own path.
	 * If 'replace_extent' is true, this must not be NULL. Also the path
	 * is always released except if 'replace_extent' is true and
	 * btrfs_drop_extents() sets 'extent_inserted' to true, in which case
	 * the path is kept locked.
	 */
	struct btrfs_path *path;
	/* Start offset of the range to drop extents from */
	u64 start;
	/* End (exclusive, last byte + 1) of the range to drop extents from */
	u64 end;
	/* If true drop all the extent maps in the range */
	bool drop_cache;
	/*
	 * If true it means we want to insert a new extent after dropping all
	 * the extents in the range. If this is true, the 'extent_item_size'
	 * parameter must be set as well and the 'extent_inserted' field will
	 * be set to true by btrfs_drop_extents() if it could insert the new
	 * extent.
	 * Note: when this is set to true the path must not be NULL.
	 */
	bool replace_extent;
	/*
	 * Used if 'replace_extent' is true. Size of the file extent item to
	 * insert after dropping all existing extents in the range
	 */
	u32 extent_item_size;

	/* Output parameters */

	/*
	 * Set to the minimum between the input parameter 'end' and the end
	 * (exclusive, last byte + 1) of the last dropped extent. This is always
	 * set even if btrfs_drop_extents() returns an error.
	 */
	u64 drop_end;
	/*
	 * The number of allocated bytes found in the range. This can be smaller
	 * than the range's length when there are holes in the range.
	 */
	u64 bytes_found;
	/*
	 * Only set if 'replace_extent' is true. Set to true if we were able
	 * to insert a replacement extent after dropping all extents in the
	 * range, otherwise set to false by btrfs_drop_extents().
	 * Also, if btrfs_drop_extents() has set this to true it means it
	 * returned with the path locked, otherwise if it has set this to
	 * false it has returned with the path released.
	 */
	bool extent_inserted;
};

struct btrfs_file_private {
	void *filldir_buf;
};


static inline u32 BTRFS_LEAF_DATA_SIZE(const struct btrfs_fs_info *info)
{

	return info->nodesize - sizeof(struct btrfs_header);
}

#define BTRFS_LEAF_DATA_OFFSET		offsetof(struct btrfs_leaf, items)

static inline u32 BTRFS_MAX_ITEM_SIZE(const struct btrfs_fs_info *info)
{
	return BTRFS_LEAF_DATA_SIZE(info) - sizeof(struct btrfs_item);
}

static inline u32 BTRFS_NODEPTRS_PER_BLOCK(const struct btrfs_fs_info *info)
{
	return BTRFS_LEAF_DATA_SIZE(info) / sizeof(struct btrfs_key_ptr);
}

#define BTRFS_FILE_EXTENT_INLINE_DATA_START		\
		(offsetof(struct btrfs_file_extent_item, disk_bytenr))
static inline u32 BTRFS_MAX_INLINE_DATA_SIZE(const struct btrfs_fs_info *info)
{
	return BTRFS_MAX_ITEM_SIZE(info) -
	       BTRFS_FILE_EXTENT_INLINE_DATA_START;
}

static inline u32 BTRFS_MAX_XATTR_SIZE(const struct btrfs_fs_info *info)
{
	return BTRFS_MAX_ITEM_SIZE(info) - sizeof(struct btrfs_dir_item);
}

/*
 * Flags for mount options.
 *
 * Note: don't forget to add new options to btrfs_show_options()
 */
enum {
	BTRFS_MOUNT_NODATASUM			= (1UL << 0),
	BTRFS_MOUNT_NODATACOW			= (1UL << 1),
	BTRFS_MOUNT_NOBARRIER			= (1UL << 2),
	BTRFS_MOUNT_SSD				= (1UL << 3),
	BTRFS_MOUNT_DEGRADED			= (1UL << 4),
	BTRFS_MOUNT_COMPRESS			= (1UL << 5),
	BTRFS_MOUNT_NOTREELOG   		= (1UL << 6),
	BTRFS_MOUNT_FLUSHONCOMMIT		= (1UL << 7),
	BTRFS_MOUNT_SSD_SPREAD			= (1UL << 8),
	BTRFS_MOUNT_NOSSD			= (1UL << 9),
	BTRFS_MOUNT_DISCARD_SYNC		= (1UL << 10),
	BTRFS_MOUNT_FORCE_COMPRESS      	= (1UL << 11),
	BTRFS_MOUNT_SPACE_CACHE			= (1UL << 12),
	BTRFS_MOUNT_CLEAR_CACHE			= (1UL << 13),
	BTRFS_MOUNT_USER_SUBVOL_RM_ALLOWED	= (1UL << 14),
	BTRFS_MOUNT_ENOSPC_DEBUG		= (1UL << 15),
	BTRFS_MOUNT_AUTO_DEFRAG			= (1UL << 16),
	BTRFS_MOUNT_USEBACKUPROOT		= (1UL << 17),
	BTRFS_MOUNT_SKIP_BALANCE		= (1UL << 18),
	BTRFS_MOUNT_CHECK_INTEGRITY		= (1UL << 19),
	BTRFS_MOUNT_CHECK_INTEGRITY_DATA	= (1UL << 20),
	BTRFS_MOUNT_PANIC_ON_FATAL_ERROR	= (1UL << 21),
	BTRFS_MOUNT_RESCAN_UUID_TREE		= (1UL << 22),
	BTRFS_MOUNT_FRAGMENT_DATA		= (1UL << 23),
	BTRFS_MOUNT_FRAGMENT_METADATA		= (1UL << 24),
	BTRFS_MOUNT_FREE_SPACE_TREE		= (1UL << 25),
	BTRFS_MOUNT_NOLOGREPLAY			= (1UL << 26),
	BTRFS_MOUNT_REF_VERIFY			= (1UL << 27),
	BTRFS_MOUNT_DISCARD_ASYNC		= (1UL << 28),
	BTRFS_MOUNT_IGNOREBADROOTS		= (1UL << 29),
	BTRFS_MOUNT_IGNOREDATACSUMS		= (1UL << 30),
};

#define BTRFS_DEFAULT_COMMIT_INTERVAL	(30)
#define BTRFS_DEFAULT_MAX_INLINE	(2048)

#define btrfs_clear_opt(o, opt)		((o) &= ~BTRFS_MOUNT_##opt)
#define btrfs_set_opt(o, opt)		((o) |= BTRFS_MOUNT_##opt)
#define btrfs_raw_test_opt(o, opt)	((o) & BTRFS_MOUNT_##opt)
#define btrfs_test_opt(fs_info, opt)	((fs_info)->mount_opt & \
					 BTRFS_MOUNT_##opt)

#define btrfs_set_and_info(fs_info, opt, fmt, args...)			\
do {									\
	if (!btrfs_test_opt(fs_info, opt))				\
		btrfs_info(fs_info, fmt, ##args);			\
	btrfs_set_opt(fs_info->mount_opt, opt);				\
} while (0)

#define btrfs_clear_and_info(fs_info, opt, fmt, args...)		\
do {									\
	if (btrfs_test_opt(fs_info, opt))				\
		btrfs_info(fs_info, fmt, ##args);			\
	btrfs_clear_opt(fs_info->mount_opt, opt);			\
} while (0)

/*
 * Requests for changes that need to be done during transaction commit.
 *
 * Internal mount options that are used for special handling of the real
 * mount options (eg. cannot be set during remount and have to be set during
 * transaction commit)
 */

#define BTRFS_PENDING_COMMIT			(0)

#define btrfs_test_pending(info, opt)	\
	test_bit(BTRFS_PENDING_##opt, &(info)->pending_changes)
#define btrfs_set_pending(info, opt)	\
	set_bit(BTRFS_PENDING_##opt, &(info)->pending_changes)
#define btrfs_clear_pending(info, opt)	\
	clear_bit(BTRFS_PENDING_##opt, &(info)->pending_changes)

/*
 * Helpers for setting pending mount option changes.
 *
 * Expects corresponding macros
 * BTRFS_PENDING_SET_ and CLEAR_ + short mount option name
 */
#define btrfs_set_pending_and_info(info, opt, fmt, args...)            \
do {                                                                   \
       if (!btrfs_raw_test_opt((info)->mount_opt, opt)) {              \
               btrfs_info((info), fmt, ##args);                        \
               btrfs_set_pending((info), SET_##opt);                   \
               btrfs_clear_pending((info), CLEAR_##opt);               \
       }                                                               \
} while(0)

#define btrfs_clear_pending_and_info(info, opt, fmt, args...)          \
do {                                                                   \
       if (btrfs_raw_test_opt((info)->mount_opt, opt)) {               \
               btrfs_info((info), fmt, ##args);                        \
               btrfs_set_pending((info), CLEAR_##opt);                 \
               btrfs_clear_pending((info), SET_##opt);                 \
       }                                                               \
} while(0)

/*
 * Inode flags
 */
#define BTRFS_INODE_NODATASUM		(1U << 0)
#define BTRFS_INODE_NODATACOW		(1U << 1)
#define BTRFS_INODE_READONLY		(1U << 2)
#define BTRFS_INODE_NOCOMPRESS		(1U << 3)
#define BTRFS_INODE_PREALLOC		(1U << 4)
#define BTRFS_INODE_SYNC		(1U << 5)
#define BTRFS_INODE_IMMUTABLE		(1U << 6)
#define BTRFS_INODE_APPEND		(1U << 7)
#define BTRFS_INODE_NODUMP		(1U << 8)
#define BTRFS_INODE_NOATIME		(1U << 9)
#define BTRFS_INODE_DIRSYNC		(1U << 10)
#define BTRFS_INODE_COMPRESS		(1U << 11)

#define BTRFS_INODE_ROOT_ITEM_INIT	(1U << 31)

#define BTRFS_INODE_FLAG_MASK						\
	(BTRFS_INODE_NODATASUM |					\
	 BTRFS_INODE_NODATACOW |					\
	 BTRFS_INODE_READONLY |						\
	 BTRFS_INODE_NOCOMPRESS |					\
	 BTRFS_INODE_PREALLOC |						\
	 BTRFS_INODE_SYNC |						\
	 BTRFS_INODE_IMMUTABLE |					\
	 BTRFS_INODE_APPEND |						\
	 BTRFS_INODE_NODUMP |						\
	 BTRFS_INODE_NOATIME |						\
	 BTRFS_INODE_DIRSYNC |						\
	 BTRFS_INODE_COMPRESS |						\
	 BTRFS_INODE_ROOT_ITEM_INIT)

#define BTRFS_INODE_RO_VERITY		(1U << 0)

#define BTRFS_INODE_RO_FLAG_MASK	(BTRFS_INODE_RO_VERITY)

struct btrfs_map_token {
	struct extent_buffer *eb;
	char *kaddr;
	unsigned long offset;
};

#define BTRFS_BYTES_TO_BLKS(fs_info, bytes) \
				((bytes) >> (fs_info)->sectorsize_bits)

static inline void btrfs_init_map_token(struct btrfs_map_token *token,
					struct extent_buffer *eb)
{
	token->eb = eb;
	token->kaddr = page_address(eb->pages[0]);
	token->offset = 0;
}

/* some macros to generate set/get functions for the struct fields.  This
 * assumes there is a lefoo_to_cpu for every type, so lets make a simple
 * one for u8:
 */
#define le8_to_cpu(v) (v)
#define cpu_to_le8(v) (v)
#define __le8 u8

static inline u8 get_unaligned_le8(const void *p)
{
       return *(u8 *)p;
}

static inline void put_unaligned_le8(u8 val, void *p)
{
       *(u8 *)p = val;
}

#define read_eb_member(eb, ptr, type, member, result) (\
	read_extent_buffer(eb, (char *)(result),			\
			   ((unsigned long)(ptr)) +			\
			    offsetof(type, member),			\
			   sizeof(((type *)0)->member)))

#define write_eb_member(eb, ptr, type, member, result) (\
	write_extent_buffer(eb, (char *)(result),			\
			   ((unsigned long)(ptr)) +			\
			    offsetof(type, member),			\
			   sizeof(((type *)0)->member)))

#define DECLARE_BTRFS_SETGET_BITS(bits)					\
u##bits btrfs_get_token_##bits(struct btrfs_map_token *token,		\
			       const void *ptr, unsigned long off);	\
void btrfs_set_token_##bits(struct btrfs_map_token *token,		\
			    const void *ptr, unsigned long off,		\
			    u##bits val);				\
u##bits btrfs_get_##bits(const struct extent_buffer *eb,		\
			 const void *ptr, unsigned long off);		\
void btrfs_set_##bits(const struct extent_buffer *eb, void *ptr,	\
		      unsigned long off, u##bits val);

DECLARE_BTRFS_SETGET_BITS(8)
DECLARE_BTRFS_SETGET_BITS(16)
DECLARE_BTRFS_SETGET_BITS(32)
DECLARE_BTRFS_SETGET_BITS(64)

#define BTRFS_SETGET_FUNCS(name, type, member, bits)			\
static inline u##bits btrfs_##name(const struct extent_buffer *eb,	\
				   const type *s)			\
{									\
	BUILD_BUG_ON(sizeof(u##bits) != sizeof(((type *)0))->member);	\
	return btrfs_get_##bits(eb, s, offsetof(type, member));		\
}									\
static inline void btrfs_set_##name(const struct extent_buffer *eb, type *s, \
				    u##bits val)			\
{									\
	BUILD_BUG_ON(sizeof(u##bits) != sizeof(((type *)0))->member);	\
	btrfs_set_##bits(eb, s, offsetof(type, member), val);		\
}									\
static inline u##bits btrfs_token_##name(struct btrfs_map_token *token,	\
					 const type *s)			\
{									\
	BUILD_BUG_ON(sizeof(u##bits) != sizeof(((type *)0))->member);	\
	return btrfs_get_token_##bits(token, s, offsetof(type, member));\
}									\
static inline void btrfs_set_token_##name(struct btrfs_map_token *token,\
					  type *s, u##bits val)		\
{									\
	BUILD_BUG_ON(sizeof(u##bits) != sizeof(((type *)0))->member);	\
	btrfs_set_token_##bits(token, s, offsetof(type, member), val);	\
}

#define BTRFS_SETGET_HEADER_FUNCS(name, type, member, bits)		\
static inline u##bits btrfs_##name(const struct extent_buffer *eb)	\
{									\
	const type *p = page_address(eb->pages[0]) +			\
			offset_in_page(eb->start);			\
	return get_unaligned_le##bits(&p->member);			\
}									\
static inline void btrfs_set_##name(const struct extent_buffer *eb,	\
				    u##bits val)			\
{									\
	type *p = page_address(eb->pages[0]) + offset_in_page(eb->start); \
	put_unaligned_le##bits(val, &p->member);			\
}

#define BTRFS_SETGET_STACK_FUNCS(name, type, member, bits)		\
static inline u##bits btrfs_##name(const type *s)			\
{									\
	return get_unaligned_le##bits(&s->member);			\
}									\
static inline void btrfs_set_##name(type *s, u##bits val)		\
{									\
	put_unaligned_le##bits(val, &s->member);			\
}

static inline u64 btrfs_device_total_bytes(const struct extent_buffer *eb,
					   struct btrfs_dev_item *s)
{
	BUILD_BUG_ON(sizeof(u64) !=
		     sizeof(((struct btrfs_dev_item *)0))->total_bytes);
	return btrfs_get_64(eb, s, offsetof(struct btrfs_dev_item,
					    total_bytes));
}
static inline void btrfs_set_device_total_bytes(const struct extent_buffer *eb,
						struct btrfs_dev_item *s,
						u64 val)
{
	BUILD_BUG_ON(sizeof(u64) !=
		     sizeof(((struct btrfs_dev_item *)0))->total_bytes);
	WARN_ON(!IS_ALIGNED(val, eb->fs_info->sectorsize));
	btrfs_set_64(eb, s, offsetof(struct btrfs_dev_item, total_bytes), val);
}


BTRFS_SETGET_FUNCS(device_type, struct btrfs_dev_item, type, 64);
BTRFS_SETGET_FUNCS(device_bytes_used, struct btrfs_dev_item, bytes_used, 64);
BTRFS_SETGET_FUNCS(device_io_align, struct btrfs_dev_item, io_align, 32);
BTRFS_SETGET_FUNCS(device_io_width, struct btrfs_dev_item, io_width, 32);
BTRFS_SETGET_FUNCS(device_start_offset, struct btrfs_dev_item,
		   start_offset, 64);
BTRFS_SETGET_FUNCS(device_sector_size, struct btrfs_dev_item, sector_size, 32);
BTRFS_SETGET_FUNCS(device_id, struct btrfs_dev_item, devid, 64);
BTRFS_SETGET_FUNCS(device_group, struct btrfs_dev_item, dev_group, 32);
BTRFS_SETGET_FUNCS(device_seek_speed, struct btrfs_dev_item, seek_speed, 8);
BTRFS_SETGET_FUNCS(device_bandwidth, struct btrfs_dev_item, bandwidth, 8);
BTRFS_SETGET_FUNCS(device_generation, struct btrfs_dev_item, generation, 64);

BTRFS_SETGET_STACK_FUNCS(stack_device_type, struct btrfs_dev_item, type, 64);
BTRFS_SETGET_STACK_FUNCS(stack_device_total_bytes, struct btrfs_dev_item,
			 total_bytes, 64);
BTRFS_SETGET_STACK_FUNCS(stack_device_bytes_used, struct btrfs_dev_item,
			 bytes_used, 64);
BTRFS_SETGET_STACK_FUNCS(stack_device_io_align, struct btrfs_dev_item,
			 io_align, 32);
BTRFS_SETGET_STACK_FUNCS(stack_device_io_width, struct btrfs_dev_item,
			 io_width, 32);
BTRFS_SETGET_STACK_FUNCS(stack_device_sector_size, struct btrfs_dev_item,
			 sector_size, 32);
BTRFS_SETGET_STACK_FUNCS(stack_device_id, struct btrfs_dev_item, devid, 64);
BTRFS_SETGET_STACK_FUNCS(stack_device_group, struct btrfs_dev_item,
			 dev_group, 32);
BTRFS_SETGET_STACK_FUNCS(stack_device_seek_speed, struct btrfs_dev_item,
			 seek_speed, 8);
BTRFS_SETGET_STACK_FUNCS(stack_device_bandwidth, struct btrfs_dev_item,
			 bandwidth, 8);
BTRFS_SETGET_STACK_FUNCS(stack_device_generation, struct btrfs_dev_item,
			 generation, 64);

static inline unsigned long btrfs_device_uuid(struct btrfs_dev_item *d)
{
	return (unsigned long)d + offsetof(struct btrfs_dev_item, uuid);
}

static inline unsigned long btrfs_device_fsid(struct btrfs_dev_item *d)
{
	return (unsigned long)d + offsetof(struct btrfs_dev_item, fsid);
}

BTRFS_SETGET_FUNCS(chunk_length, struct btrfs_chunk, length, 64);
BTRFS_SETGET_FUNCS(chunk_owner, struct btrfs_chunk, owner, 64);
BTRFS_SETGET_FUNCS(chunk_stripe_len, struct btrfs_chunk, stripe_len, 64);
BTRFS_SETGET_FUNCS(chunk_io_align, struct btrfs_chunk, io_align, 32);
BTRFS_SETGET_FUNCS(chunk_io_width, struct btrfs_chunk, io_width, 32);
BTRFS_SETGET_FUNCS(chunk_sector_size, struct btrfs_chunk, sector_size, 32);
BTRFS_SETGET_FUNCS(chunk_type, struct btrfs_chunk, type, 64);
BTRFS_SETGET_FUNCS(chunk_num_stripes, struct btrfs_chunk, num_stripes, 16);
BTRFS_SETGET_FUNCS(chunk_sub_stripes, struct btrfs_chunk, sub_stripes, 16);
BTRFS_SETGET_FUNCS(stripe_devid, struct btrfs_stripe, devid, 64);
BTRFS_SETGET_FUNCS(stripe_offset, struct btrfs_stripe, offset, 64);

static inline char *btrfs_stripe_dev_uuid(struct btrfs_stripe *s)
{
	return (char *)s + offsetof(struct btrfs_stripe, dev_uuid);
}

BTRFS_SETGET_STACK_FUNCS(stack_chunk_length, struct btrfs_chunk, length, 64);
BTRFS_SETGET_STACK_FUNCS(stack_chunk_owner, struct btrfs_chunk, owner, 64);
BTRFS_SETGET_STACK_FUNCS(stack_chunk_stripe_len, struct btrfs_chunk,
			 stripe_len, 64);
BTRFS_SETGET_STACK_FUNCS(stack_chunk_io_align, struct btrfs_chunk,
			 io_align, 32);
BTRFS_SETGET_STACK_FUNCS(stack_chunk_io_width, struct btrfs_chunk,
			 io_width, 32);
BTRFS_SETGET_STACK_FUNCS(stack_chunk_sector_size, struct btrfs_chunk,
			 sector_size, 32);
BTRFS_SETGET_STACK_FUNCS(stack_chunk_type, struct btrfs_chunk, type, 64);
BTRFS_SETGET_STACK_FUNCS(stack_chunk_num_stripes, struct btrfs_chunk,
			 num_stripes, 16);
BTRFS_SETGET_STACK_FUNCS(stack_chunk_sub_stripes, struct btrfs_chunk,
			 sub_stripes, 16);
BTRFS_SETGET_STACK_FUNCS(stack_stripe_devid, struct btrfs_stripe, devid, 64);
BTRFS_SETGET_STACK_FUNCS(stack_stripe_offset, struct btrfs_stripe, offset, 64);

static inline struct btrfs_stripe *btrfs_stripe_nr(struct btrfs_chunk *c,
						   int nr)
{
	unsigned long offset = (unsigned long)c;
	offset += offsetof(struct btrfs_chunk, stripe);
	offset += nr * sizeof(struct btrfs_stripe);
	return (struct btrfs_stripe *)offset;
}

static inline char *btrfs_stripe_dev_uuid_nr(struct btrfs_chunk *c, int nr)
{
	return btrfs_stripe_dev_uuid(btrfs_stripe_nr(c, nr));
}

static inline u64 btrfs_stripe_offset_nr(const struct extent_buffer *eb,
					 struct btrfs_chunk *c, int nr)
{
	return btrfs_stripe_offset(eb, btrfs_stripe_nr(c, nr));
}

static inline u64 btrfs_stripe_devid_nr(const struct extent_buffer *eb,
					 struct btrfs_chunk *c, int nr)
{
	return btrfs_stripe_devid(eb, btrfs_stripe_nr(c, nr));
}

/* struct btrfs_block_group_item */
BTRFS_SETGET_STACK_FUNCS(stack_block_group_used, struct btrfs_block_group_item,
			 used, 64);
BTRFS_SETGET_FUNCS(block_group_used, struct btrfs_block_group_item,
			 used, 64);
BTRFS_SETGET_STACK_FUNCS(stack_block_group_chunk_objectid,
			struct btrfs_block_group_item, chunk_objectid, 64);

BTRFS_SETGET_FUNCS(block_group_chunk_objectid,
		   struct btrfs_block_group_item, chunk_objectid, 64);
BTRFS_SETGET_FUNCS(block_group_flags,
		   struct btrfs_block_group_item, flags, 64);
BTRFS_SETGET_STACK_FUNCS(stack_block_group_flags,
			struct btrfs_block_group_item, flags, 64);

/* struct btrfs_free_space_info */
BTRFS_SETGET_FUNCS(free_space_extent_count, struct btrfs_free_space_info,
		   extent_count, 32);
BTRFS_SETGET_FUNCS(free_space_flags, struct btrfs_free_space_info, flags, 32);

/* struct btrfs_inode_ref */
BTRFS_SETGET_FUNCS(inode_ref_name_len, struct btrfs_inode_ref, name_len, 16);
BTRFS_SETGET_FUNCS(inode_ref_index, struct btrfs_inode_ref, index, 64);

/* struct btrfs_inode_extref */
BTRFS_SETGET_FUNCS(inode_extref_parent, struct btrfs_inode_extref,
		   parent_objectid, 64);
BTRFS_SETGET_FUNCS(inode_extref_name_len, struct btrfs_inode_extref,
		   name_len, 16);
BTRFS_SETGET_FUNCS(inode_extref_index, struct btrfs_inode_extref, index, 64);

/* struct btrfs_inode_item */
BTRFS_SETGET_FUNCS(inode_generation, struct btrfs_inode_item, generation, 64);
BTRFS_SETGET_FUNCS(inode_sequence, struct btrfs_inode_item, sequence, 64);
BTRFS_SETGET_FUNCS(inode_transid, struct btrfs_inode_item, transid, 64);
BTRFS_SETGET_FUNCS(inode_size, struct btrfs_inode_item, size, 64);
BTRFS_SETGET_FUNCS(inode_nbytes, struct btrfs_inode_item, nbytes, 64);
BTRFS_SETGET_FUNCS(inode_block_group, struct btrfs_inode_item, block_group, 64);
BTRFS_SETGET_FUNCS(inode_nlink, struct btrfs_inode_item, nlink, 32);
BTRFS_SETGET_FUNCS(inode_uid, struct btrfs_inode_item, uid, 32);
BTRFS_SETGET_FUNCS(inode_gid, struct btrfs_inode_item, gid, 32);
BTRFS_SETGET_FUNCS(inode_mode, struct btrfs_inode_item, mode, 32);
BTRFS_SETGET_FUNCS(inode_rdev, struct btrfs_inode_item, rdev, 64);
BTRFS_SETGET_FUNCS(inode_flags, struct btrfs_inode_item, flags, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_generation, struct btrfs_inode_item,
			 generation, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_sequence, struct btrfs_inode_item,
			 sequence, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_transid, struct btrfs_inode_item,
			 transid, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_size, struct btrfs_inode_item, size, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_nbytes, struct btrfs_inode_item,
			 nbytes, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_block_group, struct btrfs_inode_item,
			 block_group, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_nlink, struct btrfs_inode_item, nlink, 32);
BTRFS_SETGET_STACK_FUNCS(stack_inode_uid, struct btrfs_inode_item, uid, 32);
BTRFS_SETGET_STACK_FUNCS(stack_inode_gid, struct btrfs_inode_item, gid, 32);
BTRFS_SETGET_STACK_FUNCS(stack_inode_mode, struct btrfs_inode_item, mode, 32);
BTRFS_SETGET_STACK_FUNCS(stack_inode_rdev, struct btrfs_inode_item, rdev, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_flags, struct btrfs_inode_item, flags, 64);
BTRFS_SETGET_FUNCS(timespec_sec, struct btrfs_timespec, sec, 64);
BTRFS_SETGET_FUNCS(timespec_nsec, struct btrfs_timespec, nsec, 32);
BTRFS_SETGET_STACK_FUNCS(stack_timespec_sec, struct btrfs_timespec, sec, 64);
BTRFS_SETGET_STACK_FUNCS(stack_timespec_nsec, struct btrfs_timespec, nsec, 32);

/* struct btrfs_dev_extent */
BTRFS_SETGET_FUNCS(dev_extent_chunk_tree, struct btrfs_dev_extent,
		   chunk_tree, 64);
BTRFS_SETGET_FUNCS(dev_extent_chunk_objectid, struct btrfs_dev_extent,
		   chunk_objectid, 64);
BTRFS_SETGET_FUNCS(dev_extent_chunk_offset, struct btrfs_dev_extent,
		   chunk_offset, 64);
BTRFS_SETGET_FUNCS(dev_extent_length, struct btrfs_dev_extent, length, 64);
BTRFS_SETGET_FUNCS(extent_refs, struct btrfs_extent_item, refs, 64);
BTRFS_SETGET_FUNCS(extent_generation, struct btrfs_extent_item,
		   generation, 64);
BTRFS_SETGET_FUNCS(extent_flags, struct btrfs_extent_item, flags, 64);

BTRFS_SETGET_FUNCS(tree_block_level, struct btrfs_tree_block_info, level, 8);

static inline void btrfs_tree_block_key(const struct extent_buffer *eb,
					struct btrfs_tree_block_info *item,
					struct btrfs_disk_key *key)
{
	read_eb_member(eb, item, struct btrfs_tree_block_info, key, key);
}

static inline void btrfs_set_tree_block_key(const struct extent_buffer *eb,
					    struct btrfs_tree_block_info *item,
					    struct btrfs_disk_key *key)
{
	write_eb_member(eb, item, struct btrfs_tree_block_info, key, key);
}

BTRFS_SETGET_FUNCS(extent_data_ref_root, struct btrfs_extent_data_ref,
		   root, 64);
BTRFS_SETGET_FUNCS(extent_data_ref_objectid, struct btrfs_extent_data_ref,
		   objectid, 64);
BTRFS_SETGET_FUNCS(extent_data_ref_offset, struct btrfs_extent_data_ref,
		   offset, 64);
BTRFS_SETGET_FUNCS(extent_data_ref_count, struct btrfs_extent_data_ref,
		   count, 32);

BTRFS_SETGET_FUNCS(shared_data_ref_count, struct btrfs_shared_data_ref,
		   count, 32);

BTRFS_SETGET_FUNCS(extent_inline_ref_type, struct btrfs_extent_inline_ref,
		   type, 8);
BTRFS_SETGET_FUNCS(extent_inline_ref_offset, struct btrfs_extent_inline_ref,
		   offset, 64);

static inline u32 btrfs_extent_inline_ref_size(int type)
{
	if (type == BTRFS_TREE_BLOCK_REF_KEY ||
	    type == BTRFS_SHARED_BLOCK_REF_KEY)
		return sizeof(struct btrfs_extent_inline_ref);
	if (type == BTRFS_SHARED_DATA_REF_KEY)
		return sizeof(struct btrfs_shared_data_ref) +
		       sizeof(struct btrfs_extent_inline_ref);
	if (type == BTRFS_EXTENT_DATA_REF_KEY)
		return sizeof(struct btrfs_extent_data_ref) +
		       offsetof(struct btrfs_extent_inline_ref, offset);
	return 0;
}

/* struct btrfs_node */
BTRFS_SETGET_FUNCS(key_blockptr, struct btrfs_key_ptr, blockptr, 64);
BTRFS_SETGET_FUNCS(key_generation, struct btrfs_key_ptr, generation, 64);
BTRFS_SETGET_STACK_FUNCS(stack_key_blockptr, struct btrfs_key_ptr,
			 blockptr, 64);
BTRFS_SETGET_STACK_FUNCS(stack_key_generation, struct btrfs_key_ptr,
			 generation, 64);

static inline u64 btrfs_node_blockptr(const struct extent_buffer *eb, int nr)
{
	unsigned long ptr;
	ptr = offsetof(struct btrfs_node, ptrs) +
		sizeof(struct btrfs_key_ptr) * nr;
	return btrfs_key_blockptr(eb, (struct btrfs_key_ptr *)ptr);
}

static inline void btrfs_set_node_blockptr(const struct extent_buffer *eb,
					   int nr, u64 val)
{
	unsigned long ptr;
	ptr = offsetof(struct btrfs_node, ptrs) +
		sizeof(struct btrfs_key_ptr) * nr;
	btrfs_set_key_blockptr(eb, (struct btrfs_key_ptr *)ptr, val);
}

static inline u64 btrfs_node_ptr_generation(const struct extent_buffer *eb, int nr)
{
	unsigned long ptr;
	ptr = offsetof(struct btrfs_node, ptrs) +
		sizeof(struct btrfs_key_ptr) * nr;
	return btrfs_key_generation(eb, (struct btrfs_key_ptr *)ptr);
}

static inline void btrfs_set_node_ptr_generation(const struct extent_buffer *eb,
						 int nr, u64 val)
{
	unsigned long ptr;
	ptr = offsetof(struct btrfs_node, ptrs) +
		sizeof(struct btrfs_key_ptr) * nr;
	btrfs_set_key_generation(eb, (struct btrfs_key_ptr *)ptr, val);
}

static inline unsigned long btrfs_node_key_ptr_offset(int nr)
{
	return offsetof(struct btrfs_node, ptrs) +
		sizeof(struct btrfs_key_ptr) * nr;
}

void btrfs_node_key(const struct extent_buffer *eb,
		    struct btrfs_disk_key *disk_key, int nr);

static inline void btrfs_set_node_key(const struct extent_buffer *eb,
				      struct btrfs_disk_key *disk_key, int nr)
{
	unsigned long ptr;
	ptr = btrfs_node_key_ptr_offset(nr);
	write_eb_member(eb, (struct btrfs_key_ptr *)ptr,
		       struct btrfs_key_ptr, key, disk_key);
}

/* struct btrfs_item */
BTRFS_SETGET_FUNCS(raw_item_offset, struct btrfs_item, offset, 32);
BTRFS_SETGET_FUNCS(raw_item_size, struct btrfs_item, size, 32);
BTRFS_SETGET_STACK_FUNCS(stack_item_offset, struct btrfs_item, offset, 32);
BTRFS_SETGET_STACK_FUNCS(stack_item_size, struct btrfs_item, size, 32);

static inline unsigned long btrfs_item_nr_offset(int nr)
{
	return offsetof(struct btrfs_leaf, items) +
		sizeof(struct btrfs_item) * nr;
}

static inline struct btrfs_item *btrfs_item_nr(int nr)
{
	return (struct btrfs_item *)btrfs_item_nr_offset(nr);
}

#define BTRFS_ITEM_SETGET_FUNCS(member)						\
static inline u32 btrfs_item_##member(const struct extent_buffer *eb,		\
				      int slot)					\
{										\
	return btrfs_raw_item_##member(eb, btrfs_item_nr(slot));		\
}										\
static inline void btrfs_set_item_##member(const struct extent_buffer *eb,	\
					   int slot, u32 val)			\
{										\
	btrfs_set_raw_item_##member(eb, btrfs_item_nr(slot), val);		\
}										\
static inline u32 btrfs_token_item_##member(struct btrfs_map_token *token,	\
					    int slot)				\
{										\
	struct btrfs_item *item = btrfs_item_nr(slot);				\
	return btrfs_token_raw_item_##member(token, item);			\
}										\
static inline void btrfs_set_token_item_##member(struct btrfs_map_token *token,	\
						 int slot, u32 val)		\
{										\
	struct btrfs_item *item = btrfs_item_nr(slot);				\
	btrfs_set_token_raw_item_##member(token, item, val);			\
}

BTRFS_ITEM_SETGET_FUNCS(offset)
BTRFS_ITEM_SETGET_FUNCS(size);

static inline u32 btrfs_item_data_end(const struct extent_buffer *eb, int nr)
{
	return btrfs_item_offset(eb, nr) + btrfs_item_size(eb, nr);
}

static inline void btrfs_item_key(const struct extent_buffer *eb,
			   struct btrfs_disk_key *disk_key, int nr)
{
	struct btrfs_item *item = btrfs_item_nr(nr);
	read_eb_member(eb, item, struct btrfs_item, key, disk_key);
}

static inline void btrfs_set_item_key(struct extent_buffer *eb,
			       struct btrfs_disk_key *disk_key, int nr)
{
	struct btrfs_item *item = btrfs_item_nr(nr);
	write_eb_member(eb, item, struct btrfs_item, key, disk_key);
}

BTRFS_SETGET_FUNCS(dir_log_end, struct btrfs_dir_log_item, end, 64);

/*
 * struct btrfs_root_ref
 */
BTRFS_SETGET_FUNCS(root_ref_dirid, struct btrfs_root_ref, dirid, 64);
BTRFS_SETGET_FUNCS(root_ref_sequence, struct btrfs_root_ref, sequence, 64);
BTRFS_SETGET_FUNCS(root_ref_name_len, struct btrfs_root_ref, name_len, 16);

/* struct btrfs_dir_item */
BTRFS_SETGET_FUNCS(dir_data_len, struct btrfs_dir_item, data_len, 16);
BTRFS_SETGET_FUNCS(dir_type, struct btrfs_dir_item, type, 8);
BTRFS_SETGET_FUNCS(dir_name_len, struct btrfs_dir_item, name_len, 16);
BTRFS_SETGET_FUNCS(dir_transid, struct btrfs_dir_item, transid, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dir_type, struct btrfs_dir_item, type, 8);
BTRFS_SETGET_STACK_FUNCS(stack_dir_data_len, struct btrfs_dir_item,
			 data_len, 16);
BTRFS_SETGET_STACK_FUNCS(stack_dir_name_len, struct btrfs_dir_item,
			 name_len, 16);
BTRFS_SETGET_STACK_FUNCS(stack_dir_transid, struct btrfs_dir_item,
			 transid, 64);

static inline void btrfs_dir_item_key(const struct extent_buffer *eb,
				      const struct btrfs_dir_item *item,
				      struct btrfs_disk_key *key)
{
	read_eb_member(eb, item, struct btrfs_dir_item, location, key);
}

static inline void btrfs_set_dir_item_key(struct extent_buffer *eb,
					  struct btrfs_dir_item *item,
					  const struct btrfs_disk_key *key)
{
	write_eb_member(eb, item, struct btrfs_dir_item, location, key);
}

BTRFS_SETGET_FUNCS(free_space_entries, struct btrfs_free_space_header,
		   num_entries, 64);
BTRFS_SETGET_FUNCS(free_space_bitmaps, struct btrfs_free_space_header,
		   num_bitmaps, 64);
BTRFS_SETGET_FUNCS(free_space_generation, struct btrfs_free_space_header,
		   generation, 64);

static inline void btrfs_free_space_key(const struct extent_buffer *eb,
					const struct btrfs_free_space_header *h,
					struct btrfs_disk_key *key)
{
	read_eb_member(eb, h, struct btrfs_free_space_header, location, key);
}

static inline void btrfs_set_free_space_key(struct extent_buffer *eb,
					    struct btrfs_free_space_header *h,
					    const struct btrfs_disk_key *key)
{
	write_eb_member(eb, h, struct btrfs_free_space_header, location, key);
}

/* struct btrfs_disk_key */
BTRFS_SETGET_STACK_FUNCS(disk_key_objectid, struct btrfs_disk_key,
			 objectid, 64);
BTRFS_SETGET_STACK_FUNCS(disk_key_offset, struct btrfs_disk_key, offset, 64);
BTRFS_SETGET_STACK_FUNCS(disk_key_type, struct btrfs_disk_key, type, 8);

#ifdef __LITTLE_ENDIAN

/*
 * Optimized helpers for little-endian architectures where CPU and on-disk
 * structures have the same endianness and we can skip conversions.
 */

static inline void btrfs_disk_key_to_cpu(struct btrfs_key *cpu_key,
					 const struct btrfs_disk_key *disk_key)
{
	memcpy(cpu_key, disk_key, sizeof(struct btrfs_key));
}

static inline void btrfs_cpu_key_to_disk(struct btrfs_disk_key *disk_key,
					 const struct btrfs_key *cpu_key)
{
	memcpy(disk_key, cpu_key, sizeof(struct btrfs_key));
}

static inline void btrfs_node_key_to_cpu(const struct extent_buffer *eb,
					 struct btrfs_key *cpu_key, int nr)
{
	struct btrfs_disk_key *disk_key = (struct btrfs_disk_key *)cpu_key;

	btrfs_node_key(eb, disk_key, nr);
}

static inline void btrfs_item_key_to_cpu(const struct extent_buffer *eb,
					 struct btrfs_key *cpu_key, int nr)
{
	struct btrfs_disk_key *disk_key = (struct btrfs_disk_key *)cpu_key;

	btrfs_item_key(eb, disk_key, nr);
}

static inline void btrfs_dir_item_key_to_cpu(const struct extent_buffer *eb,
					     const struct btrfs_dir_item *item,
					     struct btrfs_key *cpu_key)
{
	struct btrfs_disk_key *disk_key = (struct btrfs_disk_key *)cpu_key;

	btrfs_dir_item_key(eb, item, disk_key);
}

#else

static inline void btrfs_disk_key_to_cpu(struct btrfs_key *cpu,
					 const struct btrfs_disk_key *disk)
{
	cpu->offset = le64_to_cpu(disk->offset);
	cpu->type = disk->type;
	cpu->objectid = le64_to_cpu(disk->objectid);
}

static inline void btrfs_cpu_key_to_disk(struct btrfs_disk_key *disk,
					 const struct btrfs_key *cpu)
{
	disk->offset = cpu_to_le64(cpu->offset);
	disk->type = cpu->type;
	disk->objectid = cpu_to_le64(cpu->objectid);
}

static inline void btrfs_node_key_to_cpu(const struct extent_buffer *eb,
					 struct btrfs_key *key, int nr)
{
	struct btrfs_disk_key disk_key;
	btrfs_node_key(eb, &disk_key, nr);
	btrfs_disk_key_to_cpu(key, &disk_key);
}

static inline void btrfs_item_key_to_cpu(const struct extent_buffer *eb,
					 struct btrfs_key *key, int nr)
{
	struct btrfs_disk_key disk_key;
	btrfs_item_key(eb, &disk_key, nr);
	btrfs_disk_key_to_cpu(key, &disk_key);
}

static inline void btrfs_dir_item_key_to_cpu(const struct extent_buffer *eb,
					     const struct btrfs_dir_item *item,
					     struct btrfs_key *key)
{
	struct btrfs_disk_key disk_key;
	btrfs_dir_item_key(eb, item, &disk_key);
	btrfs_disk_key_to_cpu(key, &disk_key);
}

#endif

/* struct btrfs_header */
BTRFS_SETGET_HEADER_FUNCS(header_bytenr, struct btrfs_header, bytenr, 64);
BTRFS_SETGET_HEADER_FUNCS(header_generation, struct btrfs_header,
			  generation, 64);
BTRFS_SETGET_HEADER_FUNCS(header_owner, struct btrfs_header, owner, 64);
BTRFS_SETGET_HEADER_FUNCS(header_nritems, struct btrfs_header, nritems, 32);
BTRFS_SETGET_HEADER_FUNCS(header_flags, struct btrfs_header, flags, 64);
BTRFS_SETGET_HEADER_FUNCS(header_level, struct btrfs_header, level, 8);
BTRFS_SETGET_STACK_FUNCS(stack_header_generation, struct btrfs_header,
			 generation, 64);
BTRFS_SETGET_STACK_FUNCS(stack_header_owner, struct btrfs_header, owner, 64);
BTRFS_SETGET_STACK_FUNCS(stack_header_nritems, struct btrfs_header,
			 nritems, 32);
BTRFS_SETGET_STACK_FUNCS(stack_header_bytenr, struct btrfs_header, bytenr, 64);

static inline int btrfs_header_flag(const struct extent_buffer *eb, u64 flag)
{
	return (btrfs_header_flags(eb) & flag) == flag;
}

static inline void btrfs_set_header_flag(struct extent_buffer *eb, u64 flag)
{
	u64 flags = btrfs_header_flags(eb);
	btrfs_set_header_flags(eb, flags | flag);
}

static inline void btrfs_clear_header_flag(struct extent_buffer *eb, u64 flag)
{
	u64 flags = btrfs_header_flags(eb);
	btrfs_set_header_flags(eb, flags & ~flag);
}

static inline int btrfs_header_backref_rev(const struct extent_buffer *eb)
{
	u64 flags = btrfs_header_flags(eb);
	return flags >> BTRFS_BACKREF_REV_SHIFT;
}

static inline void btrfs_set_header_backref_rev(struct extent_buffer *eb,
						int rev)
{
	u64 flags = btrfs_header_flags(eb);
	flags &= ~BTRFS_BACKREF_REV_MASK;
	flags |= (u64)rev << BTRFS_BACKREF_REV_SHIFT;
	btrfs_set_header_flags(eb, flags);
}

static inline int btrfs_is_leaf(const struct extent_buffer *eb)
{
	return btrfs_header_level(eb) == 0;
}

/* struct btrfs_root_item */
BTRFS_SETGET_FUNCS(disk_root_generation, struct btrfs_root_item,
		   generation, 64);
BTRFS_SETGET_FUNCS(disk_root_refs, struct btrfs_root_item, refs, 32);
BTRFS_SETGET_FUNCS(disk_root_bytenr, struct btrfs_root_item, bytenr, 64);
BTRFS_SETGET_FUNCS(disk_root_level, struct btrfs_root_item, level, 8);

BTRFS_SETGET_STACK_FUNCS(root_generation, struct btrfs_root_item,
			 generation, 64);
BTRFS_SETGET_STACK_FUNCS(root_bytenr, struct btrfs_root_item, bytenr, 64);
BTRFS_SETGET_STACK_FUNCS(root_drop_level, struct btrfs_root_item, drop_level, 8);
BTRFS_SETGET_STACK_FUNCS(root_level, struct btrfs_root_item, level, 8);
BTRFS_SETGET_STACK_FUNCS(root_dirid, struct btrfs_root_item, root_dirid, 64);
BTRFS_SETGET_STACK_FUNCS(root_refs, struct btrfs_root_item, refs, 32);
BTRFS_SETGET_STACK_FUNCS(root_flags, struct btrfs_root_item, flags, 64);
BTRFS_SETGET_STACK_FUNCS(root_used, struct btrfs_root_item, bytes_used, 64);
BTRFS_SETGET_STACK_FUNCS(root_limit, struct btrfs_root_item, byte_limit, 64);
BTRFS_SETGET_STACK_FUNCS(root_last_snapshot, struct btrfs_root_item,
			 last_snapshot, 64);
BTRFS_SETGET_STACK_FUNCS(root_generation_v2, struct btrfs_root_item,
			 generation_v2, 64);
BTRFS_SETGET_STACK_FUNCS(root_ctransid, struct btrfs_root_item,
			 ctransid, 64);
BTRFS_SETGET_STACK_FUNCS(root_otransid, struct btrfs_root_item,
			 otransid, 64);
BTRFS_SETGET_STACK_FUNCS(root_stransid, struct btrfs_root_item,
			 stransid, 64);
BTRFS_SETGET_STACK_FUNCS(root_rtransid, struct btrfs_root_item,
			 rtransid, 64);

static inline bool btrfs_root_readonly(const struct btrfs_root *root)
{
	/* Byte-swap the constant at compile time, root_item::flags is LE */
	return (root->root_item.flags & cpu_to_le64(BTRFS_ROOT_SUBVOL_RDONLY)) != 0;
}

static inline bool btrfs_root_dead(const struct btrfs_root *root)
{
	/* Byte-swap the constant at compile time, root_item::flags is LE */
	return (root->root_item.flags & cpu_to_le64(BTRFS_ROOT_SUBVOL_DEAD)) != 0;
}

static inline u64 btrfs_root_id(const struct btrfs_root *root)
{
	return root->root_key.objectid;
}

/* struct btrfs_root_backup */
BTRFS_SETGET_STACK_FUNCS(backup_tree_root, struct btrfs_root_backup,
		   tree_root, 64);
BTRFS_SETGET_STACK_FUNCS(backup_tree_root_gen, struct btrfs_root_backup,
		   tree_root_gen, 64);
BTRFS_SETGET_STACK_FUNCS(backup_tree_root_level, struct btrfs_root_backup,
		   tree_root_level, 8);

BTRFS_SETGET_STACK_FUNCS(backup_chunk_root, struct btrfs_root_backup,
		   chunk_root, 64);
BTRFS_SETGET_STACK_FUNCS(backup_chunk_root_gen, struct btrfs_root_backup,
		   chunk_root_gen, 64);
BTRFS_SETGET_STACK_FUNCS(backup_chunk_root_level, struct btrfs_root_backup,
		   chunk_root_level, 8);

BTRFS_SETGET_STACK_FUNCS(backup_extent_root, struct btrfs_root_backup,
		   extent_root, 64);
BTRFS_SETGET_STACK_FUNCS(backup_extent_root_gen, struct btrfs_root_backup,
		   extent_root_gen, 64);
BTRFS_SETGET_STACK_FUNCS(backup_extent_root_level, struct btrfs_root_backup,
		   extent_root_level, 8);

BTRFS_SETGET_STACK_FUNCS(backup_fs_root, struct btrfs_root_backup,
		   fs_root, 64);
BTRFS_SETGET_STACK_FUNCS(backup_fs_root_gen, struct btrfs_root_backup,
		   fs_root_gen, 64);
BTRFS_SETGET_STACK_FUNCS(backup_fs_root_level, struct btrfs_root_backup,
		   fs_root_level, 8);

BTRFS_SETGET_STACK_FUNCS(backup_dev_root, struct btrfs_root_backup,
		   dev_root, 64);
BTRFS_SETGET_STACK_FUNCS(backup_dev_root_gen, struct btrfs_root_backup,
		   dev_root_gen, 64);
BTRFS_SETGET_STACK_FUNCS(backup_dev_root_level, struct btrfs_root_backup,
		   dev_root_level, 8);

BTRFS_SETGET_STACK_FUNCS(backup_csum_root, struct btrfs_root_backup,
		   csum_root, 64);
BTRFS_SETGET_STACK_FUNCS(backup_csum_root_gen, struct btrfs_root_backup,
		   csum_root_gen, 64);
BTRFS_SETGET_STACK_FUNCS(backup_csum_root_level, struct btrfs_root_backup,
		   csum_root_level, 8);
BTRFS_SETGET_STACK_FUNCS(backup_total_bytes, struct btrfs_root_backup,
		   total_bytes, 64);
BTRFS_SETGET_STACK_FUNCS(backup_bytes_used, struct btrfs_root_backup,
		   bytes_used, 64);
BTRFS_SETGET_STACK_FUNCS(backup_num_devices, struct btrfs_root_backup,
		   num_devices, 64);

/* struct btrfs_balance_item */
BTRFS_SETGET_FUNCS(balance_flags, struct btrfs_balance_item, flags, 64);

static inline void btrfs_balance_data(const struct extent_buffer *eb,
				      const struct btrfs_balance_item *bi,
				      struct btrfs_disk_balance_args *ba)
{
	read_eb_member(eb, bi, struct btrfs_balance_item, data, ba);
}

static inline void btrfs_set_balance_data(struct extent_buffer *eb,
				  struct btrfs_balance_item *bi,
				  const struct btrfs_disk_balance_args *ba)
{
	write_eb_member(eb, bi, struct btrfs_balance_item, data, ba);
}

static inline void btrfs_balance_meta(const struct extent_buffer *eb,
				      const struct btrfs_balance_item *bi,
				      struct btrfs_disk_balance_args *ba)
{
	read_eb_member(eb, bi, struct btrfs_balance_item, meta, ba);
}

static inline void btrfs_set_balance_meta(struct extent_buffer *eb,
				  struct btrfs_balance_item *bi,
				  const struct btrfs_disk_balance_args *ba)
{
	write_eb_member(eb, bi, struct btrfs_balance_item, meta, ba);
}

static inline void btrfs_balance_sys(const struct extent_buffer *eb,
				     const struct btrfs_balance_item *bi,
				     struct btrfs_disk_balance_args *ba)
{
	read_eb_member(eb, bi, struct btrfs_balance_item, sys, ba);
}

static inline void btrfs_set_balance_sys(struct extent_buffer *eb,
				 struct btrfs_balance_item *bi,
				 const struct btrfs_disk_balance_args *ba)
{
	write_eb_member(eb, bi, struct btrfs_balance_item, sys, ba);
}

static inline void
btrfs_disk_balance_args_to_cpu(struct btrfs_balance_args *cpu,
			       const struct btrfs_disk_balance_args *disk)
{
	memset(cpu, 0, sizeof(*cpu));

	cpu->profiles = le64_to_cpu(disk->profiles);
	cpu->usage = le64_to_cpu(disk->usage);
	cpu->devid = le64_to_cpu(disk->devid);
	cpu->pstart = le64_to_cpu(disk->pstart);
	cpu->pend = le64_to_cpu(disk->pend);
	cpu->vstart = le64_to_cpu(disk->vstart);
	cpu->vend = le64_to_cpu(disk->vend);
	cpu->target = le64_to_cpu(disk->target);
	cpu->flags = le64_to_cpu(disk->flags);
	cpu->limit = le64_to_cpu(disk->limit);
	cpu->stripes_min = le32_to_cpu(disk->stripes_min);
	cpu->stripes_max = le32_to_cpu(disk->stripes_max);
}

static inline void
btrfs_cpu_balance_args_to_disk(struct btrfs_disk_balance_args *disk,
			       const struct btrfs_balance_args *cpu)
{
	memset(disk, 0, sizeof(*disk));

	disk->profiles = cpu_to_le64(cpu->profiles);
	disk->usage = cpu_to_le64(cpu->usage);
	disk->devid = cpu_to_le64(cpu->devid);
	disk->pstart = cpu_to_le64(cpu->pstart);
	disk->pend = cpu_to_le64(cpu->pend);
	disk->vstart = cpu_to_le64(cpu->vstart);
	disk->vend = cpu_to_le64(cpu->vend);
	disk->target = cpu_to_le64(cpu->target);
	disk->flags = cpu_to_le64(cpu->flags);
	disk->limit = cpu_to_le64(cpu->limit);
	disk->stripes_min = cpu_to_le32(cpu->stripes_min);
	disk->stripes_max = cpu_to_le32(cpu->stripes_max);
}

/* struct btrfs_super_block */
BTRFS_SETGET_STACK_FUNCS(super_bytenr, struct btrfs_super_block, bytenr, 64);
BTRFS_SETGET_STACK_FUNCS(super_flags, struct btrfs_super_block, flags, 64);
BTRFS_SETGET_STACK_FUNCS(super_generation, struct btrfs_super_block,
			 generation, 64);
BTRFS_SETGET_STACK_FUNCS(super_root, struct btrfs_super_block, root, 64);
BTRFS_SETGET_STACK_FUNCS(super_sys_array_size,
			 struct btrfs_super_block, sys_chunk_array_size, 32);
BTRFS_SETGET_STACK_FUNCS(super_chunk_root_generation,
			 struct btrfs_super_block, chunk_root_generation, 64);
BTRFS_SETGET_STACK_FUNCS(super_root_level, struct btrfs_super_block,
			 root_level, 8);
BTRFS_SETGET_STACK_FUNCS(super_chunk_root, struct btrfs_super_block,
			 chunk_root, 64);
BTRFS_SETGET_STACK_FUNCS(super_chunk_root_level, struct btrfs_super_block,
			 chunk_root_level, 8);
BTRFS_SETGET_STACK_FUNCS(super_log_root, struct btrfs_super_block,
			 log_root, 64);
BTRFS_SETGET_STACK_FUNCS(super_log_root_transid, struct btrfs_super_block,
			 log_root_transid, 64);
BTRFS_SETGET_STACK_FUNCS(super_log_root_level, struct btrfs_super_block,
			 log_root_level, 8);
BTRFS_SETGET_STACK_FUNCS(super_total_bytes, struct btrfs_super_block,
			 total_bytes, 64);
BTRFS_SETGET_STACK_FUNCS(super_bytes_used, struct btrfs_super_block,
			 bytes_used, 64);
BTRFS_SETGET_STACK_FUNCS(super_sectorsize, struct btrfs_super_block,
			 sectorsize, 32);
BTRFS_SETGET_STACK_FUNCS(super_nodesize, struct btrfs_super_block,
			 nodesize, 32);
BTRFS_SETGET_STACK_FUNCS(super_stripesize, struct btrfs_super_block,
			 stripesize, 32);
BTRFS_SETGET_STACK_FUNCS(super_root_dir, struct btrfs_super_block,
			 root_dir_objectid, 64);
BTRFS_SETGET_STACK_FUNCS(super_num_devices, struct btrfs_super_block,
			 num_devices, 64);
BTRFS_SETGET_STACK_FUNCS(super_compat_flags, struct btrfs_super_block,
			 compat_flags, 64);
BTRFS_SETGET_STACK_FUNCS(super_compat_ro_flags, struct btrfs_super_block,
			 compat_ro_flags, 64);
BTRFS_SETGET_STACK_FUNCS(super_incompat_flags, struct btrfs_super_block,
			 incompat_flags, 64);
BTRFS_SETGET_STACK_FUNCS(super_csum_type, struct btrfs_super_block,
			 csum_type, 16);
BTRFS_SETGET_STACK_FUNCS(super_cache_generation, struct btrfs_super_block,
			 cache_generation, 64);
BTRFS_SETGET_STACK_FUNCS(super_magic, struct btrfs_super_block, magic, 64);
BTRFS_SETGET_STACK_FUNCS(super_uuid_tree_generation, struct btrfs_super_block,
			 uuid_tree_generation, 64);

int btrfs_super_csum_size(const struct btrfs_super_block *s);
const char *btrfs_super_csum_name(u16 csum_type);
const char *btrfs_super_csum_driver(u16 csum_type);
size_t __attribute_const__ btrfs_get_num_csums(void);


/*
 * The leaf data grows from end-to-front in the node.
 * this returns the address of the start of the last item,
 * which is the stop of the leaf data stack
 */
static inline unsigned int leaf_data_end(const struct extent_buffer *leaf)
{
	u32 nr = btrfs_header_nritems(leaf);

	if (nr == 0)
		return BTRFS_LEAF_DATA_SIZE(leaf->fs_info);
	return btrfs_item_offset(leaf, nr - 1);
}

/* struct btrfs_file_extent_item */
BTRFS_SETGET_STACK_FUNCS(stack_file_extent_type, struct btrfs_file_extent_item,
			 type, 8);
BTRFS_SETGET_STACK_FUNCS(stack_file_extent_disk_bytenr,
			 struct btrfs_file_extent_item, disk_bytenr, 64);
BTRFS_SETGET_STACK_FUNCS(stack_file_extent_offset,
			 struct btrfs_file_extent_item, offset, 64);
BTRFS_SETGET_STACK_FUNCS(stack_file_extent_generation,
			 struct btrfs_file_extent_item, generation, 64);
BTRFS_SETGET_STACK_FUNCS(stack_file_extent_num_bytes,
			 struct btrfs_file_extent_item, num_bytes, 64);
BTRFS_SETGET_STACK_FUNCS(stack_file_extent_ram_bytes,
			 struct btrfs_file_extent_item, ram_bytes, 64);
BTRFS_SETGET_STACK_FUNCS(stack_file_extent_disk_num_bytes,
			 struct btrfs_file_extent_item, disk_num_bytes, 64);
BTRFS_SETGET_STACK_FUNCS(stack_file_extent_compression,
			 struct btrfs_file_extent_item, compression, 8);

static inline unsigned long
btrfs_file_extent_inline_start(const struct btrfs_file_extent_item *e)
{
	return (unsigned long)e + BTRFS_FILE_EXTENT_INLINE_DATA_START;
}

static inline u32 btrfs_file_extent_calc_inline_size(u32 datasize)
{
	return BTRFS_FILE_EXTENT_INLINE_DATA_START + datasize;
}

BTRFS_SETGET_FUNCS(file_extent_type, struct btrfs_file_extent_item, type, 8);
BTRFS_SETGET_FUNCS(file_extent_disk_bytenr, struct btrfs_file_extent_item,
		   disk_bytenr, 64);
BTRFS_SETGET_FUNCS(file_extent_generation, struct btrfs_file_extent_item,
		   generation, 64);
BTRFS_SETGET_FUNCS(file_extent_disk_num_bytes, struct btrfs_file_extent_item,
		   disk_num_bytes, 64);
BTRFS_SETGET_FUNCS(file_extent_offset, struct btrfs_file_extent_item,
		  offset, 64);
BTRFS_SETGET_FUNCS(file_extent_num_bytes, struct btrfs_file_extent_item,
		   num_bytes, 64);
BTRFS_SETGET_FUNCS(file_extent_ram_bytes, struct btrfs_file_extent_item,
		   ram_bytes, 64);
BTRFS_SETGET_FUNCS(file_extent_compression, struct btrfs_file_extent_item,
		   compression, 8);
BTRFS_SETGET_FUNCS(file_extent_encryption, struct btrfs_file_extent_item,
		   encryption, 8);
BTRFS_SETGET_FUNCS(file_extent_other_encoding, struct btrfs_file_extent_item,
		   other_encoding, 16);

/*
 * this returns the number of bytes used by the item on disk, minus the
 * size of any extent headers.  If a file is compressed on disk, this is
 * the compressed size
 */
static inline u32 btrfs_file_extent_inline_item_len(
						const struct extent_buffer *eb,
						int nr)
{
	return btrfs_item_size(eb, nr) - BTRFS_FILE_EXTENT_INLINE_DATA_START;
}

/* btrfs_qgroup_status_item */
BTRFS_SETGET_FUNCS(qgroup_status_generation, struct btrfs_qgroup_status_item,
		   generation, 64);
BTRFS_SETGET_FUNCS(qgroup_status_version, struct btrfs_qgroup_status_item,
		   version, 64);
BTRFS_SETGET_FUNCS(qgroup_status_flags, struct btrfs_qgroup_status_item,
		   flags, 64);
BTRFS_SETGET_FUNCS(qgroup_status_rescan, struct btrfs_qgroup_status_item,
		   rescan, 64);

/* btrfs_qgroup_info_item */
BTRFS_SETGET_FUNCS(qgroup_info_generation, struct btrfs_qgroup_info_item,
		   generation, 64);
BTRFS_SETGET_FUNCS(qgroup_info_rfer, struct btrfs_qgroup_info_item, rfer, 64);
BTRFS_SETGET_FUNCS(qgroup_info_rfer_cmpr, struct btrfs_qgroup_info_item,
		   rfer_cmpr, 64);
BTRFS_SETGET_FUNCS(qgroup_info_excl, struct btrfs_qgroup_info_item, excl, 64);
BTRFS_SETGET_FUNCS(qgroup_info_excl_cmpr, struct btrfs_qgroup_info_item,
		   excl_cmpr, 64);

BTRFS_SETGET_STACK_FUNCS(stack_qgroup_info_generation,
			 struct btrfs_qgroup_info_item, generation, 64);
BTRFS_SETGET_STACK_FUNCS(stack_qgroup_info_rfer, struct btrfs_qgroup_info_item,
			 rfer, 64);
BTRFS_SETGET_STACK_FUNCS(stack_qgroup_info_rfer_cmpr,
			 struct btrfs_qgroup_info_item, rfer_cmpr, 64);
BTRFS_SETGET_STACK_FUNCS(stack_qgroup_info_excl, struct btrfs_qgroup_info_item,
			 excl, 64);
BTRFS_SETGET_STACK_FUNCS(stack_qgroup_info_excl_cmpr,
			 struct btrfs_qgroup_info_item, excl_cmpr, 64);

/* btrfs_qgroup_limit_item */
BTRFS_SETGET_FUNCS(qgroup_limit_flags, struct btrfs_qgroup_limit_item,
		   flags, 64);
BTRFS_SETGET_FUNCS(qgroup_limit_max_rfer, struct btrfs_qgroup_limit_item,
		   max_rfer, 64);
BTRFS_SETGET_FUNCS(qgroup_limit_max_excl, struct btrfs_qgroup_limit_item,
		   max_excl, 64);
BTRFS_SETGET_FUNCS(qgroup_limit_rsv_rfer, struct btrfs_qgroup_limit_item,
		   rsv_rfer, 64);
BTRFS_SETGET_FUNCS(qgroup_limit_rsv_excl, struct btrfs_qgroup_limit_item,
		   rsv_excl, 64);

/* btrfs_dev_replace_item */
BTRFS_SETGET_FUNCS(dev_replace_src_devid,
		   struct btrfs_dev_replace_item, src_devid, 64);
BTRFS_SETGET_FUNCS(dev_replace_cont_reading_from_srcdev_mode,
		   struct btrfs_dev_replace_item, cont_reading_from_srcdev_mode,
		   64);
BTRFS_SETGET_FUNCS(dev_replace_replace_state, struct btrfs_dev_replace_item,
		   replace_state, 64);
BTRFS_SETGET_FUNCS(dev_replace_time_started, struct btrfs_dev_replace_item,
		   time_started, 64);
BTRFS_SETGET_FUNCS(dev_replace_time_stopped, struct btrfs_dev_replace_item,
		   time_stopped, 64);
BTRFS_SETGET_FUNCS(dev_replace_num_write_errors, struct btrfs_dev_replace_item,
		   num_write_errors, 64);
BTRFS_SETGET_FUNCS(dev_replace_num_uncorrectable_read_errors,
		   struct btrfs_dev_replace_item, num_uncorrectable_read_errors,
		   64);
BTRFS_SETGET_FUNCS(dev_replace_cursor_left, struct btrfs_dev_replace_item,
		   cursor_left, 64);
BTRFS_SETGET_FUNCS(dev_replace_cursor_right, struct btrfs_dev_replace_item,
		   cursor_right, 64);

BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_src_devid,
			 struct btrfs_dev_replace_item, src_devid, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_cont_reading_from_srcdev_mode,
			 struct btrfs_dev_replace_item,
			 cont_reading_from_srcdev_mode, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_replace_state,
			 struct btrfs_dev_replace_item, replace_state, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_time_started,
			 struct btrfs_dev_replace_item, time_started, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_time_stopped,
			 struct btrfs_dev_replace_item, time_stopped, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_num_write_errors,
			 struct btrfs_dev_replace_item, num_write_errors, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_num_uncorrectable_read_errors,
			 struct btrfs_dev_replace_item,
			 num_uncorrectable_read_errors, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_cursor_left,
			 struct btrfs_dev_replace_item, cursor_left, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_cursor_right,
			 struct btrfs_dev_replace_item, cursor_right, 64);

/* helper function to cast into the data area of the leaf. */
#define btrfs_item_ptr(leaf, slot, type) \
	((type *)(BTRFS_LEAF_DATA_OFFSET + \
	btrfs_item_offset(leaf, slot)))

#define btrfs_item_ptr_offset(leaf, slot) \
	((unsigned long)(BTRFS_LEAF_DATA_OFFSET + \
	btrfs_item_offset(leaf, slot)))

static inline u32 btrfs_crc32c(u32 crc, const void *address, unsigned length)
{
	return crc32c(crc, address, length);
}

static inline void btrfs_crc32c_final(u32 crc, u8 *result)
{
	put_unaligned_le32(~crc, result);
}

static inline u64 btrfs_name_hash(const char *name, int len)
{
       return crc32c((u32)~1, name, len);
}

/*
 * Figure the key offset of an extended inode ref
 */
static inline u64 btrfs_extref_hash(u64 parent_objectid, const char *name,
                                   int len)
{
       return (u64) crc32c(parent_objectid, name, len);
}

static inline gfp_t btrfs_alloc_write_mask(struct address_space *mapping)
{
	return mapping_gfp_constraint(mapping, ~__GFP_FS);
}

/* extent-tree.c */

enum btrfs_inline_ref_type {
	BTRFS_REF_TYPE_INVALID,
	BTRFS_REF_TYPE_BLOCK,
	BTRFS_REF_TYPE_DATA,
	BTRFS_REF_TYPE_ANY,
};

int btrfs_get_extent_inline_ref_type(const struct extent_buffer *eb,
				     struct btrfs_extent_inline_ref *iref,
				     enum btrfs_inline_ref_type is_data);
u64 hash_extent_data_ref(u64 root_objectid, u64 owner, u64 offset);

/*
 * Take the number of bytes to be checksummmed and figure out how many leaves
 * it would require to store the csums for that many bytes.
 */
static inline u64 btrfs_csum_bytes_to_leaves(
			const struct btrfs_fs_info *fs_info, u64 csum_bytes)
{
	const u64 num_csums = csum_bytes >> fs_info->sectorsize_bits;

	return DIV_ROUND_UP_ULL(num_csums, fs_info->csums_per_leaf);
}

/*
 * Use this if we would be adding new items, as we could split nodes as we cow
 * down the tree.
 */
static inline u64 btrfs_calc_insert_metadata_size(struct btrfs_fs_info *fs_info,
						  unsigned num_items)
{
	return (u64)fs_info->nodesize * BTRFS_MAX_LEVEL * 2 * num_items;
}

/*
 * Doing a truncate or a modification won't result in new nodes or leaves, just
 * what we need for COW.
 */
static inline u64 btrfs_calc_metadata_size(struct btrfs_fs_info *fs_info,
						 unsigned num_items)
{
	return (u64)fs_info->nodesize * BTRFS_MAX_LEVEL * num_items;
}

int btrfs_add_excluded_extent(struct btrfs_fs_info *fs_info,
			      u64 start, u64 num_bytes);
void btrfs_free_excluded_extents(struct btrfs_block_group *cache);
int btrfs_run_delayed_refs(struct btrfs_trans_handle *trans,
			   unsigned long count);
void btrfs_cleanup_ref_head_accounting(struct btrfs_fs_info *fs_info,
				  struct btrfs_delayed_ref_root *delayed_refs,
				  struct btrfs_delayed_ref_head *head);
int btrfs_lookup_data_extent(struct btrfs_fs_info *fs_info, u64 start, u64 len);
int btrfs_lookup_extent_info(struct btrfs_trans_handle *trans,
			     struct btrfs_fs_info *fs_info, u64 bytenr,
			     u64 offset, int metadata, u64 *refs, u64 *flags);
int btrfs_pin_extent(struct btrfs_trans_handle *trans, u64 bytenr, u64 num,
		     int reserved);
int btrfs_pin_extent_for_log_replay(struct btrfs_trans_handle *trans,
				    u64 bytenr, u64 num_bytes);
int btrfs_exclude_logged_extents(struct extent_buffer *eb);
int btrfs_cross_ref_exist(struct btrfs_root *root,
			  u64 objectid, u64 offset, u64 bytenr, bool strict);
struct extent_buffer *btrfs_alloc_tree_block(struct btrfs_trans_handle *trans,
					     struct btrfs_root *root,
					     u64 parent, u64 root_objectid,
					     const struct btrfs_disk_key *key,
					     int level, u64 hint,
					     u64 empty_size,
					     enum btrfs_lock_nesting nest);
void btrfs_free_tree_block(struct btrfs_trans_handle *trans,
			   u64 root_id,
			   struct extent_buffer *buf,
			   u64 parent, int last_ref);
int btrfs_alloc_reserved_file_extent(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root, u64 owner,
				     u64 offset, u64 ram_bytes,
				     struct btrfs_key *ins);
int btrfs_alloc_logged_file_extent(struct btrfs_trans_handle *trans,
				   u64 root_objectid, u64 owner, u64 offset,
				   struct btrfs_key *ins);
int btrfs_reserve_extent(struct btrfs_root *root, u64 ram_bytes, u64 num_bytes,
			 u64 min_alloc_size, u64 empty_size, u64 hint_byte,
			 struct btrfs_key *ins, int is_data, int delalloc);
int btrfs_inc_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct extent_buffer *buf, int full_backref);
int btrfs_dec_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct extent_buffer *buf, int full_backref);
int btrfs_set_disk_extent_flags(struct btrfs_trans_handle *trans,
				struct extent_buffer *eb, u64 flags,
				int level, int is_data);
int btrfs_free_extent(struct btrfs_trans_handle *trans, struct btrfs_ref *ref);

int btrfs_free_reserved_extent(struct btrfs_fs_info *fs_info,
			       u64 start, u64 len, int delalloc);
int btrfs_pin_reserved_extent(struct btrfs_trans_handle *trans, u64 start,
			      u64 len);
int btrfs_finish_extent_commit(struct btrfs_trans_handle *trans);
int btrfs_inc_extent_ref(struct btrfs_trans_handle *trans,
			 struct btrfs_ref *generic_ref);

void btrfs_clear_space_info_full(struct btrfs_fs_info *info);

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
};

enum btrfs_flush_state {
	FLUSH_DELAYED_ITEMS_NR	=	1,
	FLUSH_DELAYED_ITEMS	=	2,
	FLUSH_DELAYED_REFS_NR	=	3,
	FLUSH_DELAYED_REFS	=	4,
	FLUSH_DELALLOC		=	5,
	FLUSH_DELALLOC_WAIT	=	6,
	FLUSH_DELALLOC_FULL	=	7,
	ALLOC_CHUNK		=	8,
	ALLOC_CHUNK_FORCE	=	9,
	RUN_DELAYED_IPUTS	=	10,
	COMMIT_TRANS		=	11,
};

int btrfs_subvolume_reserve_metadata(struct btrfs_root *root,
				     struct btrfs_block_rsv *rsv,
				     int nitems, bool use_global_rsv);
void btrfs_subvolume_release_metadata(struct btrfs_root *root,
				      struct btrfs_block_rsv *rsv);
void btrfs_delalloc_release_extents(struct btrfs_inode *inode, u64 num_bytes);

int btrfs_delalloc_reserve_metadata(struct btrfs_inode *inode, u64 num_bytes);
u64 btrfs_account_ro_block_groups_free_space(struct btrfs_space_info *sinfo);
int btrfs_error_unpin_extent_range(struct btrfs_fs_info *fs_info,
				   u64 start, u64 end);
int btrfs_discard_extent(struct btrfs_fs_info *fs_info, u64 bytenr,
			 u64 num_bytes, u64 *actual_bytes);
int btrfs_trim_fs(struct btrfs_fs_info *fs_info, struct fstrim_range *range);

int btrfs_init_space_info(struct btrfs_fs_info *fs_info);
int btrfs_delayed_refs_qgroup_accounting(struct btrfs_trans_handle *trans,
					 struct btrfs_fs_info *fs_info);
int btrfs_start_write_no_snapshotting(struct btrfs_root *root);
void btrfs_end_write_no_snapshotting(struct btrfs_root *root);
void btrfs_wait_for_snapshot_creation(struct btrfs_root *root);

/* ctree.c */
int btrfs_bin_search(struct extent_buffer *eb, const struct btrfs_key *key,
		     int *slot);
int __pure btrfs_comp_cpu_keys(const struct btrfs_key *k1, const struct btrfs_key *k2);
int btrfs_previous_item(struct btrfs_root *root,
			struct btrfs_path *path, u64 min_objectid,
			int type);
int btrfs_previous_extent_item(struct btrfs_root *root,
			struct btrfs_path *path, u64 min_objectid);
void btrfs_set_item_key_safe(struct btrfs_fs_info *fs_info,
			     struct btrfs_path *path,
			     const struct btrfs_key *new_key);
struct extent_buffer *btrfs_root_node(struct btrfs_root *root);
int btrfs_find_next_key(struct btrfs_root *root, struct btrfs_path *path,
			struct btrfs_key *key, int lowest_level,
			u64 min_trans);
int btrfs_search_forward(struct btrfs_root *root, struct btrfs_key *min_key,
			 struct btrfs_path *path,
			 u64 min_trans);
struct extent_buffer *btrfs_read_node_slot(struct extent_buffer *parent,
					   int slot);

int btrfs_cow_block(struct btrfs_trans_handle *trans,
		    struct btrfs_root *root, struct extent_buffer *buf,
		    struct extent_buffer *parent, int parent_slot,
		    struct extent_buffer **cow_ret,
		    enum btrfs_lock_nesting nest);
int btrfs_copy_root(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root,
		      struct extent_buffer *buf,
		      struct extent_buffer **cow_ret, u64 new_root_objectid);
int btrfs_block_can_be_shared(struct btrfs_root *root,
			      struct extent_buffer *buf);
void btrfs_extend_item(struct btrfs_path *path, u32 data_size);
void btrfs_truncate_item(struct btrfs_path *path, u32 new_size, int from_end);
int btrfs_split_item(struct btrfs_trans_handle *trans,
		     struct btrfs_root *root,
		     struct btrfs_path *path,
		     const struct btrfs_key *new_key,
		     unsigned long split_offset);
int btrfs_duplicate_item(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 struct btrfs_path *path,
			 const struct btrfs_key *new_key);
int btrfs_find_item(struct btrfs_root *fs_root, struct btrfs_path *path,
		u64 inum, u64 ioff, u8 key_type, struct btrfs_key *found_key);
int btrfs_search_slot(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		      const struct btrfs_key *key, struct btrfs_path *p,
		      int ins_len, int cow);
int btrfs_search_old_slot(struct btrfs_root *root, const struct btrfs_key *key,
			  struct btrfs_path *p, u64 time_seq);
int btrfs_search_slot_for_read(struct btrfs_root *root,
			       const struct btrfs_key *key,
			       struct btrfs_path *p, int find_higher,
			       int return_any);
int btrfs_realloc_node(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct extent_buffer *parent,
		       int start_slot, u64 *last_ret,
		       struct btrfs_key *progress);
void btrfs_release_path(struct btrfs_path *p);
struct btrfs_path *btrfs_alloc_path(void);
void btrfs_free_path(struct btrfs_path *p);

int btrfs_del_items(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct btrfs_path *path, int slot, int nr);
static inline int btrfs_del_item(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path)
{
	return btrfs_del_items(trans, root, path, path->slots[0], 1);
}

/*
 * Describes a batch of items to insert in a btree. This is used by
 * btrfs_insert_empty_items().
 */
struct btrfs_item_batch {
	/*
	 * Pointer to an array containing the keys of the items to insert (in
	 * sorted order).
	 */
	const struct btrfs_key *keys;
	/* Pointer to an array containing the data size for each item to insert. */
	const u32 *data_sizes;
	/*
	 * The sum of data sizes for all items. The caller can compute this while
	 * setting up the data_sizes array, so it ends up being more efficient
	 * than having btrfs_insert_empty_items() or setup_item_for_insert()
	 * doing it, as it would avoid an extra loop over a potentially large
	 * array, and in the case of setup_item_for_insert(), we would be doing
	 * it while holding a write lock on a leaf and often on upper level nodes
	 * too, unnecessarily increasing the size of a critical section.
	 */
	u32 total_data_size;
	/* Size of the keys and data_sizes arrays (number of items in the batch). */
	int nr;
};

void btrfs_setup_item_for_insert(struct btrfs_root *root,
				 struct btrfs_path *path,
				 const struct btrfs_key *key,
				 u32 data_size);
int btrfs_insert_item(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		      const struct btrfs_key *key, void *data, u32 data_size);
int btrfs_insert_empty_items(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct btrfs_path *path,
			     const struct btrfs_item_batch *batch);

static inline int btrfs_insert_empty_item(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path,
					  const struct btrfs_key *key,
					  u32 data_size)
{
	struct btrfs_item_batch batch;

	batch.keys = key;
	batch.data_sizes = &data_size;
	batch.total_data_size = data_size;
	batch.nr = 1;

	return btrfs_insert_empty_items(trans, root, path, &batch);
}

int btrfs_prev_leaf(struct btrfs_root *root, struct btrfs_path *path);
int btrfs_next_old_leaf(struct btrfs_root *root, struct btrfs_path *path,
			u64 time_seq);

int btrfs_search_backwards(struct btrfs_root *root, struct btrfs_key *key,
			   struct btrfs_path *path);

static inline int btrfs_next_old_item(struct btrfs_root *root,
				      struct btrfs_path *p, u64 time_seq)
{
	++p->slots[0];
	if (p->slots[0] >= btrfs_header_nritems(p->nodes[0]))
		return btrfs_next_old_leaf(root, p, time_seq);
	return 0;
}

/*
 * Search the tree again to find a leaf with greater keys.
 *
 * Returns 0 if it found something or 1 if there are no greater leaves.
 * Returns < 0 on error.
 */
static inline int btrfs_next_leaf(struct btrfs_root *root, struct btrfs_path *path)
{
	return btrfs_next_old_leaf(root, path, 0);
}

static inline int btrfs_next_item(struct btrfs_root *root, struct btrfs_path *p)
{
	return btrfs_next_old_item(root, p, 0);
}
int btrfs_leaf_free_space(struct extent_buffer *leaf);
int __must_check btrfs_drop_snapshot(struct btrfs_root *root, int update_ref,
				     int for_reloc);
int btrfs_drop_subtree(struct btrfs_trans_handle *trans,
			struct btrfs_root *root,
			struct extent_buffer *node,
			struct extent_buffer *parent);
static inline int btrfs_fs_closing(struct btrfs_fs_info *fs_info)
{
	/*
	 * Do it this way so we only ever do one test_bit in the normal case.
	 */
	if (test_bit(BTRFS_FS_CLOSING_START, &fs_info->flags)) {
		if (test_bit(BTRFS_FS_CLOSING_DONE, &fs_info->flags))
			return 2;
		return 1;
	}
	return 0;
}

/*
 * If we remount the fs to be R/O or umount the fs, the cleaner needn't do
 * anything except sleeping. This function is used to check the status of
 * the fs.
 * We check for BTRFS_FS_STATE_RO to avoid races with a concurrent remount,
 * since setting and checking for SB_RDONLY in the superblock's flags is not
 * atomic.
 */
static inline int btrfs_need_cleaner_sleep(struct btrfs_fs_info *fs_info)
{
	return test_bit(BTRFS_FS_STATE_RO, &fs_info->fs_state) ||
		btrfs_fs_closing(fs_info);
}

static inline void btrfs_set_sb_rdonly(struct super_block *sb)
{
	sb->s_flags |= SB_RDONLY;
	set_bit(BTRFS_FS_STATE_RO, &btrfs_sb(sb)->fs_state);
}

static inline void btrfs_clear_sb_rdonly(struct super_block *sb)
{
	sb->s_flags &= ~SB_RDONLY;
	clear_bit(BTRFS_FS_STATE_RO, &btrfs_sb(sb)->fs_state);
}

/* root-item.c */
int btrfs_add_root_ref(struct btrfs_trans_handle *trans, u64 root_id,
		       u64 ref_id, u64 dirid, u64 sequence, const char *name,
		       int name_len);
int btrfs_del_root_ref(struct btrfs_trans_handle *trans, u64 root_id,
		       u64 ref_id, u64 dirid, u64 *sequence, const char *name,
		       int name_len);
int btrfs_del_root(struct btrfs_trans_handle *trans,
		   const struct btrfs_key *key);
int btrfs_insert_root(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		      const struct btrfs_key *key,
		      struct btrfs_root_item *item);
int __must_check btrfs_update_root(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct btrfs_key *key,
				   struct btrfs_root_item *item);
int btrfs_find_root(struct btrfs_root *root, const struct btrfs_key *search_key,
		    struct btrfs_path *path, struct btrfs_root_item *root_item,
		    struct btrfs_key *root_key);
int btrfs_find_orphan_roots(struct btrfs_fs_info *fs_info);
void btrfs_set_root_node(struct btrfs_root_item *item,
			 struct extent_buffer *node);
void btrfs_check_and_init_root_item(struct btrfs_root_item *item);
void btrfs_update_root_times(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root);

/* uuid-tree.c */
int btrfs_uuid_tree_add(struct btrfs_trans_handle *trans, u8 *uuid, u8 type,
			u64 subid);
int btrfs_uuid_tree_remove(struct btrfs_trans_handle *trans, u8 *uuid, u8 type,
			u64 subid);
int btrfs_uuid_tree_iterate(struct btrfs_fs_info *fs_info);

/* dir-item.c */
int btrfs_check_dir_item_collision(struct btrfs_root *root, u64 dir,
			  const char *name, int name_len);
int btrfs_insert_dir_item(struct btrfs_trans_handle *trans, const char *name,
			  int name_len, struct btrfs_inode *dir,
			  struct btrfs_key *location, u8 type, u64 index);
struct btrfs_dir_item *btrfs_lookup_dir_item(struct btrfs_trans_handle *trans,
					     struct btrfs_root *root,
					     struct btrfs_path *path, u64 dir,
					     const char *name, int name_len,
					     int mod);
struct btrfs_dir_item *
btrfs_lookup_dir_index_item(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    struct btrfs_path *path, u64 dir,
			    u64 index, const char *name, int name_len,
			    int mod);
struct btrfs_dir_item *
btrfs_search_dir_index_item(struct btrfs_root *root,
			    struct btrfs_path *path, u64 dirid,
			    const char *name, int name_len);
int btrfs_delete_one_dir_name(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct btrfs_path *path,
			      struct btrfs_dir_item *di);
int btrfs_insert_xattr_item(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    struct btrfs_path *path, u64 objectid,
			    const char *name, u16 name_len,
			    const void *data, u16 data_len);
struct btrfs_dir_item *btrfs_lookup_xattr(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path, u64 dir,
					  const char *name, u16 name_len,
					  int mod);
struct btrfs_dir_item *btrfs_match_dir_item_name(struct btrfs_fs_info *fs_info,
						 struct btrfs_path *path,
						 const char *name,
						 int name_len);

/* orphan.c */
int btrfs_insert_orphan_item(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root, u64 offset);
int btrfs_del_orphan_item(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, u64 offset);
int btrfs_find_orphan_item(struct btrfs_root *root, u64 offset);

/* file-item.c */
struct btrfs_dio_private;
int btrfs_del_csums(struct btrfs_trans_handle *trans,
		    struct btrfs_root *root, u64 bytenr, u64 len);
blk_status_t btrfs_lookup_bio_sums(struct inode *inode, struct bio *bio, u8 *dst);
int btrfs_insert_file_extent(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     u64 objectid, u64 pos,
			     u64 disk_offset, u64 disk_num_bytes,
			     u64 num_bytes, u64 offset, u64 ram_bytes,
			     u8 compression, u8 encryption, u16 other_encoding);
int btrfs_lookup_file_extent(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct btrfs_path *path, u64 objectid,
			     u64 bytenr, int mod);
int btrfs_csum_file_blocks(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   struct btrfs_ordered_sum *sums);
blk_status_t btrfs_csum_one_bio(struct btrfs_inode *inode, struct bio *bio,
				u64 file_start, int contig);
int btrfs_lookup_csums_range(struct btrfs_root *root, u64 start, u64 end,
			     struct list_head *list, int search_commit);
void btrfs_extent_item_to_extent_map(struct btrfs_inode *inode,
				     const struct btrfs_path *path,
				     struct btrfs_file_extent_item *fi,
				     const bool new_inline,
				     struct extent_map *em);
int btrfs_inode_clear_file_extent_range(struct btrfs_inode *inode, u64 start,
					u64 len);
int btrfs_inode_set_file_extent_range(struct btrfs_inode *inode, u64 start,
				      u64 len);
void btrfs_inode_safe_disk_i_size_write(struct btrfs_inode *inode, u64 new_i_size);
u64 btrfs_file_extent_end(const struct btrfs_path *path);

/* inode.c */
blk_status_t btrfs_submit_data_bio(struct inode *inode, struct bio *bio,
				   int mirror_num, unsigned long bio_flags);
unsigned int btrfs_verify_data_csum(struct btrfs_bio *bbio,
				    u32 bio_offset, struct page *page,
				    u64 start, u64 end);
struct extent_map *btrfs_get_extent_fiemap(struct btrfs_inode *inode,
					   u64 start, u64 len);
noinline int can_nocow_extent(struct inode *inode, u64 offset, u64 *len,
			      u64 *orig_start, u64 *orig_block_len,
			      u64 *ram_bytes, bool strict);

void __btrfs_del_delalloc_inode(struct btrfs_root *root,
				struct btrfs_inode *inode);
struct inode *btrfs_lookup_dentry(struct inode *dir, struct dentry *dentry);
int btrfs_set_inode_index(struct btrfs_inode *dir, u64 *index);
int btrfs_unlink_inode(struct btrfs_trans_handle *trans,
		       struct btrfs_inode *dir, struct btrfs_inode *inode,
		       const char *name, int name_len);
int btrfs_add_link(struct btrfs_trans_handle *trans,
		   struct btrfs_inode *parent_inode, struct btrfs_inode *inode,
		   const char *name, int name_len, int add_backref, u64 index);
int btrfs_delete_subvolume(struct inode *dir, struct dentry *dentry);
int btrfs_truncate_block(struct btrfs_inode *inode, loff_t from, loff_t len,
			 int front);

int btrfs_start_delalloc_snapshot(struct btrfs_root *root, bool in_reclaim_context);
int btrfs_start_delalloc_roots(struct btrfs_fs_info *fs_info, long nr,
			       bool in_reclaim_context);
int btrfs_set_extent_delalloc(struct btrfs_inode *inode, u64 start, u64 end,
			      unsigned int extra_bits,
			      struct extent_state **cached_state);
int btrfs_create_subvol_root(struct btrfs_trans_handle *trans,
			     struct btrfs_root *new_root,
			     struct btrfs_root *parent_root,
			     struct user_namespace *mnt_userns);
 void btrfs_set_delalloc_extent(struct inode *inode, struct extent_state *state,
			       unsigned *bits);
void btrfs_clear_delalloc_extent(struct inode *inode,
				 struct extent_state *state, unsigned *bits);
void btrfs_merge_delalloc_extent(struct inode *inode, struct extent_state *new,
				 struct extent_state *other);
void btrfs_split_delalloc_extent(struct inode *inode,
				 struct extent_state *orig, u64 split);
void btrfs_set_range_writeback(struct btrfs_inode *inode, u64 start, u64 end);
vm_fault_t btrfs_page_mkwrite(struct vm_fault *vmf);
int btrfs_readpage(struct file *file, struct page *page);
void btrfs_evict_inode(struct inode *inode);
int btrfs_write_inode(struct inode *inode, struct writeback_control *wbc);
struct inode *btrfs_alloc_inode(struct super_block *sb);
void btrfs_destroy_inode(struct inode *inode);
void btrfs_free_inode(struct inode *inode);
int btrfs_drop_inode(struct inode *inode);
int __init btrfs_init_cachep(void);
void __cold btrfs_destroy_cachep(void);
struct inode *btrfs_iget_path(struct super_block *s, u64 ino,
			      struct btrfs_root *root, struct btrfs_path *path);
struct inode *btrfs_iget(struct super_block *s, u64 ino, struct btrfs_root *root);
struct extent_map *btrfs_get_extent(struct btrfs_inode *inode,
				    struct page *page, size_t pg_offset,
				    u64 start, u64 end);
int btrfs_update_inode(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct btrfs_inode *inode);
int btrfs_update_inode_fallback(struct btrfs_trans_handle *trans,
				struct btrfs_root *root, struct btrfs_inode *inode);
int btrfs_orphan_add(struct btrfs_trans_handle *trans,
		struct btrfs_inode *inode);
int btrfs_orphan_cleanup(struct btrfs_root *root);
int btrfs_cont_expand(struct btrfs_inode *inode, loff_t oldsize, loff_t size);
void btrfs_add_delayed_iput(struct inode *inode);
void btrfs_run_delayed_iputs(struct btrfs_fs_info *fs_info);
int btrfs_wait_on_delayed_iputs(struct btrfs_fs_info *fs_info);
int btrfs_prealloc_file_range(struct inode *inode, int mode,
			      u64 start, u64 num_bytes, u64 min_size,
			      loff_t actual_len, u64 *alloc_hint);
int btrfs_prealloc_file_range_trans(struct inode *inode,
				    struct btrfs_trans_handle *trans, int mode,
				    u64 start, u64 num_bytes, u64 min_size,
				    loff_t actual_len, u64 *alloc_hint);
int btrfs_run_delalloc_range(struct btrfs_inode *inode, struct page *locked_page,
		u64 start, u64 end, int *page_started, unsigned long *nr_written,
		struct writeback_control *wbc);
int btrfs_writepage_cow_fixup(struct page *page);
void btrfs_writepage_endio_finish_ordered(struct btrfs_inode *inode,
					  struct page *page, u64 start,
					  u64 end, bool uptodate);
extern const struct dentry_operations btrfs_dentry_operations;
extern const struct iomap_ops btrfs_dio_iomap_ops;
extern const struct iomap_dio_ops btrfs_dio_ops;

/* Inode locking type flags, by default the exclusive lock is taken */
#define BTRFS_ILOCK_SHARED	(1U << 0)
#define BTRFS_ILOCK_TRY 	(1U << 1)
#define BTRFS_ILOCK_MMAP	(1U << 2)

int btrfs_inode_lock(struct inode *inode, unsigned int ilock_flags);
void btrfs_inode_unlock(struct inode *inode, unsigned int ilock_flags);
void btrfs_update_inode_bytes(struct btrfs_inode *inode,
			      const u64 add_bytes,
			      const u64 del_bytes);

/* ioctl.c */
long btrfs_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
long btrfs_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
int btrfs_fileattr_get(struct dentry *dentry, struct fileattr *fa);
int btrfs_fileattr_set(struct user_namespace *mnt_userns,
		       struct dentry *dentry, struct fileattr *fa);
int btrfs_ioctl_get_supported_features(void __user *arg);
void btrfs_sync_inode_flags_to_i_flags(struct inode *inode);
int __pure btrfs_is_empty_uuid(u8 *uuid);
int btrfs_defrag_file(struct inode *inode, struct file_ra_state *ra,
		      struct btrfs_ioctl_defrag_range_args *range,
		      u64 newer_than, unsigned long max_to_defrag);
void btrfs_get_block_group_info(struct list_head *groups_list,
				struct btrfs_ioctl_space_info *space);
void btrfs_update_ioctl_balance_args(struct btrfs_fs_info *fs_info,
			       struct btrfs_ioctl_balance_args *bargs);
bool btrfs_exclop_start(struct btrfs_fs_info *fs_info,
			enum btrfs_exclusive_operation type);
bool btrfs_exclop_start_try_lock(struct btrfs_fs_info *fs_info,
				 enum btrfs_exclusive_operation type);
void btrfs_exclop_start_unlock(struct btrfs_fs_info *fs_info);
void btrfs_exclop_finish(struct btrfs_fs_info *fs_info);
void btrfs_exclop_balance(struct btrfs_fs_info *fs_info,
			  enum btrfs_exclusive_operation op);


/* file.c */
int __init btrfs_auto_defrag_init(void);
void __cold btrfs_auto_defrag_exit(void);
int btrfs_add_inode_defrag(struct btrfs_trans_handle *trans,
			   struct btrfs_inode *inode, u32 extent_thresh);
int btrfs_run_defrag_inodes(struct btrfs_fs_info *fs_info);
void btrfs_cleanup_defrag_inodes(struct btrfs_fs_info *fs_info);
int btrfs_sync_file(struct file *file, loff_t start, loff_t end, int datasync);
void btrfs_drop_extent_cache(struct btrfs_inode *inode, u64 start, u64 end,
			     int skip_pinned);
extern const struct file_operations btrfs_file_operations;
int btrfs_drop_extents(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct btrfs_inode *inode,
		       struct btrfs_drop_extents_args *args);
int btrfs_replace_file_extents(struct btrfs_inode *inode,
			   struct btrfs_path *path, const u64 start,
			   const u64 end,
			   struct btrfs_replace_extent_info *extent_info,
			   struct btrfs_trans_handle **trans_out);
int btrfs_mark_extent_written(struct btrfs_trans_handle *trans,
			      struct btrfs_inode *inode, u64 start, u64 end);
int btrfs_release_file(struct inode *inode, struct file *file);
int btrfs_dirty_pages(struct btrfs_inode *inode, struct page **pages,
		      size_t num_pages, loff_t pos, size_t write_bytes,
		      struct extent_state **cached, bool noreserve);
int btrfs_fdatawrite_range(struct inode *inode, loff_t start, loff_t end);
int btrfs_check_nocow_lock(struct btrfs_inode *inode, loff_t pos,
			   size_t *write_bytes);
void btrfs_check_nocow_unlock(struct btrfs_inode *inode);

/* tree-defrag.c */
int btrfs_defrag_leaves(struct btrfs_trans_handle *trans,
			struct btrfs_root *root);

/* super.c */
int btrfs_parse_options(struct btrfs_fs_info *info, char *options,
			unsigned long new_flags);
int btrfs_sync_fs(struct super_block *sb, int wait);
char *btrfs_get_subvol_name_from_objectid(struct btrfs_fs_info *fs_info,
					  u64 subvol_objectid);

static inline __printf(2, 3) __cold
void btrfs_no_printk(const struct btrfs_fs_info *fs_info, const char *fmt, ...)
{
}

#ifdef CONFIG_PRINTK
__printf(2, 3)
__cold
void btrfs_printk(const struct btrfs_fs_info *fs_info, const char *fmt, ...);
#else
#define btrfs_printk(fs_info, fmt, args...) \
	btrfs_no_printk(fs_info, fmt, ##args)
#endif

#define btrfs_emerg(fs_info, fmt, args...) \
	btrfs_printk(fs_info, KERN_EMERG fmt, ##args)
#define btrfs_alert(fs_info, fmt, args...) \
	btrfs_printk(fs_info, KERN_ALERT fmt, ##args)
#define btrfs_crit(fs_info, fmt, args...) \
	btrfs_printk(fs_info, KERN_CRIT fmt, ##args)
#define btrfs_err(fs_info, fmt, args...) \
	btrfs_printk(fs_info, KERN_ERR fmt, ##args)
#define btrfs_warn(fs_info, fmt, args...) \
	btrfs_printk(fs_info, KERN_WARNING fmt, ##args)
#define btrfs_notice(fs_info, fmt, args...) \
	btrfs_printk(fs_info, KERN_NOTICE fmt, ##args)
#define btrfs_info(fs_info, fmt, args...) \
	btrfs_printk(fs_info, KERN_INFO fmt, ##args)

/*
 * Wrappers that use printk_in_rcu
 */
#define btrfs_emerg_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, KERN_EMERG fmt, ##args)
#define btrfs_alert_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, KERN_ALERT fmt, ##args)
#define btrfs_crit_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, KERN_CRIT fmt, ##args)
#define btrfs_err_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, KERN_ERR fmt, ##args)
#define btrfs_warn_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, KERN_WARNING fmt, ##args)
#define btrfs_notice_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, KERN_NOTICE fmt, ##args)
#define btrfs_info_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, KERN_INFO fmt, ##args)

/*
 * Wrappers that use a ratelimited printk_in_rcu
 */
#define btrfs_emerg_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, KERN_EMERG fmt, ##args)
#define btrfs_alert_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, KERN_ALERT fmt, ##args)
#define btrfs_crit_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, KERN_CRIT fmt, ##args)
#define btrfs_err_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, KERN_ERR fmt, ##args)
#define btrfs_warn_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, KERN_WARNING fmt, ##args)
#define btrfs_notice_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, KERN_NOTICE fmt, ##args)
#define btrfs_info_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, KERN_INFO fmt, ##args)

/*
 * Wrappers that use a ratelimited printk
 */
#define btrfs_emerg_rl(fs_info, fmt, args...) \
	btrfs_printk_ratelimited(fs_info, KERN_EMERG fmt, ##args)
#define btrfs_alert_rl(fs_info, fmt, args...) \
	btrfs_printk_ratelimited(fs_info, KERN_ALERT fmt, ##args)
#define btrfs_crit_rl(fs_info, fmt, args...) \
	btrfs_printk_ratelimited(fs_info, KERN_CRIT fmt, ##args)
#define btrfs_err_rl(fs_info, fmt, args...) \
	btrfs_printk_ratelimited(fs_info, KERN_ERR fmt, ##args)
#define btrfs_warn_rl(fs_info, fmt, args...) \
	btrfs_printk_ratelimited(fs_info, KERN_WARNING fmt, ##args)
#define btrfs_notice_rl(fs_info, fmt, args...) \
	btrfs_printk_ratelimited(fs_info, KERN_NOTICE fmt, ##args)
#define btrfs_info_rl(fs_info, fmt, args...) \
	btrfs_printk_ratelimited(fs_info, KERN_INFO fmt, ##args)

#if defined(CONFIG_DYNAMIC_DEBUG)
#define btrfs_debug(fs_info, fmt, args...)				\
	_dynamic_func_call_no_desc(fmt, btrfs_printk,			\
				   fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_in_rcu(fs_info, fmt, args...)			\
	_dynamic_func_call_no_desc(fmt, btrfs_printk_in_rcu,		\
				   fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_rl_in_rcu(fs_info, fmt, args...)			\
	_dynamic_func_call_no_desc(fmt, btrfs_printk_rl_in_rcu,		\
				   fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_rl(fs_info, fmt, args...)				\
	_dynamic_func_call_no_desc(fmt, btrfs_printk_ratelimited,	\
				   fs_info, KERN_DEBUG fmt, ##args)
#elif defined(DEBUG)
#define btrfs_debug(fs_info, fmt, args...) \
	btrfs_printk(fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_rl(fs_info, fmt, args...) \
	btrfs_printk_ratelimited(fs_info, KERN_DEBUG fmt, ##args)
#else
#define btrfs_debug(fs_info, fmt, args...) \
	btrfs_no_printk(fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_in_rcu(fs_info, fmt, args...) \
	btrfs_no_printk_in_rcu(fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_no_printk_in_rcu(fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_rl(fs_info, fmt, args...) \
	btrfs_no_printk(fs_info, KERN_DEBUG fmt, ##args)
#endif

#define btrfs_printk_in_rcu(fs_info, fmt, args...)	\
do {							\
	rcu_read_lock();				\
	btrfs_printk(fs_info, fmt, ##args);		\
	rcu_read_unlock();				\
} while (0)

#define btrfs_no_printk_in_rcu(fs_info, fmt, args...)	\
do {							\
	rcu_read_lock();				\
	btrfs_no_printk(fs_info, fmt, ##args);		\
	rcu_read_unlock();				\
} while (0)

#define btrfs_printk_ratelimited(fs_info, fmt, args...)		\
do {								\
	static DEFINE_RATELIMIT_STATE(_rs,			\
		DEFAULT_RATELIMIT_INTERVAL,			\
		DEFAULT_RATELIMIT_BURST);       		\
	if (__ratelimit(&_rs))					\
		btrfs_printk(fs_info, fmt, ##args);		\
} while (0)

#define btrfs_printk_rl_in_rcu(fs_info, fmt, args...)		\
do {								\
	rcu_read_lock();					\
	btrfs_printk_ratelimited(fs_info, fmt, ##args);		\
	rcu_read_unlock();					\
} while (0)

#ifdef CONFIG_BTRFS_ASSERT
__cold __noreturn
static inline void assertfail(const char *expr, const char *file, int line)
{
	pr_err("assertion failed: %s, in %s:%d\n", expr, file, line);
	BUG();
}

#define ASSERT(expr)						\
	(likely(expr) ? (void)0 : assertfail(#expr, __FILE__, __LINE__))

#else
static inline void assertfail(const char *expr, const char* file, int line) { }
#define ASSERT(expr)	(void)(expr)
#endif

#if BITS_PER_LONG == 32
#define BTRFS_32BIT_MAX_FILE_SIZE (((u64)ULONG_MAX + 1) << PAGE_SHIFT)
/*
 * The warning threshold is 5/8th of the MAX_LFS_FILESIZE that limits the logical
 * addresses of extents.
 *
 * For 4K page size it's about 10T, for 64K it's 160T.
 */
#define BTRFS_32BIT_EARLY_WARN_THRESHOLD (BTRFS_32BIT_MAX_FILE_SIZE * 5 / 8)
void btrfs_warn_32bit_limit(struct btrfs_fs_info *fs_info);
void btrfs_err_32bit_limit(struct btrfs_fs_info *fs_info);
#endif

/*
 * Get the correct offset inside the page of extent buffer.
 *
 * @eb:		target extent buffer
 * @start:	offset inside the extent buffer
 *
 * Will handle both sectorsize == PAGE_SIZE and sectorsize < PAGE_SIZE cases.
 */
static inline size_t get_eb_offset_in_page(const struct extent_buffer *eb,
					   unsigned long offset)
{
	/*
	 * For sectorsize == PAGE_SIZE case, eb->start will always be aligned
	 * to PAGE_SIZE, thus adding it won't cause any difference.
	 *
	 * For sectorsize < PAGE_SIZE, we must only read the data that belongs
	 * to the eb, thus we have to take the eb->start into consideration.
	 */
	return offset_in_page(offset + eb->start);
}

static inline unsigned long get_eb_page_index(unsigned long offset)
{
	/*
	 * For sectorsize == PAGE_SIZE case, plain >> PAGE_SHIFT is enough.
	 *
	 * For sectorsize < PAGE_SIZE case, we only support 64K PAGE_SIZE,
	 * and have ensured that all tree blocks are contained in one page,
	 * thus we always get index == 0.
	 */
	return offset >> PAGE_SHIFT;
}

/*
 * Use that for functions that are conditionally exported for sanity tests but
 * otherwise static
 */
#ifndef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
#define EXPORT_FOR_TESTS static
#else
#define EXPORT_FOR_TESTS
#endif

__cold
static inline void btrfs_print_v0_err(struct btrfs_fs_info *fs_info)
{
	btrfs_err(fs_info,
"Unsupported V0 extent filesystem detected. Aborting. Please re-create your filesystem with a newer kernel");
}

__printf(5, 6)
__cold
void __btrfs_handle_fs_error(struct btrfs_fs_info *fs_info, const char *function,
		     unsigned int line, int errno, const char *fmt, ...);

const char * __attribute_const__ btrfs_decode_error(int errno);

__cold
void __btrfs_abort_transaction(struct btrfs_trans_handle *trans,
			       const char *function,
			       unsigned int line, int errno);

/*
 * Call btrfs_abort_transaction as early as possible when an error condition is
 * detected, that way the exact line number is reported.
 */
#define btrfs_abort_transaction(trans, errno)		\
do {								\
	/* Report first abort since mount */			\
	if (!test_and_set_bit(BTRFS_FS_STATE_TRANS_ABORTED,	\
			&((trans)->fs_info->fs_state))) {	\
		if ((errno) != -EIO && (errno) != -EROFS) {		\
			WARN(1, KERN_DEBUG				\
			"BTRFS: Transaction aborted (error %d)\n",	\
			(errno));					\
		} else {						\
			btrfs_debug((trans)->fs_info,			\
				    "Transaction aborted (error %d)", \
				  (errno));			\
		}						\
	}							\
	__btrfs_abort_transaction((trans), __func__,		\
				  __LINE__, (errno));		\
} while (0)

#define btrfs_handle_fs_error(fs_info, errno, fmt, args...)		\
do {								\
	__btrfs_handle_fs_error((fs_info), __func__, __LINE__,	\
			  (errno), fmt, ##args);		\
} while (0)

#define BTRFS_FS_ERROR(fs_info)	(unlikely(test_bit(BTRFS_FS_STATE_ERROR, \
						   &(fs_info)->fs_state)))
#define BTRFS_FS_LOG_CLEANUP_ERROR(fs_info)				\
	(unlikely(test_bit(BTRFS_FS_STATE_LOG_CLEANUP_ERROR,		\
			   &(fs_info)->fs_state)))

__printf(5, 6)
__cold
void __btrfs_panic(struct btrfs_fs_info *fs_info, const char *function,
		   unsigned int line, int errno, const char *fmt, ...);
/*
 * If BTRFS_MOUNT_PANIC_ON_FATAL_ERROR is in mount_opt, __btrfs_panic
 * will panic().  Otherwise we BUG() here.
 */
#define btrfs_panic(fs_info, errno, fmt, args...)			\
do {									\
	__btrfs_panic(fs_info, __func__, __LINE__, errno, fmt, ##args);	\
	BUG();								\
} while (0)


/* compatibility and incompatibility defines */

#define btrfs_set_fs_incompat(__fs_info, opt) \
	__btrfs_set_fs_incompat((__fs_info), BTRFS_FEATURE_INCOMPAT_##opt, \
				#opt)

static inline void __btrfs_set_fs_incompat(struct btrfs_fs_info *fs_info,
					   u64 flag, const char* name)
{
	struct btrfs_super_block *disk_super;
	u64 features;

	disk_super = fs_info->super_copy;
	features = btrfs_super_incompat_flags(disk_super);
	if (!(features & flag)) {
		spin_lock(&fs_info->super_lock);
		features = btrfs_super_incompat_flags(disk_super);
		if (!(features & flag)) {
			features |= flag;
			btrfs_set_super_incompat_flags(disk_super, features);
			btrfs_info(fs_info,
				"setting incompat feature flag for %s (0x%llx)",
				name, flag);
		}
		spin_unlock(&fs_info->super_lock);
	}
}

#define btrfs_clear_fs_incompat(__fs_info, opt) \
	__btrfs_clear_fs_incompat((__fs_info), BTRFS_FEATURE_INCOMPAT_##opt, \
				  #opt)

static inline void __btrfs_clear_fs_incompat(struct btrfs_fs_info *fs_info,
					     u64 flag, const char* name)
{
	struct btrfs_super_block *disk_super;
	u64 features;

	disk_super = fs_info->super_copy;
	features = btrfs_super_incompat_flags(disk_super);
	if (features & flag) {
		spin_lock(&fs_info->super_lock);
		features = btrfs_super_incompat_flags(disk_super);
		if (features & flag) {
			features &= ~flag;
			btrfs_set_super_incompat_flags(disk_super, features);
			btrfs_info(fs_info,
				"clearing incompat feature flag for %s (0x%llx)",
				name, flag);
		}
		spin_unlock(&fs_info->super_lock);
	}
}

#define btrfs_fs_incompat(fs_info, opt) \
	__btrfs_fs_incompat((fs_info), BTRFS_FEATURE_INCOMPAT_##opt)

static inline bool __btrfs_fs_incompat(struct btrfs_fs_info *fs_info, u64 flag)
{
	struct btrfs_super_block *disk_super;
	disk_super = fs_info->super_copy;
	return !!(btrfs_super_incompat_flags(disk_super) & flag);
}

#define btrfs_set_fs_compat_ro(__fs_info, opt) \
	__btrfs_set_fs_compat_ro((__fs_info), BTRFS_FEATURE_COMPAT_RO_##opt, \
				 #opt)

static inline void __btrfs_set_fs_compat_ro(struct btrfs_fs_info *fs_info,
					    u64 flag, const char *name)
{
	struct btrfs_super_block *disk_super;
	u64 features;

	disk_super = fs_info->super_copy;
	features = btrfs_super_compat_ro_flags(disk_super);
	if (!(features & flag)) {
		spin_lock(&fs_info->super_lock);
		features = btrfs_super_compat_ro_flags(disk_super);
		if (!(features & flag)) {
			features |= flag;
			btrfs_set_super_compat_ro_flags(disk_super, features);
			btrfs_info(fs_info,
				"setting compat-ro feature flag for %s (0x%llx)",
				name, flag);
		}
		spin_unlock(&fs_info->super_lock);
	}
}

#define btrfs_clear_fs_compat_ro(__fs_info, opt) \
	__btrfs_clear_fs_compat_ro((__fs_info), BTRFS_FEATURE_COMPAT_RO_##opt, \
				   #opt)

static inline void __btrfs_clear_fs_compat_ro(struct btrfs_fs_info *fs_info,
					      u64 flag, const char *name)
{
	struct btrfs_super_block *disk_super;
	u64 features;

	disk_super = fs_info->super_copy;
	features = btrfs_super_compat_ro_flags(disk_super);
	if (features & flag) {
		spin_lock(&fs_info->super_lock);
		features = btrfs_super_compat_ro_flags(disk_super);
		if (features & flag) {
			features &= ~flag;
			btrfs_set_super_compat_ro_flags(disk_super, features);
			btrfs_info(fs_info,
				"clearing compat-ro feature flag for %s (0x%llx)",
				name, flag);
		}
		spin_unlock(&fs_info->super_lock);
	}
}

#define btrfs_fs_compat_ro(fs_info, opt) \
	__btrfs_fs_compat_ro((fs_info), BTRFS_FEATURE_COMPAT_RO_##opt)

static inline int __btrfs_fs_compat_ro(struct btrfs_fs_info *fs_info, u64 flag)
{
	struct btrfs_super_block *disk_super;
	disk_super = fs_info->super_copy;
	return !!(btrfs_super_compat_ro_flags(disk_super) & flag);
}

/* acl.c */
#ifdef CONFIG_BTRFS_FS_POSIX_ACL
struct posix_acl *btrfs_get_acl(struct inode *inode, int type, bool rcu);
int btrfs_set_acl(struct user_namespace *mnt_userns, struct inode *inode,
		  struct posix_acl *acl, int type);
int btrfs_init_acl(struct btrfs_trans_handle *trans,
		   struct inode *inode, struct inode *dir);
#else
#define btrfs_get_acl NULL
#define btrfs_set_acl NULL
static inline int btrfs_init_acl(struct btrfs_trans_handle *trans,
				 struct inode *inode, struct inode *dir)
{
	return 0;
}
#endif

/* relocation.c */
int btrfs_relocate_block_group(struct btrfs_fs_info *fs_info, u64 group_start);
int btrfs_init_reloc_root(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root);
int btrfs_update_reloc_root(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root);
int btrfs_recover_relocation(struct btrfs_root *root);
int btrfs_reloc_clone_csums(struct btrfs_inode *inode, u64 file_pos, u64 len);
int btrfs_reloc_cow_block(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, struct extent_buffer *buf,
			  struct extent_buffer *cow);
void btrfs_reloc_pre_snapshot(struct btrfs_pending_snapshot *pending,
			      u64 *bytes_to_reserve);
int btrfs_reloc_post_snapshot(struct btrfs_trans_handle *trans,
			      struct btrfs_pending_snapshot *pending);
int btrfs_should_cancel_balance(struct btrfs_fs_info *fs_info);
struct btrfs_root *find_reloc_root(struct btrfs_fs_info *fs_info,
				   u64 bytenr);
int btrfs_should_ignore_reloc_root(struct btrfs_root *root);

/* scrub.c */
int btrfs_scrub_dev(struct btrfs_fs_info *fs_info, u64 devid, u64 start,
		    u64 end, struct btrfs_scrub_progress *progress,
		    int readonly, int is_dev_replace);
void btrfs_scrub_pause(struct btrfs_fs_info *fs_info);
void btrfs_scrub_continue(struct btrfs_fs_info *fs_info);
int btrfs_scrub_cancel(struct btrfs_fs_info *info);
int btrfs_scrub_cancel_dev(struct btrfs_device *dev);
int btrfs_scrub_progress(struct btrfs_fs_info *fs_info, u64 devid,
			 struct btrfs_scrub_progress *progress);
static inline void btrfs_init_full_stripe_locks_tree(
			struct btrfs_full_stripe_locks_tree *locks_root)
{
	locks_root->root = RB_ROOT;
	mutex_init(&locks_root->lock);
}

/* dev-replace.c */
void btrfs_bio_counter_inc_blocked(struct btrfs_fs_info *fs_info);
void btrfs_bio_counter_inc_noblocked(struct btrfs_fs_info *fs_info);
void btrfs_bio_counter_sub(struct btrfs_fs_info *fs_info, s64 amount);

static inline void btrfs_bio_counter_dec(struct btrfs_fs_info *fs_info)
{
	btrfs_bio_counter_sub(fs_info, 1);
}

static inline int is_fstree(u64 rootid)
{
	if (rootid == BTRFS_FS_TREE_OBJECTID ||
	    ((s64)rootid >= (s64)BTRFS_FIRST_FREE_OBJECTID &&
	      !btrfs_qgroup_level(rootid)))
		return 1;
	return 0;
}

static inline int btrfs_defrag_cancelled(struct btrfs_fs_info *fs_info)
{
	return signal_pending(current);
}

/* verity.c */
#ifdef CONFIG_FS_VERITY

extern const struct fsverity_operations btrfs_verityops;
int btrfs_drop_verity_items(struct btrfs_inode *inode);

BTRFS_SETGET_FUNCS(verity_descriptor_encryption, struct btrfs_verity_descriptor_item,
		   encryption, 8);
BTRFS_SETGET_FUNCS(verity_descriptor_size, struct btrfs_verity_descriptor_item,
		   size, 64);
BTRFS_SETGET_STACK_FUNCS(stack_verity_descriptor_encryption,
			 struct btrfs_verity_descriptor_item, encryption, 8);
BTRFS_SETGET_STACK_FUNCS(stack_verity_descriptor_size,
			 struct btrfs_verity_descriptor_item, size, 64);

#else

static inline int btrfs_drop_verity_items(struct btrfs_inode *inode)
{
	return 0;
}

#endif

/* Sanity test specific functions */
#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
void btrfs_test_destroy_inode(struct inode *inode);
static inline int btrfs_is_testing(struct btrfs_fs_info *fs_info)
{
	return test_bit(BTRFS_FS_STATE_DUMMY_FS_INFO, &fs_info->fs_state);
}
#else
static inline int btrfs_is_testing(struct btrfs_fs_info *fs_info)
{
	return 0;
}
#endif

static inline bool btrfs_is_zoned(const struct btrfs_fs_info *fs_info)
{
	return fs_info->zoned != 0;
}

static inline bool btrfs_is_data_reloc_root(const struct btrfs_root *root)
{
	return root->root_key.objectid == BTRFS_DATA_RELOC_TREE_OBJECTID;
}

/*
 * We use page status Private2 to indicate there is an ordered extent with
 * unfinished IO.
 *
 * Rename the Private2 accessors to Ordered, to improve readability.
 */
#define PageOrdered(page)		PagePrivate2(page)
#define SetPageOrdered(page)		SetPagePrivate2(page)
#define ClearPageOrdered(page)		ClearPagePrivate2(page)

#endif
