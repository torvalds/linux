/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_FILE_ITEM_H
#define BTRFS_FILE_ITEM_H

int btrfs_del_csums(struct btrfs_trans_handle *trans,
		    struct btrfs_root *root, u64 bytenr, u64 len);
blk_status_t btrfs_lookup_bio_sums(struct inode *inode, struct bio *bio, u8 *dst);
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
blk_status_t btrfs_csum_one_bio(struct btrfs_inode *inode, struct bio *bio,
				u64 offset, bool one_ordered);
int btrfs_lookup_csums_range(struct btrfs_root *root, u64 start, u64 end,
			     struct list_head *list, int search_commit,
			     bool nowait);
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
