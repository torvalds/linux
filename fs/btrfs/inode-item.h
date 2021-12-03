/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_INODE_ITEM_H
#define BTRFS_INODE_ITEM_H

#include <linux/types.h>

struct btrfs_trans_handle;
struct btrfs_root;
struct btrfs_path;
struct btrfs_key;
struct btrfs_inode_extref;
struct btrfs_inode;
struct extent_buffer;

/*
 * Return this if we need to call truncate_block for the last bit of the
 * truncate.
 */
#define BTRFS_NEED_TRUNCATE_BLOCK		1

int btrfs_truncate_inode_items(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_inode *inode, u64 new_size,
			       u32 min_type, u64 *extents_found);
int btrfs_insert_inode_ref(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   const char *name, int name_len,
			   u64 inode_objectid, u64 ref_objectid, u64 index);
int btrfs_del_inode_ref(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   const char *name, int name_len,
			   u64 inode_objectid, u64 ref_objectid, u64 *index);
int btrfs_insert_empty_inode(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct btrfs_path *path, u64 objectid);
int btrfs_lookup_inode(struct btrfs_trans_handle *trans, struct btrfs_root
		       *root, struct btrfs_path *path,
		       struct btrfs_key *location, int mod);

struct btrfs_inode_extref *btrfs_lookup_inode_extref(
			  struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  struct btrfs_path *path,
			  const char *name, int name_len,
			  u64 inode_objectid, u64 ref_objectid, int ins_len,
			  int cow);

struct btrfs_inode_ref *btrfs_find_name_in_backref(struct extent_buffer *leaf,
						   int slot, const char *name,
						   int name_len);
struct btrfs_inode_extref *btrfs_find_name_in_ext_backref(
		struct extent_buffer *leaf, int slot, u64 ref_objectid,
		const char *name, int name_len);

#endif
