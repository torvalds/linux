/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#ifndef BTRFS_ORDERED_DATA_H
#define BTRFS_ORDERED_DATA_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/refcount.h>
#include <linux/completion.h>
#include <linux/rbtree.h>
#include <linux/wait.h>
#include "async-thread.h"

struct inode;
struct page;
struct extent_state;
struct btrfs_inode;
struct btrfs_root;
struct btrfs_fs_info;

struct btrfs_ordered_sum {
	/*
	 * Logical start address and length for of the blocks covered by
	 * the sums array.
	 */
	u64 logical;
	u32 len;

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
	/* BTRFS_IOC_ENCODED_WRITE */
	BTRFS_ORDERED_ENCODED,
};

/* BTRFS_ORDERED_* flags that specify the type of the extent. */
#define BTRFS_ORDERED_TYPE_FLAGS ((1UL << BTRFS_ORDERED_REGULAR) |	\
				  (1UL << BTRFS_ORDERED_NOCOW) |	\
				  (1UL << BTRFS_ORDERED_PREALLOC) |	\
				  (1UL << BTRFS_ORDERED_COMPRESSED) |	\
				  (1UL << BTRFS_ORDERED_DIRECT) |	\
				  (1UL << BTRFS_ORDERED_ENCODED))

struct btrfs_ordered_extent {
	/* logical offset in the file */
	u64 file_offset;

	/*
	 * These fields directly correspond to the same fields in
	 * btrfs_file_extent_item.
	 */
	u64 num_bytes;
	u64 ram_bytes;
	u64 disk_bytenr;
	u64 disk_num_bytes;
	u64 offset;

	/* number of bytes that still need writing */
	u64 bytes_left;

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

	struct list_head bioc_list;
};

int btrfs_finish_one_ordered(struct btrfs_ordered_extent *ordered_extent);
int btrfs_finish_ordered_io(struct btrfs_ordered_extent *ordered_extent);

void btrfs_put_ordered_extent(struct btrfs_ordered_extent *entry);
void btrfs_remove_ordered_extent(struct btrfs_inode *btrfs_inode,
				struct btrfs_ordered_extent *entry);
void btrfs_finish_ordered_extent(struct btrfs_ordered_extent *ordered,
				 struct page *page, u64 file_offset, u64 len,
				 bool uptodate);
void btrfs_mark_ordered_io_finished(struct btrfs_inode *inode,
				struct page *page, u64 file_offset,
				u64 num_bytes, bool uptodate);
bool btrfs_dec_test_ordered_pending(struct btrfs_inode *inode,
				    struct btrfs_ordered_extent **cached,
				    u64 file_offset, u64 io_size);

/*
 * This represents details about the target file extent item of a write operation.
 */
struct btrfs_file_extent {
	u64 disk_bytenr;
	u64 disk_num_bytes;
	u64 num_bytes;
	u64 ram_bytes;
	u64 offset;
	u8 compression;
};

struct btrfs_ordered_extent *btrfs_alloc_ordered_extent(
			struct btrfs_inode *inode, u64 file_offset,
			const struct btrfs_file_extent *file_extent, unsigned long flags);
void btrfs_add_ordered_sum(struct btrfs_ordered_extent *entry,
			   struct btrfs_ordered_sum *sum);
struct btrfs_ordered_extent *btrfs_lookup_ordered_extent(struct btrfs_inode *inode,
							 u64 file_offset);
void btrfs_start_ordered_extent(struct btrfs_ordered_extent *entry);
int btrfs_wait_ordered_range(struct btrfs_inode *inode, u64 start, u64 len);
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
			       const struct btrfs_block_group *bg);
void btrfs_wait_ordered_roots(struct btrfs_fs_info *fs_info, u64 nr,
			      const struct btrfs_block_group *bg);
void btrfs_lock_and_flush_ordered_range(struct btrfs_inode *inode, u64 start,
					u64 end,
					struct extent_state **cached_state);
bool btrfs_try_lock_ordered_range(struct btrfs_inode *inode, u64 start, u64 end,
				  struct extent_state **cached_state);
struct btrfs_ordered_extent *btrfs_split_ordered_extent(
			struct btrfs_ordered_extent *ordered, u64 len);
void btrfs_mark_ordered_extent_error(struct btrfs_ordered_extent *ordered);
int __init ordered_data_init(void);
void __cold ordered_data_exit(void);

#endif
