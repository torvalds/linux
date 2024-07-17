/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_FILE_ITEM_H
#define BTRFS_FILE_ITEM_H

#include <linux/list.h>
#include <uapi/linux/btrfs_tree.h>
#include "accessors.h"

struct extent_map;
struct btrfs_file_extent_item;
struct btrfs_fs_info;
struct btrfs_path;
struct btrfs_bio;
struct btrfs_trans_handle;
struct btrfs_root;
struct btrfs_ordered_sum;
struct btrfs_path;
struct btrfs_inode;

#define BTRFS_FILE_EXTENT_INLINE_DATA_START		\
		(offsetof(struct btrfs_file_extent_item, disk_bytenr))

static inline u32 BTRFS_MAX_INLINE_DATA_SIZE(const struct btrfs_fs_info *info)
{
	return BTRFS_MAX_ITEM_SIZE(info) - BTRFS_FILE_EXTENT_INLINE_DATA_START;
}

/*
 * Return the number of bytes used by the item on disk, minus the size of any
 * extent headers.  If a file is compressed on disk, this is the compressed
 * size.
 */
static inline u32 btrfs_file_extent_inline_item_len(
						const struct extent_buffer *eb,
						int nr)
{
	return btrfs_item_size(eb, nr) - BTRFS_FILE_EXTENT_INLINE_DATA_START;
}

static inline unsigned long btrfs_file_extent_inline_start(
				const struct btrfs_file_extent_item *e)
{
	return (unsigned long)e + BTRFS_FILE_EXTENT_INLINE_DATA_START;
}

static inline u32 btrfs_file_extent_calc_inline_size(u32 datasize)
{
	return BTRFS_FILE_EXTENT_INLINE_DATA_START + datasize;
}

int btrfs_del_csums(struct btrfs_trans_handle *trans,
		    struct btrfs_root *root, u64 bytenr, u64 len);
blk_status_t btrfs_lookup_bio_sums(struct btrfs_bio *bbio);
int btrfs_insert_hole_extent(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root, u64 objectid, u64 pos,
			     u64 num_bytes);
int btrfs_lookup_file_extent(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct btrfs_path *path, u64 objectid,
			     u64 bytenr, int mod);
int btrfs_csum_file_blocks(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   struct btrfs_ordered_sum *sums);
blk_status_t btrfs_csum_one_bio(struct btrfs_bio *bbio);
blk_status_t btrfs_alloc_dummy_sum(struct btrfs_bio *bbio);
int btrfs_lookup_csums_range(struct btrfs_root *root, u64 start, u64 end,
			     struct list_head *list, int search_commit,
			     bool nowait);
int btrfs_lookup_csums_list(struct btrfs_root *root, u64 start, u64 end,
			    struct list_head *list, bool nowait);
int btrfs_lookup_csums_bitmap(struct btrfs_root *root, struct btrfs_path *path,
			      u64 start, u64 end, u8 *csum_buf,
			      unsigned long *csum_bitmap);
void btrfs_extent_item_to_extent_map(struct btrfs_inode *inode,
				     const struct btrfs_path *path,
				     struct btrfs_file_extent_item *fi,
				     struct extent_map *em);
int btrfs_inode_clear_file_extent_range(struct btrfs_inode *inode, u64 start,
					u64 len);
int btrfs_inode_set_file_extent_range(struct btrfs_inode *inode, u64 start, u64 len);
void btrfs_inode_safe_disk_i_size_write(struct btrfs_inode *inode, u64 new_i_size);
u64 btrfs_file_extent_end(const struct btrfs_path *path);

#endif
