/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#ifndef BTRFS_ORDERED_DATA_H
#define BTRFS_ORDERED_DATA_H

/* one of these per inode */
struct btrfs_ordered_inode_tree {
	spinlock_t lock;
	struct rb_root tree;
	struct rb_node *last;
};

struct btrfs_ordered_sum {
	/* bytenr is the start of this extent on disk */
	u64 bytenr;

	/*
	 * this is the length in bytes covered by the sums array below.
	 */
	int len;
	struct list_head list;
	/* last field is a variable length array of csums */
	u8 sums[];
};

/*
 * Bits for btrfs_ordered_extent::flags.
 *
 * BTRFS_ORDERED_IO_DONE is set when all of the blocks are written.
 * It is used to make sure metadata is inserted into the tree only once
 * per extent.
 *
 * BTRFS_ORDERED_COMPLETE is set when the extent is removed from the
 * rbtree, just before waking any waiters.  It is used to indicate the
 * IO is done and any metadata is inserted into the tree.
 */
enum {
	/*
	 * Different types for ordered extents, one and only one of the 4 types
	 * need to be set when creating ordered extent.
	 *
	 * REGULAR:	For regular non-compressed COW write
	 * NOCOW:	For NOCOW write into existing non-hole extent
	 * PREALLOC:	For NOCOW write into preallocated extent
	 * COMPRESSED:	For compressed COW write
	 */
	BTRFS_ORDERED_REGULAR,
	BTRFS_ORDERED_NOCOW,
	BTRFS_ORDERED_PREALLOC,
	BTRFS_ORDERED_COMPRESSED,

	/*
	 * Extra bit for direct io, can only be set for
	 * REGULAR/NOCOW/PREALLOC. No direct io for compressed extent.
	 */
	BTRFS_ORDERED_DIRECT,

	/* Extra status bits for ordered extents */

	/* set when all the pages are written */
	BTRFS_ORDERED_IO_DONE,
	/* set when removed from the tree */
	BTRFS_ORDERED_COMPLETE,
	/* We had an io error when writing this out */
	BTRFS_ORDERED_IOERR,
	/* Set when we have to truncate an extent */
	BTRFS_ORDERED_TRUNCATED,
	/* Used during fsync to track already logged extents */
	BTRFS_ORDERED_LOGGED,
	/* We have already logged all the csums of the ordered extent */
	BTRFS_ORDERED_LOGGED_CSUM,
	/* We wait for this extent to complete in the current transaction */
	BTRFS_ORDERED_PENDING,
};

struct btrfs_ordered_extent {
	/* logical offset in the file */
	u64 file_offset;

	/*
	 * These fields directly correspond to the same fields in
	 * btrfs_file_extent_item.
	 */
	u64 disk_bytenr;
	u64 num_bytes;
	u64 disk_num_bytes;

	/* number of bytes that still need writing */
	u64 bytes_left;

	/*
	 * the end of the ordered extent which is behind it but
	 * didn't update disk_i_size. Please see the comment of
	 * btrfs_ordered_update_i_size();
	 */
	u64 outstanding_isize;

	/*
	 * If we get truncated we need to adjust the file extent we enter for
	 * this ordered extent so that we do not expose stale data.
	 */
	u64 truncated_len;

	/* flags (described above) */
	unsigned long flags;

	/* compression algorithm */
	int compress_type;

	/* Qgroup reserved space */
	int qgroup_rsv;

	/* reference count */
	refcount_t refs;

	/* the inode we belong to */
	struct inode *inode;

	/* list of checksums for insertion when the extent io is done */
	struct list_head list;

	/* used for fast fsyncs */
	struct list_head log_list;

	/* used to wait for the BTRFS_ORDERED_COMPLETE bit */
	wait_queue_head_t wait;

	/* our friendly rbtree entry */
	struct rb_node rb_node;

	/* a per root list of all the pending ordered extents */
	struct list_head root_extent_list;

