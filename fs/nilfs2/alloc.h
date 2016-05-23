/*
 * alloc.h - persistent object (dat entry/disk inode) allocator/deallocator
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Originally written by Koji Sato.
 * Two allocators were unified by Ryusuke Konishi and Amagai Yoshiji.
 */

#ifndef _NILFS_ALLOC_H
#define _NILFS_ALLOC_H

#include <linux/types.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>

/**
 * nilfs_palloc_entries_per_group - get the number of entries per group
 * @inode: inode of metadata file using this allocator
 *
 * The number of entries per group is defined by the number of bits
 * that a bitmap block can maintain.
 */
static inline unsigned long
nilfs_palloc_entries_per_group(const struct inode *inode)
{
	return 1UL << (inode->i_blkbits + 3 /* log2(8 = CHAR_BITS) */);
}

int nilfs_palloc_init_blockgroup(struct inode *, unsigned);
int nilfs_palloc_get_entry_block(struct inode *, __u64, int,
				 struct buffer_head **);
void *nilfs_palloc_block_get_entry(const struct inode *, __u64,
				   const struct buffer_head *, void *);

int nilfs_palloc_count_max_entries(struct inode *, u64, u64 *);

/**
 * nilfs_palloc_req - persistent allocator request and reply
 * @pr_entry_nr: entry number (vblocknr or inode number)
 * @pr_desc_bh: buffer head of the buffer containing block group descriptors
 * @pr_bitmap_bh: buffer head of the buffer containing a block group bitmap
 * @pr_entry_bh: buffer head of the buffer containing translation entries
 */
struct nilfs_palloc_req {
	__u64 pr_entry_nr;
	struct buffer_head *pr_desc_bh;
	struct buffer_head *pr_bitmap_bh;
	struct buffer_head *pr_entry_bh;
};

int nilfs_palloc_prepare_alloc_entry(struct inode *,
				     struct nilfs_palloc_req *);
void nilfs_palloc_commit_alloc_entry(struct inode *,
				     struct nilfs_palloc_req *);
void nilfs_palloc_abort_alloc_entry(struct inode *, struct nilfs_palloc_req *);
void nilfs_palloc_commit_free_entry(struct inode *, struct nilfs_palloc_req *);
int nilfs_palloc_prepare_free_entry(struct inode *, struct nilfs_palloc_req *);
void nilfs_palloc_abort_free_entry(struct inode *, struct nilfs_palloc_req *);
int nilfs_palloc_freev(struct inode *, __u64 *, size_t);

#define nilfs_set_bit_atomic		ext2_set_bit_atomic
#define nilfs_clear_bit_atomic		ext2_clear_bit_atomic
#define nilfs_find_next_zero_bit	find_next_zero_bit_le
#define nilfs_find_next_bit		find_next_bit_le

/**
 * struct nilfs_bh_assoc - block offset and buffer head association
 * @blkoff: block offset
 * @bh: buffer head
 */
struct nilfs_bh_assoc {
	unsigned long blkoff;
	struct buffer_head *bh;
};

/**
 * struct nilfs_palloc_cache - persistent object allocator cache
 * @lock: cache protecting lock
 * @prev_desc: blockgroup descriptors cache
 * @prev_bitmap: blockgroup bitmap cache
 * @prev_entry: translation entries cache
 */
struct nilfs_palloc_cache {
	spinlock_t lock;
	struct nilfs_bh_assoc prev_desc;
	struct nilfs_bh_assoc prev_bitmap;
	struct nilfs_bh_assoc prev_entry;
};

void nilfs_palloc_setup_cache(struct inode *inode,
			      struct nilfs_palloc_cache *cache);
void nilfs_palloc_clear_cache(struct inode *inode);
void nilfs_palloc_destroy_cache(struct inode *inode);

#endif	/* _NILFS_ALLOC_H */
