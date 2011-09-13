/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef __DISKIO__
#define __DISKIO__

#define BTRFS_SUPER_INFO_OFFSET (64 * 1024)
#define BTRFS_SUPER_INFO_SIZE 4096

#define BTRFS_SUPER_MIRROR_MAX	 3
#define BTRFS_SUPER_MIRROR_SHIFT 12

static inline u64 btrfs_sb_offset(int mirror)
{
	u64 start = 16 * 1024;
	if (mirror)
		return start << (BTRFS_SUPER_MIRROR_SHIFT * mirror);
	return BTRFS_SUPER_INFO_OFFSET;
}

struct btrfs_device;
struct btrfs_fs_devices;

struct extent_buffer *read_tree_block(struct btrfs_root *root, u64 bytenr,
				      u32 blocksize, u64 parent_transid);
int readahead_tree_block(struct btrfs_root *root, u64 bytenr, u32 blocksize,
			 u64 parent_transid);
int reada_tree_block_flagged(struct btrfs_root *root, u64 bytenr, u32 blocksize,
			 int mirror_num, struct extent_buffer **eb);
struct extent_buffer *btrfs_find_create_tree_block(struct btrfs_root *root,
						   u64 bytenr, u32 blocksize);
void clean_tree_block(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root, struct extent_buffer *buf);
int open_ctree(struct super_block *sb,
	       struct btrfs_fs_devices *fs_devices,
	       char *options);
int close_ctree(struct btrfs_root *root);
int write_ctree_super(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root, int max_mirrors);
struct buffer_head *btrfs_read_dev_super(struct block_device *bdev);
int btrfs_commit_super(struct btrfs_root *root);
int btrfs_error_commit_super(struct btrfs_root *root);
struct extent_buffer *btrfs_find_tree_block(struct btrfs_root *root,
					    u64 bytenr, u32 blocksize);
struct btrfs_root *btrfs_read_fs_root_no_radix(struct btrfs_root *tree_root,
					       struct btrfs_key *location);
struct btrfs_root *btrfs_read_fs_root_no_name(struct btrfs_fs_info *fs_info,
					      struct btrfs_key *location);
int btrfs_cleanup_fs_roots(struct btrfs_fs_info *fs_info);
void btrfs_btree_balance_dirty(struct btrfs_root *root, unsigned long nr);
void __btrfs_btree_balance_dirty(struct btrfs_root *root, unsigned long nr);
void btrfs_free_fs_root(struct btrfs_fs_info *fs_info, struct btrfs_root *root);
void btrfs_mark_buffer_dirty(struct extent_buffer *buf);
int btrfs_buffer_uptodate(struct extent_buffer *buf, u64 parent_transid,
			  int atomic);
int btrfs_set_buffer_uptodate(struct extent_buffer *buf);
int btrfs_read_buffer(struct extent_buffer *buf, u64 parent_transid);
u32 btrfs_csum_data(struct btrfs_root *root, char *data, u32 seed, size_t len);
void btrfs_csum_final(u32 crc, char *result);
int btrfs_bio_wq_end_io(struct btrfs_fs_info *info, struct bio *bio,
			int metadata);
int btrfs_wq_submit_bio(struct btrfs_fs_info *fs_info, struct inode *inode,
			int rw, struct bio *bio, int mirror_num,
			unsigned long bio_flags, u64 bio_offset,
			extent_submit_bio_hook_t *submit_bio_start,
			extent_submit_bio_hook_t *submit_bio_done);
unsigned long btrfs_async_submit_limit(struct btrfs_fs_info *info);
int btrfs_write_tree_block(struct extent_buffer *buf);
int btrfs_wait_tree_block_writeback(struct extent_buffer *buf);
int btrfs_init_log_root_tree(struct btrfs_trans_handle *trans,
			     struct btrfs_fs_info *fs_info);
int btrfs_add_log_tree(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root);
int btrfs_cleanup_transaction(struct btrfs_root *root);
void btrfs_cleanup_one_transaction(struct btrfs_transaction *trans,
				  struct btrfs_root *root);
void btrfs_abort_devices(struct btrfs_root *root);
struct btrfs_root *btrfs_create_tree(struct btrfs_trans_handle *trans,
				     struct btrfs_fs_info *fs_info,
				     u64 objectid);
int btree_lock_page_hook(struct page *page, void *data,
				void (*flush_fn)(void *));

#ifdef CONFIG_DEBUG_LOCK_ALLOC
void btrfs_init_lockdep(void);
void btrfs_set_buffer_lockdep_class(u64 objectid,
			            struct extent_buffer *eb, int level);
#else
static inline void btrfs_init_lockdep(void)
{ }
static inline void btrfs_set_buffer_lockdep_class(u64 objectid,
					struct extent_buffer *eb, int level)
{
}
#endif
#endif
