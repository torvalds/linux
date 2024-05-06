/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#ifndef BTRFS_CTREE_H
#define BTRFS_CTREE_H

#include <linux/pagemap.h>
#include <linux/spinlock.h>
#include <linux/rbtree.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/atomic.h>
#include <linux/xarray.h>
#include <linux/refcount.h>
#include <uapi/linux/btrfs_tree.h>
#include "locking.h"
#include "fs.h"
#include "accessors.h"
#include "extent-io-tree.h"

struct extent_buffer;
struct btrfs_block_rsv;
struct btrfs_trans_handle;
struct btrfs_block_group;

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
	/* Stop search if any locks need to be taken (for read) */
	unsigned int nowait:1;
};

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
	/* This reloc root needs to have its buffers lockdep class reset. */
	BTRFS_ROOT_RESET_LOCKDEP_CLASS,
};

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
	/*
	 * Protected by the 'log_mutex' lock but can be read without holding
	 * that lock to avoid unnecessary lock contention, in which case it
	 * should be read using btrfs_get_root_log_transid() except if it's a
	 * log tree in which case it can be directly accessed. Updates to this
	 * field should always use btrfs_set_root_log_transid(), except for log
	 * trees where the field can be updated directly.
	 */
	int log_transid;
	/* No matter the commit succeeds or not*/
	int log_transid_committed;
	/*
	 * Just be updated when the commit succeeds. Use
	 * btrfs_get_root_last_log_commit() and btrfs_set_root_last_log_commit()
	 * to access this field.
	 */
	int last_log_commit;
	pid_t log_start_pid;

	u64 last_trans;

	u64 free_objectid;

	struct btrfs_key defrag_progress;
	struct btrfs_key defrag_max;

	/* The dirty list is only used by non-shareable roots */
	struct list_head dirty_list;

	struct list_head root_list;

	/*
	 * Xarray that keeps track of in-memory inodes, protected by the lock
	 * @inode_lock.
	 */
	struct xarray inodes;

	/*
	 * Xarray that keeps track of delayed nodes of every inode, protected
	 * by @inode_lock.
	 */
	struct xarray delayed_nodes;
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

	/* Used in simple quotas, track root during relocation. */
	u64 relocation_src_root;

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
	u64 alloc_bytenr;
#endif

#ifdef CONFIG_BTRFS_DEBUG
	struct list_head leak_list;
#endif
};

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

static inline int btrfs_get_root_log_transid(const struct btrfs_root *root)
{
	return READ_ONCE(root->log_transid);
}

static inline void btrfs_set_root_log_transid(struct btrfs_root *root, int log_transid)
{
	WRITE_ONCE(root->log_transid, log_transid);
}

static inline int btrfs_get_root_last_log_commit(const struct btrfs_root *root)
{
	return READ_ONCE(root->last_log_commit);
}

static inline void btrfs_set_root_last_log_commit(struct btrfs_root *root, int commit_id)
{
	WRITE_ONCE(root->last_log_commit, commit_id);
}

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
	/* Indicate if we should update the inode's mtime and ctime. */
	bool update_times;
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
	u64 last_index;
	struct extent_state *llseek_cached_state;
};

static inline u32 BTRFS_LEAF_DATA_SIZE(const struct btrfs_fs_info *info)
{
	return info->nodesize - sizeof(struct btrfs_header);
}

static inline u32 BTRFS_MAX_ITEM_SIZE(const struct btrfs_fs_info *info)
{
	return BTRFS_LEAF_DATA_SIZE(info) - sizeof(struct btrfs_item);
}

static inline u32 BTRFS_NODEPTRS_PER_BLOCK(const struct btrfs_fs_info *info)
{
	return BTRFS_LEAF_DATA_SIZE(info) / sizeof(struct btrfs_key_ptr);
}

static inline u32 BTRFS_MAX_XATTR_SIZE(const struct btrfs_fs_info *info)
{
	return BTRFS_MAX_ITEM_SIZE(info) - sizeof(struct btrfs_dir_item);
}

#define BTRFS_BYTES_TO_BLKS(fs_info, bytes) \
				((bytes) >> (fs_info)->sectorsize_bits)

static inline gfp_t btrfs_alloc_write_mask(struct address_space *mapping)
{
	return mapping_gfp_constraint(mapping, ~__GFP_FS);
}

void btrfs_error_unpin_extent_range(struct btrfs_fs_info *fs_info, u64 start, u64 end);
int btrfs_discard_extent(struct btrfs_fs_info *fs_info, u64 bytenr,
			 u64 num_bytes, u64 *actual_bytes);
int btrfs_trim_fs(struct btrfs_fs_info *fs_info, struct fstrim_range *range);

/* ctree.c */
int __init btrfs_ctree_init(void);
void __cold btrfs_ctree_exit(void);

int btrfs_bin_search(struct extent_buffer *eb, int first_slot,
		     const struct btrfs_key *key, int *slot);

int __pure btrfs_comp_cpu_keys(const struct btrfs_key *k1, const struct btrfs_key *k2);

#ifdef __LITTLE_ENDIAN

