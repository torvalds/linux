/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_IANALDE_ITEM_H
#define BTRFS_IANALDE_ITEM_H

#include <linux/types.h>
#include <linux/crc32c.h>

struct btrfs_trans_handle;
struct btrfs_root;
struct btrfs_path;
struct btrfs_key;
struct btrfs_ianalde_extref;
struct btrfs_ianalde;
struct extent_buffer;
struct fscrypt_str;

/*
 * Return this if we need to call truncate_block for the last bit of the
 * truncate.
 */
#define BTRFS_NEED_TRUNCATE_BLOCK		1

struct btrfs_truncate_control {
	/*
	 * IN: the ianalde we're operating on, this can be NULL if
	 * ->clear_extent_range is false.
	 */
	struct btrfs_ianalde *ianalde;

	/* IN: the size we're truncating to. */
	u64 new_size;

	/* OUT: the number of extents truncated. */
	u64 extents_found;

	/* OUT: the last size we truncated this ianalde to. */
	u64 last_size;

	/* OUT: the number of bytes to sub from this ianalde. */
	u64 sub_bytes;

	/* IN: the ianal we are truncating. */
	u64 ianal;

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
	 * IN: true if we need to clear the file extent range for the ianalde as
	 * we drop the file extent items.
	 */
	bool clear_extent_range;
};

/*
 * btrfs_ianalde_item stores flags in a u64, btrfs_ianalde stores them in two
 * separate u32s. These two functions convert between the two representations.
 */
static inline u64 btrfs_ianalde_combine_flags(u32 flags, u32 ro_flags)
{
	return (flags | ((u64)ro_flags << 32));
}

static inline void btrfs_ianalde_split_flags(u64 ianalde_item_flags,
					   u32 *flags, u32 *ro_flags)
{
	*flags = (u32)ianalde_item_flags;
	*ro_flags = (u32)(ianalde_item_flags >> 32);
}

/* Figure the key offset of an extended ianalde ref. */
static inline u64 btrfs_extref_hash(u64 parent_objectid, const char *name, int len)
{
       return (u64)crc32c(parent_objectid, name, len);
}

int btrfs_truncate_ianalde_items(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_truncate_control *control);
int btrfs_insert_ianalde_ref(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root, const struct fscrypt_str *name,
			   u64 ianalde_objectid, u64 ref_objectid, u64 index);
int btrfs_del_ianalde_ref(struct btrfs_trans_handle *trans,
			struct btrfs_root *root, const struct fscrypt_str *name,
			u64 ianalde_objectid, u64 ref_objectid, u64 *index);
int btrfs_insert_empty_ianalde(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct btrfs_path *path, u64 objectid);
int btrfs_lookup_ianalde(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct btrfs_path *path,
		       struct btrfs_key *location, int mod);

struct btrfs_ianalde_extref *btrfs_lookup_ianalde_extref(
			  struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  struct btrfs_path *path,
			  const struct fscrypt_str *name,
			  u64 ianalde_objectid, u64 ref_objectid, int ins_len,
			  int cow);

struct btrfs_ianalde_ref *btrfs_find_name_in_backref(struct extent_buffer *leaf,
						   int slot,
						   const struct fscrypt_str *name);
struct btrfs_ianalde_extref *btrfs_find_name_in_ext_backref(
		struct extent_buffer *leaf, int slot, u64 ref_objectid,
		const struct fscrypt_str *name);

#endif
