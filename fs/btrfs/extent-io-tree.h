/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_EXTENT_IO_TREE_H
#define BTRFS_EXTENT_IO_TREE_H

struct extent_changeset;
struct io_failure_record;

/* Bits for the extent state */
#define EXTENT_DIRTY		(1U << 0)
#define EXTENT_UPTODATE		(1U << 1)
#define EXTENT_LOCKED		(1U << 2)
#define EXTENT_NEW		(1U << 3)
#define EXTENT_DELALLOC		(1U << 4)
#define EXTENT_DEFRAG		(1U << 5)
#define EXTENT_BOUNDARY		(1U << 6)
#define EXTENT_NODATASUM	(1U << 7)
#define EXTENT_CLEAR_META_RESV	(1U << 8)
#define EXTENT_NEED_WAIT	(1U << 9)
#define EXTENT_DAMAGED		(1U << 10)
#define EXTENT_NORESERVE	(1U << 11)
#define EXTENT_QGROUP_RESERVED	(1U << 12)
#define EXTENT_CLEAR_DATA_RESV	(1U << 13)
#define EXTENT_DELALLOC_NEW	(1U << 14)
#define EXTENT_DO_ACCOUNTING    (EXTENT_CLEAR_META_RESV | \
				 EXTENT_CLEAR_DATA_RESV)
#define EXTENT_CTLBITS		(EXTENT_DO_ACCOUNTING)

/*
 * Redefined bits above which are used only in the device allocation tree,
 * shouldn't be using EXTENT_LOCKED / EXTENT_BOUNDARY / EXTENT_CLEAR_META_RESV
 * / EXTENT_CLEAR_DATA_RESV because they have special meaning to the bit
 * manipulation functions
 */
#define CHUNK_ALLOCATED				EXTENT_DIRTY
#define CHUNK_TRIMMED				EXTENT_DEFRAG

enum {
	IO_TREE_FS_INFO_FREED_EXTENTS0,
	IO_TREE_FS_INFO_FREED_EXTENTS1,
	IO_TREE_INODE_IO,
	IO_TREE_INODE_IO_FAILURE,
	IO_TREE_RELOC_BLOCKS,
	IO_TREE_TRANS_DIRTY_PAGES,
	IO_TREE_ROOT_DIRTY_LOG_PAGES,
	IO_TREE_SELFTEST,
};

struct extent_io_tree {
	struct rb_root state;
	struct btrfs_fs_info *fs_info;
	void *private_data;
	u64 dirty_bytes;
	bool track_uptodate;

	/* Who owns this io tree, should be one of IO_TREE_* */
	u8 owner;

	spinlock_t lock;
	const struct extent_io_ops *ops;
};

struct extent_state {
	u64 start;
	u64 end; /* inclusive */
	struct rb_node rb_node;

	/* ADD NEW ELEMENTS AFTER THIS */
	wait_queue_head_t wq;
	refcount_t refs;
	unsigned state;

	struct io_failure_record *failrec;

#ifdef CONFIG_BTRFS_DEBUG
	struct list_head leak_list;
#endif
};

int __init extent_state_cache_init(void);
void __cold extent_state_cache_exit(void);

void extent_io_tree_init(struct btrfs_fs_info *fs_info,
			 struct extent_io_tree *tree, unsigned int owner,
			 void *private_data);
void extent_io_tree_release(struct extent_io_tree *tree);

int lock_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
		     struct extent_state **cached);

static inline int lock_extent(struct extent_io_tree *tree, u64 start, u64 end)
{
	return lock_extent_bits(tree, start, end, NULL);
}

int try_lock_extent(struct extent_io_tree *tree, u64 start, u64 end);

int __init extent_io_init(void);
void __cold extent_io_exit(void);

u64 count_range_bits(struct extent_io_tree *tree,
		     u64 *start, u64 search_end,
		     u64 max_bytes, unsigned bits, int contig);

void free_extent_state(struct extent_state *state);
int test_range_bit(struct extent_io_tree *tree, u64 start, u64 end,
		   unsigned bits, int filled,
		   struct extent_state *cached_state);
int clear_record_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
		unsigned bits, struct extent_changeset *changeset);
int clear_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		     unsigned bits, int wake, int delete,
		     struct extent_state **cached);
int __clear_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		     unsigned bits, int wake, int delete,
		     struct extent_state **cached, gfp_t mask,
		     struct extent_changeset *changeset);

static inline int unlock_extent(struct extent_io_tree *tree, u64 start, u64 end)
{
	return clear_extent_bit(tree, start, end, EXTENT_LOCKED, 1, 0, NULL);
}

static inline int unlock_extent_cached(struct extent_io_tree *tree, u64 start,
		u64 end, struct extent_state **cached)
{
	return __clear_extent_bit(tree, start, end, EXTENT_LOCKED, 1, 0, cached,
				GFP_NOFS, NULL);
}

