/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2009 Oracle.  All rights reserved.
 */

#ifndef BTRFS_FREE_SPACE_CACHE_H
#define BTRFS_FREE_SPACE_CACHE_H

/*
 * This is the trim state of an extent or bitmap.
 *
 * BTRFS_TRIM_STATE_TRIMMING is special and used to maintain the state of a
 * bitmap as we may need several trims to fully trim a single bitmap entry.
 * This is reset should any free space other than trimmed space be added to the
 * bitmap.
 */
enum btrfs_trim_state {
	BTRFS_TRIM_STATE_UNTRIMMED,
	BTRFS_TRIM_STATE_TRIMMED,
	BTRFS_TRIM_STATE_TRIMMING,
};

struct btrfs_free_space {
	struct rb_node offset_index;
	struct rb_node bytes_index;
	u64 offset;
	u64 bytes;
	u64 max_extent_size;
	unsigned long *bitmap;
	struct list_head list;
	enum btrfs_trim_state trim_state;
	s32 bitmap_extents;
};

static inline bool btrfs_free_space_trimmed(struct btrfs_free_space *info)
{
	return (info->trim_state == BTRFS_TRIM_STATE_TRIMMED);
}

static inline bool btrfs_free_space_trimming_bitmap(
					    struct btrfs_free_space *info)
{
	return (info->trim_state == BTRFS_TRIM_STATE_TRIMMING);
}

struct btrfs_free_space_ctl {
	spinlock_t tree_lock;
	struct rb_root free_space_offset;
	struct rb_root_cached free_space_bytes;
	u64 free_space;
	int extents_thresh;
	int free_extents;
	int total_bitmaps;
	int unit;
	u64 start;
	s32 discardable_extents[BTRFS_STAT_NR_ENTRIES];
	s64 discardable_bytes[BTRFS_STAT_NR_ENTRIES];
	const struct btrfs_free_space_op *op;
	void *private;
	struct mutex cache_writeout_mutex;
	struct list_head trimming_ranges;
};

struct btrfs_free_space_op {
	bool (*use_bitmap)(struct btrfs_free_space_ctl *ctl,
			   struct btrfs_free_space *info);
};

struct btrfs_io_ctl {
	void *cur, *orig;
	struct page *page;
	struct page **pages;
	struct btrfs_fs_info *fs_info;
	struct inode *inode;
	unsigned long size;
	int index;
	int num_pages;
	int entries;
	int bitmaps;
};

struct inode *lookup_free_space_inode(struct btrfs_block_group *block_group,
		struct btrfs_path *path);
int create_free_space_inode(struct btrfs_trans_handle *trans,
			    struct btrfs_block_group *block_group,
			    struct btrfs_path *path);
int btrfs_remove_free_space_inode(struct btrfs_trans_handle *trans,
				  struct inode *inode,
				  struct btrfs_block_group *block_group);

int btrfs_check_trunc_cache_free_space(struct btrfs_fs_info *fs_info,
				       struct btrfs_block_rsv *rsv);
int btrfs_truncate_free_space_cache(struct btrfs_trans_handle *trans,
				    struct btrfs_block_group *block_group,
				    struct inode *inode);
int load_free_space_cache(struct btrfs_block_group *block_group);
int btrfs_wait_cache_io(struct btrfs_trans_handle *trans,
			struct btrfs_block_group *block_group,
			struct btrfs_path *path);
int btrfs_write_out_cache(struct btrfs_trans_handle *trans,
			  struct btrfs_block_group *block_group,
			  struct btrfs_path *path);

void btrfs_init_free_space_ctl(struct btrfs_block_group *block_group,
			       struct btrfs_free_space_ctl *ctl);
int __btrfs_add_free_space(struct btrfs_fs_info *fs_info,
			   struct btrfs_free_space_ctl *ctl,
			   u64 bytenr, u64 size,
			   enum btrfs_trim_state trim_state);
int btrfs_add_free_space(struct btrfs_block_group *block_group,
			 u64 bytenr, u64 size);
int btrfs_add_free_space_unused(struct btrfs_block_group *block_group,
				u64 bytenr, u64 size);
int btrfs_add_free_space_async_trimmed(struct btrfs_block_group *block_group,
				       u64 bytenr, u64 size);
int btrfs_remove_free_space(struct btrfs_block_group *block_group,
			    u64 bytenr, u64 size);
void __btrfs_remove_free_space_cache(struct btrfs_free_space_ctl *ctl);
void btrfs_remove_free_space_cache(struct btrfs_block_group *block_group);
bool btrfs_is_free_space_trimmed(struct btrfs_block_group *block_group);
u64 btrfs_find_space_for_alloc(struct btrfs_block_group *block_group,
			       u64 offset, u64 bytes, u64 empty_size,
			       u64 *max_extent_size);
void btrfs_dump_free_space(struct btrfs_block_group *block_group,
			   u64 bytes);
int btrfs_find_space_cluster(struct btrfs_block_group *block_group,
			     struct btrfs_free_cluster *cluster,
			     u64 offset, u64 bytes, u64 empty_size);
void btrfs_init_free_cluster(struct btrfs_free_cluster *cluster);
u64 btrfs_alloc_from_cluster(struct btrfs_block_group *block_group,
			     struct btrfs_free_cluster *cluster, u64 bytes,
			     u64 min_start, u64 *max_extent_size);
void btrfs_return_cluster_to_free_space(
			       struct btrfs_block_group *block_group,
			       struct btrfs_free_cluster *cluster);
int btrfs_trim_block_group(struct btrfs_block_group *block_group,
			   u64 *trimmed, u64 start, u64 end, u64 minlen);
int btrfs_trim_block_group_extents(struct btrfs_block_group *block_group,
				   u64 *trimmed, u64 start, u64 end, u64 minlen,
				   bool async);
int btrfs_trim_block_group_bitmaps(struct btrfs_block_group *block_group,
				   u64 *trimmed, u64 start, u64 end, u64 minlen,
				   u64 maxlen, bool async);

bool btrfs_free_space_cache_v1_active(struct btrfs_fs_info *fs_info);
int btrfs_set_free_space_cache_v1_active(struct btrfs_fs_info *fs_info, bool active);
/* Support functions for running our sanity tests */
#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
int test_add_free_space_entry(struct btrfs_block_group *cache,
			      u64 offset, u64 bytes, bool bitmap);
int test_check_exists(struct btrfs_block_group *cache, u64 offset, u64 bytes);
#endif

#endif