	struct btrfs_work work;

	struct completion completion;
	struct btrfs_work flush_work;
	struct list_head work_list;

	/*
	 * Used to reverse-map physical address returned from ZONE_APPEND write
	 * command in a workqueue context
	 */
	u64 physical;
	struct block_device *bdev;
};

/*
 * calculates the total size you need to allocate for an ordered sum
 * structure spanning 'bytes' in the file
 */
static inline int btrfs_ordered_sum_size(struct btrfs_fs_info *fs_info,
					 unsigned long bytes)
{
	int num_sectors = (int)DIV_ROUND_UP(bytes, fs_info->sectorsize);

	return sizeof(struct btrfs_ordered_sum) + num_sectors * fs_info->csum_size;
}

static inline void
btrfs_ordered_inode_tree_init(struct btrfs_ordered_inode_tree *t)
{
	spin_lock_init(&t->lock);
	t->tree = RB_ROOT;
	t->last = NULL;
}

void btrfs_put_ordered_extent(struct btrfs_ordered_extent *entry);
void btrfs_remove_ordered_extent(struct btrfs_inode *btrfs_inode,
				struct btrfs_ordered_extent *entry);
void btrfs_mark_ordered_io_finished(struct btrfs_inode *inode,
				struct page *page, u64 file_offset,
				u64 num_bytes, btrfs_func_t finish_func,
				bool uptodate);
bool btrfs_dec_test_ordered_pending(struct btrfs_inode *inode,
				    struct btrfs_ordered_extent **cached,
				    u64 file_offset, u64 io_size, int uptodate);
int btrfs_add_ordered_extent(struct btrfs_inode *inode, u64 file_offset,
			     u64 disk_bytenr, u64 num_bytes, u64 disk_num_bytes,
			     int type);
int btrfs_add_ordered_extent_dio(struct btrfs_inode *inode, u64 file_offset,
				 u64 disk_bytenr, u64 num_bytes,
				 u64 disk_num_bytes, int type);
int btrfs_add_ordered_extent_compress(struct btrfs_inode *inode, u64 file_offset,
				      u64 disk_bytenr, u64 num_bytes,
				      u64 disk_num_bytes, int compress_type);
void btrfs_add_ordered_sum(struct btrfs_ordered_extent *entry,
			   struct btrfs_ordered_sum *sum);
struct btrfs_ordered_extent *btrfs_lookup_ordered_extent(struct btrfs_inode *inode,
							 u64 file_offset);
void btrfs_start_ordered_extent(struct btrfs_ordered_extent *entry, int wait);
int btrfs_wait_ordered_range(struct inode *inode, u64 start, u64 len);
struct btrfs_ordered_extent *
btrfs_lookup_first_ordered_extent(struct btrfs_inode *inode, u64 file_offset);
struct btrfs_ordered_extent *btrfs_lookup_first_ordered_range(
			struct btrfs_inode *inode, u64 file_offset, u64 len);
struct btrfs_ordered_extent *btrfs_lookup_ordered_range(
		struct btrfs_inode *inode,
		u64 file_offset,
		u64 len);
void btrfs_get_ordered_extents_for_logging(struct btrfs_inode *inode,
					   struct list_head *list);
u64 btrfs_wait_ordered_extents(struct btrfs_root *root, u64 nr,
			       const u64 range_start, const u64 range_len);
void btrfs_wait_ordered_roots(struct btrfs_fs_info *fs_info, u64 nr,
			      const u64 range_start, const u64 range_len);
void btrfs_lock_and_flush_ordered_range(struct btrfs_inode *inode, u64 start,
					u64 end,
					struct extent_state **cached_state);
int btrfs_split_ordered_extent(struct btrfs_ordered_extent *ordered, u64 pre,
			       u64 post);
int __init ordered_data_init(void);
void __cold ordered_data_exit(void);

#endif
