/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2009 Oracle.  All rights reserved.
 */

#ifndef BTRFS_FREE_SPACE_CACHE_H
#define BTRFS_FREE_SPACE_CACHE_H

struct btrfs_free_space {
	struct rb_node offset_index;
	u64 offset;
	u64 bytes;
	u64 max_extent_size;
	unsigned long *bitmap;
	struct list_head list;
};

struct btrfs_free_space_ctl {
	spinlock_t tree_lock;
	struct rb_root free_space_offset;
	u64 free_space;
	int extents_thresh;
	int free_extents;
	int total_bitmaps;
	int unit;
	u64 start;
	const struct btrfs_free_space_op *op;
	void *private;
	struct mutex cache_writeout_mutex;
	struct list_head trimming_ranges;
};

struct btrfs_free_space_op {
	void (*recalc_thresholds)(struct btrfs_free_space_ctl *ctl);
	bool (*use_bitmap)(struct btrfs_free_space_ctl *ctl,
			   struct btrfs_free_space *info);
};

struct btrfs_io_ctl;

struct inode *lookup_free_space_inode(
		struct btrfs_block_group_cache *block_group,
		struct btrfs_path *path);
int create_free_space_inode(struct btrfs_trans_handle *trans,
			    struct btrfs_block_group_cache *block_group,
			    struct btrfs_path *path);

int btrfs_check_trunc_cache_free_space(struct btrfs_fs_info *fs_info,
				       struct btrfs_block_rsv *rsv);
int btrfs_truncate_free_space_cache(struct btrfs_trans_handle *trans,
				    struct btrfs_block_group_cache *block_group,
				    struct inode *inode);
int load_free_space_cache(struct btrfs_block_group_cache *block_group);
int btrfs_wait_cache_io(struct btrfs_trans_handle *trans,
			struct btrfs_block_group_cache *block_group,
			struct btrfs_path *path);
int btrfs_write_out_cache(struct btrfs_trans_handle *trans,
			  struct btrfs_block_group_cache *block_group,
			  struct btrfs_path *path);
struct inode *lookup_free_ino_inode(struct btrfs_root *root,
				    struct btrfs_path *path);
int create_free_ino_inode(struct btrfs_root *root,
			  struct btrfs_trans_handle *trans,
			  struct btrfs_path *path);
int load_free_ino_cache(struct btrfs_fs_info *fs_info,
			struct btrfs_root *root);
int btrfs_write_out_ino_cache(struct btrfs_root *root,
			      struct btrfs_trans_handle *trans,
			      struct btrfs_path *path,
			      struct inode *inode);

void btrfs_init_free_space_ctl(struct btrfs_block_group_cache *block_group);
int __btrfs_add_free_space(struct btrfs_fs_info *fs_info,
			   struct btrfs_free_space_ctl *ctl,
			   u64 bytenr, u64 size);
static inline int
btrfs_add_free_space(struct btrfs_block_group_cache *block_group,
		     u64 bytenr, u64 size)
{
	return __btrfs_add_free_space(block_group->fs_info,
				      block_group->free_space_ctl,
				      bytenr, size);
}
int btrfs_remove_free_space(struct btrfs_block_group_cache *block_group,
			    u64 bytenr, u64 size);
void __btrfs_remove_free_space_cache(struct btrfs_free_space_ctl *ctl);
void btrfs_remove_free_space_cache(struct btrfs_block_group_cache
				     *block_group);
u64 btrfs_find_space_for_alloc(struct btrfs_block_group_cache *block_group,
			       u64 offset, u64 bytes, u64 empty_size,
			       u64 *max_extent_size);
u64 btrfs_find_ino_for_alloc(struct btrfs_root *fs_root);
void btrfs_dump_free_space(struct btrfs_block_group_cache *block_group,
			   u64 bytes);
int btrfs_find_space_cluster(struct btrfs_block_group_cache *block_group,
			     struct btrfs_free_cluster *cluster,
			     u64 offset, u64 bytes, u64 empty_size);
void btrfs_init_free_cluster(struct btrfs_free_cluster *cluster);
u64 btrfs_alloc_from_cluster(struct btrfs_block_group_cache *block_group,
			     struct btrfs_free_cluster *cluster, u64 bytes,
			     u64 min_start, u64 *max_extent_size);
int btrfs_return_cluster_to_free_space(
			       struct btrfs_block_group_cache *block_group,
			       struct btrfs_free_cluster *cluster);
int btrfs_trim_block_group(struct btrfs_block_group_cache *block_group,
			   u64 *trimmed, u64 start, u64 end, u64 minlen);

/* Support functions for running our sanity tests */
#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
int test_add_free_space_entry(struct btrfs_block_group_cache *cache,
			      u64 offset, u64 bytes, bool bitmap);
int test_check_exists(struct btrfs_block_group_cache *cache,
		      u64 offset, u64 bytes);
#endif

#endif
