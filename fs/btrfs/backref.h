/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2011 STRATO.  All rights reserved.
 */

#ifndef BTRFS_BACKREF_H
#define BTRFS_BACKREF_H

#include <linux/btrfs.h>
#include "ulist.h"
#include "extent_io.h"

struct inode_fs_paths {
	struct btrfs_path		*btrfs_path;
	struct btrfs_root		*fs_root;
	struct btrfs_data_container	*fspath;
};

typedef int (iterate_extent_inodes_t)(u64 inum, u64 offset, u64 root,
		void *ctx);

int extent_from_logical(struct btrfs_fs_info *fs_info, u64 logical,
			struct btrfs_path *path, struct btrfs_key *found_key,
			u64 *flags);

int tree_backref_for_extent(unsigned long *ptr, struct extent_buffer *eb,
			    struct btrfs_key *key, struct btrfs_extent_item *ei,
			    u32 item_size, u64 *out_root, u8 *out_level);

int iterate_extent_inodes(struct btrfs_fs_info *fs_info,
				u64 extent_item_objectid,
				u64 extent_offset, int search_commit_root,
				iterate_extent_inodes_t *iterate, void *ctx,
				bool ignore_offset);

int iterate_inodes_from_logical(u64 logical, struct btrfs_fs_info *fs_info,
				struct btrfs_path *path,
				iterate_extent_inodes_t *iterate, void *ctx,
				bool ignore_offset);

int paths_from_inode(u64 inum, struct inode_fs_paths *ipath);

int btrfs_find_all_leafs(struct btrfs_trans_handle *trans,
			 struct btrfs_fs_info *fs_info, u64 bytenr,
			 u64 time_seq, struct ulist **leafs,
			 const u64 *extent_item_pos, bool ignore_offset);
int btrfs_find_all_roots(struct btrfs_trans_handle *trans,
			 struct btrfs_fs_info *fs_info, u64 bytenr,
			 u64 time_seq, struct ulist **roots, bool ignore_offset);
char *btrfs_ref_to_path(struct btrfs_root *fs_root, struct btrfs_path *path,
			u32 name_len, unsigned long name_off,
			struct extent_buffer *eb_in, u64 parent,
			char *dest, u32 size);

struct btrfs_data_container *init_data_container(u32 total_bytes);
struct inode_fs_paths *init_ipath(s32 total_bytes, struct btrfs_root *fs_root,
					struct btrfs_path *path);
void free_ipath(struct inode_fs_paths *ipath);

int btrfs_find_one_extref(struct btrfs_root *root, u64 inode_objectid,
			  u64 start_off, struct btrfs_path *path,
			  struct btrfs_inode_extref **ret_extref,
			  u64 *found_off);
int btrfs_check_shared(struct btrfs_root *root, u64 inum, u64 bytenr,
		struct ulist *roots, struct ulist *tmp_ulist);

int __init btrfs_prelim_ref_init(void);
void __cold btrfs_prelim_ref_exit(void);

struct prelim_ref {
	struct rb_node rbnode;
	u64 root_id;
	struct btrfs_key key_for_search;
	int level;
	int count;
	struct extent_inode_elem *inode_list;
	u64 parent;
	u64 wanted_disk_byte;
};

/*
 * Iterate backrefs of one extent.
 *
 * Now it only supports iteration of tree block in commit root.
 */
struct btrfs_backref_iter {
	u64 bytenr;
	struct btrfs_path *path;
	struct btrfs_fs_info *fs_info;
	struct btrfs_key cur_key;
	u32 item_ptr;
	u32 cur_ptr;
	u32 end_ptr;
};

struct btrfs_backref_iter *btrfs_backref_iter_alloc(
		struct btrfs_fs_info *fs_info, gfp_t gfp_flag);

static inline void btrfs_backref_iter_free(struct btrfs_backref_iter *iter)
{
	if (!iter)
		return;
	btrfs_free_path(iter->path);
	kfree(iter);
}

static inline struct extent_buffer *btrfs_backref_get_eb(
		struct btrfs_backref_iter *iter)
{
	if (!iter)
		return NULL;
	return iter->path->nodes[0];
}

/*
 * For metadata with EXTENT_ITEM key (non-skinny) case, the first inline data
 * is btrfs_tree_block_info, without a btrfs_extent_inline_ref header.
 *
 * This helper determines if that's the case.
 */
static inline bool btrfs_backref_has_tree_block_info(
		struct btrfs_backref_iter *iter)
{
	if (iter->cur_key.type == BTRFS_EXTENT_ITEM_KEY &&
	    iter->cur_ptr - iter->item_ptr == sizeof(struct btrfs_extent_item))
		return true;
	return false;
}

int btrfs_backref_iter_start(struct btrfs_backref_iter *iter, u64 bytenr);

int btrfs_backref_iter_next(struct btrfs_backref_iter *iter);

static inline bool btrfs_backref_iter_is_inline_ref(
		struct btrfs_backref_iter *iter)
{
	if (iter->cur_key.type == BTRFS_EXTENT_ITEM_KEY ||
	    iter->cur_key.type == BTRFS_METADATA_ITEM_KEY)
		return true;
	return false;
}

static inline void btrfs_backref_iter_release(struct btrfs_backref_iter *iter)
{
	iter->bytenr = 0;
	iter->item_ptr = 0;
	iter->cur_ptr = 0;
	iter->end_ptr = 0;
	btrfs_release_path(iter->path);
	memset(&iter->cur_key, 0, sizeof(iter->cur_key));
}

#endif
