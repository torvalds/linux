/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#ifndef BTRFS_DISK_IO_H
#define BTRFS_DISK_IO_H

#define BTRFS_SUPER_MIRROR_MAX	 3
#define BTRFS_SUPER_MIRROR_SHIFT 12

/*
 * Fixed blocksize for all devices, applies to specific ways of reading
 * metadata like superblock. Must meet the set_blocksize requirements.
 *
 * Do not change.
 */
#define BTRFS_BDEV_BLOCKSIZE	(4096)

enum btrfs_wq_endio_type {
	BTRFS_WQ_ENDIO_DATA,
	BTRFS_WQ_ENDIO_METADATA,
	BTRFS_WQ_ENDIO_FREE_SPACE,
	BTRFS_WQ_ENDIO_RAID56,
};

static inline u64 btrfs_sb_offset(int mirror)
{
	u64 start = SZ_16K;
	if (mirror)
		return start << (BTRFS_SUPER_MIRROR_SHIFT * mirror);
	return BTRFS_SUPER_INFO_OFFSET;
}

struct btrfs_device;
struct btrfs_fs_devices;

void btrfs_check_leaked_roots(struct btrfs_fs_info *fs_info);
void btrfs_init_fs_info(struct btrfs_fs_info *fs_info);
int btrfs_verify_level_key(struct extent_buffer *eb, int level,
			   struct btrfs_key *first_key, u64 parent_transid);
struct extent_buffer *read_tree_block(struct btrfs_fs_info *fs_info, u64 bytenr,
				      u64 owner_root, u64 parent_transid,
				      int level, struct btrfs_key *first_key);
struct extent_buffer *btrfs_find_create_tree_block(
						struct btrfs_fs_info *fs_info,
						u64 bytenr, u64 owner_root,
						int level);
void btrfs_clean_tree_block(struct extent_buffer *buf);
void btrfs_clear_oneshot_options(struct btrfs_fs_info *fs_info);
int btrfs_start_pre_rw_mount(struct btrfs_fs_info *fs_info);
int __cold open_ctree(struct super_block *sb,
	       struct btrfs_fs_devices *fs_devices,
	       char *options);
void __cold close_ctree(struct btrfs_fs_info *fs_info);
int write_all_supers(struct btrfs_fs_info *fs_info, int max_mirrors);
struct btrfs_super_block *btrfs_read_dev_super(struct block_device *bdev);
struct btrfs_super_block *btrfs_read_dev_one_super(struct block_device *bdev,
						   int copy_num);
int btrfs_commit_super(struct btrfs_fs_info *fs_info);
struct btrfs_root *btrfs_read_tree_root(struct btrfs_root *tree_root,
					struct btrfs_key *key);
int btrfs_insert_fs_root(struct btrfs_fs_info *fs_info,
			 struct btrfs_root *root);
void btrfs_free_fs_roots(struct btrfs_fs_info *fs_info);

struct btrfs_root *btrfs_get_fs_root(struct btrfs_fs_info *fs_info,
				     u64 objectid, bool check_ref);
struct btrfs_root *btrfs_get_new_fs_root(struct btrfs_fs_info *fs_info,
					 u64 objectid, dev_t anon_dev);
struct btrfs_root *btrfs_get_fs_root_commit_root(struct btrfs_fs_info *fs_info,
						 struct btrfs_path *path,
						 u64 objectid);
int btrfs_global_root_insert(struct btrfs_root *root);
void btrfs_global_root_delete(struct btrfs_root *root);
struct btrfs_root *btrfs_global_root(struct btrfs_fs_info *fs_info,
				     struct btrfs_key *key);
struct btrfs_root *btrfs_csum_root(struct btrfs_fs_info *fs_info, u64 bytenr);
struct btrfs_root *btrfs_extent_root(struct btrfs_fs_info *fs_info, u64 bytenr);

void btrfs_free_fs_info(struct btrfs_fs_info *fs_info);
int btrfs_cleanup_fs_roots(struct btrfs_fs_info *fs_info);
void btrfs_btree_balance_dirty(struct btrfs_fs_info *fs_info);
void btrfs_btree_balance_dirty_nodelay(struct btrfs_fs_info *fs_info);
void btrfs_drop_and_free_fs_root(struct btrfs_fs_info *fs_info,
				 struct btrfs_root *root);
int btrfs_validate_metadata_buffer(struct btrfs_bio *bbio,
				   struct page *page, u64 start, u64 end,
				   int mirror);
void btrfs_submit_metadata_bio(struct inode *inode, struct bio *bio, int mirror_num);
#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
struct btrfs_root *btrfs_alloc_dummy_root(struct btrfs_fs_info *fs_info);
#endif

/*
 * This function is used to grab the root, and avoid it is freed when we
 * access it. But it doesn't ensure that the tree is not dropped.
 *
 * If you want to ensure the whole tree is safe, you should use
 * 	fs_info->subvol_srcu
 */
static inline struct btrfs_root *btrfs_grab_root(struct btrfs_root *root)
{
	if (!root)
		return NULL;
	if (refcount_inc_not_zero(&root->refs))
		return root;
	return NULL;
}

static inline struct btrfs_root *btrfs_block_group_root(struct btrfs_fs_info *fs_info)
{
	if (btrfs_fs_incompat(fs_info, EXTENT_TREE_V2))
		return fs_info->block_group_root;
	return btrfs_extent_root(fs_info, 0);
}

void btrfs_put_root(struct btrfs_root *root);
void btrfs_mark_buffer_dirty(struct extent_buffer *buf);
int btrfs_buffer_uptodate(struct extent_buffer *buf, u64 parent_transid,
			  int atomic);
int btrfs_read_extent_buffer(struct extent_buffer *buf, u64 parent_transid,
			     int level, struct btrfs_key *first_key);
blk_status_t btrfs_bio_wq_end_io(struct btrfs_fs_info *info, struct bio *bio,
			enum btrfs_wq_endio_type metadata);
blk_status_t btrfs_wq_submit_bio(struct inode *inode, struct bio *bio,
				 int mirror_num, u64 dio_file_offset,
				 extent_submit_bio_start_t *submit_bio_start);
blk_status_t btrfs_submit_bio_done(void *private_data, struct bio *bio,
			  int mirror_num);
int btrfs_alloc_log_tree_node(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root);
int btrfs_init_log_root_tree(struct btrfs_trans_handle *trans,
			     struct btrfs_fs_info *fs_info);
int btrfs_add_log_tree(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root);
void btrfs_cleanup_dirty_bgs(struct btrfs_transaction *trans,
			     struct btrfs_fs_info *fs_info);
void btrfs_cleanup_one_transaction(struct btrfs_transaction *trans,
				  struct btrfs_fs_info *fs_info);
struct btrfs_root *btrfs_create_tree(struct btrfs_trans_handle *trans,
				     u64 objectid);
int btree_lock_page_hook(struct page *page, void *data,
				void (*flush_fn)(void *));
int btrfs_get_num_tolerated_disk_barrier_failures(u64 flags);
int btrfs_get_free_objectid(struct btrfs_root *root, u64 *objectid);
int btrfs_init_root_free_objectid(struct btrfs_root *root);
int __init btrfs_end_io_wq_init(void);
void __cold btrfs_end_io_wq_exit(void);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
void btrfs_set_buffer_lockdep_class(u64 objectid,
			            struct extent_buffer *eb, int level);
#else
static inline void btrfs_set_buffer_lockdep_class(u64 objectid,
					struct extent_buffer *eb, int level)
{
}
#endif

#endif