static inline int unlock_extent_cached_atomic(struct extent_io_tree *tree,
		u64 start, u64 end, struct extent_state **cached)
{
	return __clear_extent_bit(tree, start, end, EXTENT_LOCKED, 1, 0, cached,
				GFP_ATOMIC, NULL);
}

static inline int clear_extent_bits(struct extent_io_tree *tree, u64 start,
		u64 end, unsigned bits)
{
	int wake = 0;

	if (bits & EXTENT_LOCKED)
		wake = 1;

	return clear_extent_bit(tree, start, end, bits, wake, 0, NULL);
}

int set_record_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
			   unsigned bits, struct extent_changeset *changeset);
int set_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		   unsigned bits, u64 *failed_start,
		   struct extent_state **cached_state, gfp_t mask);
int set_extent_bits_nowait(struct extent_io_tree *tree, u64 start, u64 end,
			   unsigned bits);

static inline int set_extent_bits(struct extent_io_tree *tree, u64 start,
		u64 end, unsigned bits)
{
	return set_extent_bit(tree, start, end, bits, NULL, NULL, GFP_NOFS);
}

static inline int clear_extent_uptodate(struct extent_io_tree *tree, u64 start,
		u64 end, struct extent_state **cached_state)
{
	return __clear_extent_bit(tree, start, end, EXTENT_UPTODATE, 0, 0,
				cached_state, GFP_NOFS, NULL);
}

static inline int set_extent_dirty(struct extent_io_tree *tree, u64 start,
		u64 end, gfp_t mask)
{
	return set_extent_bit(tree, start, end, EXTENT_DIRTY, NULL,
			      NULL, mask);
}

static inline int clear_extent_dirty(struct extent_io_tree *tree, u64 start,
				     u64 end, struct extent_state **cached)
{
	return clear_extent_bit(tree, start, end,
				EXTENT_DIRTY | EXTENT_DELALLOC |
				EXTENT_DO_ACCOUNTING, 0, 0, cached);
}

int convert_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		       unsigned bits, unsigned clear_bits,
		       struct extent_state **cached_state);

static inline int set_extent_delalloc(struct extent_io_tree *tree, u64 start,
				      u64 end, unsigned int extra_bits,
				      struct extent_state **cached_state)
{
	return set_extent_bit(tree, start, end,
			      EXTENT_DELALLOC | EXTENT_UPTODATE | extra_bits,
			      NULL, cached_state, GFP_NOFS);
}

static inline int set_extent_defrag(struct extent_io_tree *tree, u64 start,
		u64 end, struct extent_state **cached_state)
{
	return set_extent_bit(tree, start, end,
			      EXTENT_DELALLOC | EXTENT_UPTODATE | EXTENT_DEFRAG,
			      NULL, cached_state, GFP_NOFS);
}

static inline int set_extent_new(struct extent_io_tree *tree, u64 start,
		u64 end)
{
	return set_extent_bit(tree, start, end, EXTENT_NEW, NULL, NULL,
			GFP_NOFS);
}

static inline int set_extent_uptodate(struct extent_io_tree *tree, u64 start,
		u64 end, struct extent_state **cached_state, gfp_t mask)
{
	return set_extent_bit(tree, start, end, EXTENT_UPTODATE, NULL,
			      cached_state, mask);
}

int find_first_extent_bit(struct extent_io_tree *tree, u64 start,
			  u64 *start_ret, u64 *end_ret, unsigned bits,
			  struct extent_state **cached_state);
void find_first_clear_extent_bit(struct extent_io_tree *tree, u64 start,
				 u64 *start_ret, u64 *end_ret, unsigned bits);
int extent_invalidatepage(struct extent_io_tree *tree,
			  struct page *page, unsigned long offset);
bool btrfs_find_delalloc_range(struct extent_io_tree *tree, u64 *start,
			       u64 *end, u64 max_bytes,
			       struct extent_state **cached_state);

/* This should be reworked in the future and put elsewhere. */
int get_state_failrec(struct extent_io_tree *tree, u64 start,
		      struct io_failure_record **failrec);
int set_state_failrec(struct extent_io_tree *tree, u64 start,
		      struct io_failure_record *failrec);
void btrfs_free_io_failure_record(struct btrfs_inode *inode, u64 start,
		u64 end);
int btrfs_get_io_failure_record(struct inode *inode, u64 start, u64 end,
				struct io_failure_record **failrec_ret);
int free_io_failure(struct extent_io_tree *failure_tree,
		    struct extent_io_tree *io_tree,
		    struct io_failure_record *rec);
int clean_io_failure(struct btrfs_fs_info *fs_info,
		     struct extent_io_tree *failure_tree,
		     struct extent_io_tree *io_tree, u64 start,
		     struct page *page, u64 ino, unsigned int pg_offset);

#endif /* BTRFS_EXTENT_IO_TREE_H */
