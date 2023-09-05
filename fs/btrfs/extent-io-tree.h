/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_EXTENT_IO_TREE_H
#define BTRFS_EXTENT_IO_TREE_H

#include "misc.h"

struct extent_changeset;

/* Bits for the extent state */
enum {
	ENUM_BIT(EXTENT_DIRTY),
	ENUM_BIT(EXTENT_UPTODATE),
	ENUM_BIT(EXTENT_LOCKED),
	ENUM_BIT(EXTENT_NEW),
	ENUM_BIT(EXTENT_DELALLOC),
	ENUM_BIT(EXTENT_DEFRAG),
	ENUM_BIT(EXTENT_BOUNDARY),
	ENUM_BIT(EXTENT_NODATASUM),
	ENUM_BIT(EXTENT_CLEAR_META_RESV),
	ENUM_BIT(EXTENT_NEED_WAIT),
	ENUM_BIT(EXTENT_NORESERVE),
	ENUM_BIT(EXTENT_QGROUP_RESERVED),
	ENUM_BIT(EXTENT_CLEAR_DATA_RESV),
	/*
	 * Must be cleared only during ordered extent completion or on error
	 * paths if we did not manage to submit bios and create the ordered
	 * extents for the range.  Should not be cleared during page release
	 * and page invalidation (if there is an ordered extent in flight),
	 * that is left for the ordered extent completion.
	 */
	ENUM_BIT(EXTENT_DELALLOC_NEW),
	/*
	 * When an ordered extent successfully completes for a region marked as
	 * a new delalloc range, use this flag when clearing a new delalloc
	 * range to indicate that the VFS' inode number of bytes should be
	 * incremented and the inode's new delalloc bytes decremented, in an
	 * atomic way to prevent races with stat(2).
	 */
	ENUM_BIT(EXTENT_ADD_INODE_BYTES),
	/*
	 * Set during truncate when we're clearing an entire range and we just
	 * want the extent states to go away.
	 */
	ENUM_BIT(EXTENT_CLEAR_ALL_BITS),

	/*
	 * This must be last.
	 *
	 * Bit not representing a state but a request for NOWAIT semantics,
	 * e.g. when allocating memory, and must be masked out from the other
	 * bits.
	 */
	ENUM_BIT(EXTENT_NOWAIT)
};

#define EXTENT_DO_ACCOUNTING    (EXTENT_CLEAR_META_RESV | \
				 EXTENT_CLEAR_DATA_RESV)
#define EXTENT_CTLBITS		(EXTENT_DO_ACCOUNTING | \
				 EXTENT_ADD_INODE_BYTES | \
				 EXTENT_CLEAR_ALL_BITS)

/*
 * Redefined bits above which are used only in the device allocation tree,
 * shouldn't be using EXTENT_LOCKED / EXTENT_BOUNDARY / EXTENT_CLEAR_META_RESV
 * / EXTENT_CLEAR_DATA_RESV because they have special meaning to the bit
 * manipulation functions
 */
#define CHUNK_ALLOCATED				EXTENT_DIRTY
#define CHUNK_TRIMMED				EXTENT_DEFRAG
#define CHUNK_STATE_MASK			(CHUNK_ALLOCATED |		\
						 CHUNK_TRIMMED)

enum {
	IO_TREE_FS_PINNED_EXTENTS,
	IO_TREE_FS_EXCLUDED_EXTENTS,
	IO_TREE_BTREE_INODE_IO,
	IO_TREE_INODE_IO,
	IO_TREE_RELOC_BLOCKS,
	IO_TREE_TRANS_DIRTY_PAGES,
	IO_TREE_ROOT_DIRTY_LOG_PAGES,
	IO_TREE_INODE_FILE_EXTENT,
	IO_TREE_LOG_CSUM_RANGE,
	IO_TREE_SELFTEST,
	IO_TREE_DEVICE_ALLOC_STATE,
};