/*
 * Compare two keys, on little-endian the disk order is same as CPU order and
 * we can avoid the conversion.
 */
static inline int btrfs_comp_keys(const struct btrfs_disk_key *disk_key,
				  const struct btrfs_key *k2)
{
	const struct btrfs_key *k1 = (const struct btrfs_key *)disk_key;

	return btrfs_comp_cpu_keys(k1, k2);
}

#else

/* Compare two keys in a memcmp fashion. */
static inline int btrfs_comp_keys(const struct btrfs_disk_key *disk,
				  const struct btrfs_key *k2)
{
	struct btrfs_key k1;

	btrfs_disk_key_to_cpu(&k1, disk);

	return btrfs_comp_cpu_keys(&k1, k2);
}

#endif

int btrfs_previous_item(struct btrfs_root *root,
			struct btrfs_path *path, u64 min_objectid,
			int type);
int btrfs_previous_extent_item(struct btrfs_root *root,
			struct btrfs_path *path, u64 min_objectid);
void btrfs_set_item_key_safe(struct btrfs_trans_handle *trans,
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
int btrfs_force_cow_block(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  struct extent_buffer *buf,
			  struct extent_buffer *parent, int parent_slot,
			  struct extent_buffer **cow_ret,
			  u64 search_start, u64 empty_size,
			  enum btrfs_lock_nesting nest);
int btrfs_copy_root(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root,
		      struct extent_buffer *buf,
		      struct extent_buffer **cow_ret, u64 new_root_objectid);
bool btrfs_block_can_be_shared(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct extent_buffer *buf);
int btrfs_del_ptr(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct btrfs_path *path, int level, int slot);
void btrfs_extend_item(struct btrfs_trans_handle *trans,
		       struct btrfs_path *path, u32 data_size);
void btrfs_truncate_item(struct btrfs_trans_handle *trans,
			 struct btrfs_path *path, u32 new_size, int from_end);
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

void btrfs_setup_item_for_insert(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
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

int btrfs_next_old_leaf(struct btrfs_root *root, struct btrfs_path *path,
			u64 time_seq);

int btrfs_search_backwards(struct btrfs_root *root, struct btrfs_key *key,
			   struct btrfs_path *path);

int btrfs_get_next_valid_item(struct btrfs_root *root, struct btrfs_key *key,
			      struct btrfs_path *path);

/*
 * Search in @root for a given @key, and store the slot found in @found_key.
 *
 * @root:	The root node of the tree.
 * @key:	The key we are looking for.
 * @found_key:	Will hold the found item.
 * @path:	Holds the current slot/leaf.
 * @iter_ret:	Contains the value returned from btrfs_search_slot or
 * 		btrfs_get_next_valid_item, whichever was executed last.
 *
 * The @iter_ret is an output variable that will contain the return value of
 * btrfs_search_slot, if it encountered an error, or the value returned from
 * btrfs_get_next_valid_item otherwise. That return value can be 0, if a valid
 * slot was found, 1 if there were no more leaves, and <0 if there was an error.
 *
 * It's recommended to use a separate variable for iter_ret and then use it to
 * set the function return value so there's no confusion of the 0/1/errno
 * values stemming from btrfs_search_slot.
 */
#define btrfs_for_each_slot(root, key, found_key, path, iter_ret)		\
	for (iter_ret = btrfs_search_slot(NULL, (root), (key), (path), 0, 0);	\
		(iter_ret) >= 0 &&						\
		(iter_ret = btrfs_get_next_valid_item((root), (found_key), (path))) == 0; \
		(path)->slots[0]++						\
	)

int btrfs_next_old_item(struct btrfs_root *root, struct btrfs_path *path, u64 time_seq);

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
int btrfs_leaf_free_space(const struct extent_buffer *leaf);

static inline int is_fstree(u64 rootid)
{
	if (rootid == BTRFS_FS_TREE_OBJECTID ||
	    ((s64)rootid >= (s64)BTRFS_FIRST_FREE_OBJECTID &&
	      !btrfs_qgroup_level(rootid)))
		return 1;
	return 0;
}

static inline bool btrfs_is_data_reloc_root(const struct btrfs_root *root)
{
	return root->root_key.objectid == BTRFS_DATA_RELOC_TREE_OBJECTID;
}

u16 btrfs_csum_type_size(u16 type);
int btrfs_super_csum_size(const struct btrfs_super_block *s);
const char *btrfs_super_csum_name(u16 csum_type);
const char *btrfs_super_csum_driver(u16 csum_type);
size_t __attribute_const__ btrfs_get_num_csums(void);

/*
 * We use page status Private2 to indicate there is an ordered extent with
 * unfinished IO.
 *
 * Rename the Private2 accessors to Ordered, to improve readability.
 */
#define PageOrdered(page)		PagePrivate2(page)
#define SetPageOrdered(page)		SetPagePrivate2(page)
#define ClearPageOrdered(page)		ClearPagePrivate2(page)
#define folio_test_ordered(folio)	folio_test_private_2(folio)
#define folio_set_ordered(folio)	folio_set_private_2(folio)
#define folio_clear_ordered(folio)	folio_clear_private_2(folio)

#endif
