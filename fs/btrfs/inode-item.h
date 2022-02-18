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

struct btrfs_truncate_control {
	/*
	 * IN: the inode we're operating on, this can be NULL if
	 * ->clear_extent_range is false.
	 */
	struct btrfs_inode *inode;

	/* IN: the size we're truncating to. */
	u64 new_size;

	/* OUT: the number of extents truncated. */
	u64 extents_found;

	/* OUT: the last size we truncated this inode to. */
	u64 last_size;

	/* OUT: the number of bytes to sub from this inode. */
	u64 sub_bytes;

	/* IN: the ino we are truncating. */
	u64 ino;

	/*
	 * IN: minimum key type to remove.  All key types with this type are
	 * removed only if their offset >= new_size.
	 */
	u32 min_type;

	/*
	 * IN: true if we don't want to do extent reference updates for any file
	 * extents we drop.
	 */
	bool skip_ref_updates;

	/*
	 * IN: true if we need to clear the file extent range for the inode as
	 * we drop the file extent items.
	 */
	bool clear_extent_range;
};

int btrfs_truncate_inode_items(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_truncate_control *control);
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