struct extent_io_tree {
	struct rb_root state;
	struct btrfs_fs_info *fs_info;
	/* Inode associated with this tree, or NULL. */
	struct btrfs_inode *inode;

	/* Who owns this io tree, should be one of IO_TREE_* */
	u8 owner;

	spinlock_t lock;
};

struct extent_state {
	u64 start;
	u64 end; /* inclusive */
	struct rb_node rb_node;

	/* ADD NEW ELEMENTS AFTER THIS */
	wait_queue_head_t wq;
	refcount_t refs;
	u32 state;

#ifdef CONFIG_BTRFS_DEBUG
	struct list_head leak_list;
#endif
};

void extent_io_tree_init(struct btrfs_fs_info *fs_info,
			 struct extent_io_tree *tree, unsigned int owner);
void extent_io_tree_release(struct extent_io_tree *tree);

int lock_extent(struct extent_io_tree *tree, u64 start, u64 end,
		struct extent_state **cached);

int try_lock_extent(struct extent_io_tree *tree, u64 start, u64 end,
		    struct extent_state **cached);

int __init extent_state_init_cachep(void);
void __cold extent_state_free_cachep(void);

u64 count_range_bits(struct extent_io_tree *tree,
		     u64 *start, u64 search_end,
		     u64 max_bytes, u32 bits, int contig,
		     struct extent_state **cached_state);

void free_extent_state(struct extent_state *state);
int test_range_bit(struct extent_io_tree *tree, u64 start, u64 end,
		   u32 bits, int filled, struct extent_state *cached_state);
int clear_record_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
			     u32 bits, struct extent_changeset *changeset);
int __clear_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		       u32 bits, struct extent_state **cached,
		       struct extent_changeset *changeset);

static inline int clear_extent_bit(struct extent_io_tree *tree, u64 start,
				   u64 end, u32 bits,
				   struct extent_state **cached)
{
	return __clear_extent_bit(tree, start, end, bits, cached, NULL);
}

static inline int unlock_extent(struct extent_io_tree *tree, u64 start, u64 end,
				struct extent_state **cached)
{
	return __clear_extent_bit(tree, start, end, EXTENT_LOCKED, cached, NULL);
}

static inline int clear_extent_bits(struct extent_io_tree *tree, u64 start,
				    u64 end, u32 bits)
{
	return clear_extent_bit(tree, start, end, bits, NULL);
}

int set_record_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
			   u32 bits, struct extent_changeset *changeset);
int set_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		   u32 bits, struct extent_state **cached_state);

static inline int clear_extent_uptodate(struct extent_io_tree *tree, u64 start,
		u64 end, struct extent_state **cached_state)
{
	return __clear_extent_bit(tree, start, end, EXTENT_UPTODATE,
				  cached_state, NULL);
}

static inline int clear_extent_dirty(struct extent_io_tree *tree, u64 start,
				     u64 end, struct extent_state **cached)
{
	return clear_extent_bit(tree, start, end,
				EXTENT_DIRTY | EXTENT_DELALLOC |
				EXTENT_DO_ACCOUNTING, cached);
}

int convert_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		       u32 bits, u32 clear_bits,
		       struct extent_state **cached_state);

int find_first_extent_bit(struct extent_io_tree *tree, u64 start,
			  u64 *start_ret, u64 *end_ret, u32 bits,
			  struct extent_state **cached_state);
void find_first_clear_extent_bit(struct extent_io_tree *tree, u64 start,
				 u64 *start_ret, u64 *end_ret, u32 bits);
int find_contiguous_extent_bit(struct extent_io_tree *tree, u64 start,
			       u64 *start_ret, u64 *end_ret, u32 bits);
bool btrfs_find_delalloc_range(struct extent_io_tree *tree, u64 *start,
			       u64 *end, u64 max_bytes,
			       struct extent_state **cached_state);
void wait_extent_bit(struct extent_io_tree *tree, u64 start, u64 end, u32 bits,
		     struct extent_state **cached_state);

#endif /* BTRFS_EXTENT_IO_TREE_H */
