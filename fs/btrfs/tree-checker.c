// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Qu Wenruo 2017.  All rights reserved.
 */

/*
 * The module is used to catch unexpected/corrupted tree block data.
 * Such behavior can be caused either by a fuzzed image or bugs.
 *
 * The objective is to do leaf/node validation checks when tree block is read
 * from disk, and check *every* possible member, so other code won't
 * need to checking them again.
 *
 * Due to the potential and unwanted damage, every checker needs to be
 * carefully reviewed otherwise so it does not prevent mount of valid images.
 */

#include "ctree.h"
#include "tree-checker.h"
#include "disk-io.h"
#include "compression.h"
#include "volumes.h"

/*
 * Error message should follow the following format:
 * corrupt <type>: <identifier>, <reason>[, <bad_value>]
 *
 * @type:	leaf or node
 * @identifier:	the necessary info to locate the leaf/node.
 * 		It's recommened to decode key.objecitd/offset if it's
 * 		meaningful.
 * @reason:	describe the error
 * @bad_value:	optional, it's recommened to output bad value and its
 *		expected value (range).
 *
 * Since comma is used to separate the components, only space is allowed
 * inside each component.
 */

/*
 * Append generic "corrupt leaf/node root=%llu block=%llu slot=%d: " to @fmt.
 * Allows callers to customize the output.
 */
__printf(4, 5)
__cold
static void generic_err(const struct btrfs_fs_info *fs_info,
			const struct extent_buffer *eb, int slot,
			const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	btrfs_crit(fs_info,
		"corrupt %s: root=%llu block=%llu slot=%d, %pV",
		btrfs_header_level(eb) == 0 ? "leaf" : "node",
		btrfs_header_owner(eb), btrfs_header_bytenr(eb), slot, &vaf);
	va_end(args);
}

/*
 * Customized reporter for extent data item, since its key objectid and
 * offset has its own meaning.
 */
__printf(4, 5)
__cold
static void file_extent_err(const struct btrfs_fs_info *fs_info,
			    const struct extent_buffer *eb, int slot,
			    const char *fmt, ...)
{
	struct btrfs_key key;
	struct va_format vaf;
	va_list args;

	btrfs_item_key_to_cpu(eb, &key, slot);
	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	btrfs_crit(fs_info,
	"corrupt %s: root=%llu block=%llu slot=%d ino=%llu file_offset=%llu, %pV",
		btrfs_header_level(eb) == 0 ? "leaf" : "node",
		btrfs_header_owner(eb), btrfs_header_bytenr(eb), slot,
		key.objectid, key.offset, &vaf);
	va_end(args);
}

/*
 * Return 0 if the btrfs_file_extent_##name is aligned to @alignment
 * Else return 1
 */
#define CHECK_FE_ALIGNED(fs_info, leaf, slot, fi, name, alignment)	      \
({									      \
	if (!IS_ALIGNED(btrfs_file_extent_##name((leaf), (fi)), (alignment))) \
		file_extent_err((fs_info), (leaf), (slot),		      \
	"invalid %s for file extent, have %llu, should be aligned to %u",     \
			(#name), btrfs_file_extent_##name((leaf), (fi)),      \
			(alignment));					      \
	(!IS_ALIGNED(btrfs_file_extent_##name((leaf), (fi)), (alignment)));   \
})

static int check_extent_data_item(struct btrfs_fs_info *fs_info,
				  struct extent_buffer *leaf,
				  struct btrfs_key *key, int slot)
{
	struct btrfs_file_extent_item *fi;
	u32 sectorsize = fs_info->sectorsize;
	u32 item_size = btrfs_item_size_nr(leaf, slot);

	if (!IS_ALIGNED(key->offset, sectorsize)) {
		file_extent_err(fs_info, leaf, slot,
"unaligned file_offset for file extent, have %llu should be aligned to %u",
			key->offset, sectorsize);
		return -EUCLEAN;
	}

	fi = btrfs_item_ptr(leaf, slot, struct btrfs_file_extent_item);

	if (btrfs_file_extent_type(leaf, fi) > BTRFS_FILE_EXTENT_TYPES) {
		file_extent_err(fs_info, leaf, slot,
		"invalid type for file extent, have %u expect range [0, %u]",
			btrfs_file_extent_type(leaf, fi),
			BTRFS_FILE_EXTENT_TYPES);
		return -EUCLEAN;
	}

	/*
	 * Support for new compression/encrption must introduce incompat flag,
	 * and must be caught in open_ctree().
	 */
	if (btrfs_file_extent_compression(leaf, fi) > BTRFS_COMPRESS_TYPES) {
		file_extent_err(fs_info, leaf, slot,
	"invalid compression for file extent, have %u expect range [0, %u]",
			btrfs_file_extent_compression(leaf, fi),
			BTRFS_COMPRESS_TYPES);
		return -EUCLEAN;
	}
	if (btrfs_file_extent_encryption(leaf, fi)) {
		file_extent_err(fs_info, leaf, slot,
			"invalid encryption for file extent, have %u expect 0",
			btrfs_file_extent_encryption(leaf, fi));
		return -EUCLEAN;
	}
	if (btrfs_file_extent_type(leaf, fi) == BTRFS_FILE_EXTENT_INLINE) {
		/* Inline extent must have 0 as key offset */
		if (key->offset) {
			file_extent_err(fs_info, leaf, slot,
		"invalid file_offset for inline file extent, have %llu expect 0",
				key->offset);
			return -EUCLEAN;
		}

		/* Compressed inline extent has no on-disk size, skip it */
		if (btrfs_file_extent_compression(leaf, fi) !=
		    BTRFS_COMPRESS_NONE)
			return 0;

		/* Uncompressed inline extent size must match item size */
		if (item_size != BTRFS_FILE_EXTENT_INLINE_DATA_START +
		    btrfs_file_extent_ram_bytes(leaf, fi)) {
			file_extent_err(fs_info, leaf, slot,
	"invalid ram_bytes for uncompressed inline extent, have %u expect %llu",
				item_size, BTRFS_FILE_EXTENT_INLINE_DATA_START +
				btrfs_file_extent_ram_bytes(leaf, fi));
			return -EUCLEAN;
		}
		return 0;
	}

	/* Regular or preallocated extent has fixed item size */
	if (item_size != sizeof(*fi)) {
		file_extent_err(fs_info, leaf, slot,
	"invalid item size for reg/prealloc file extent, have %u expect %zu",
			item_size, sizeof(*fi));
		return -EUCLEAN;
	}
	if (CHECK_FE_ALIGNED(fs_info, leaf, slot, fi, ram_bytes, sectorsize) ||
	    CHECK_FE_ALIGNED(fs_info, leaf, slot, fi, disk_bytenr, sectorsize) ||
	    CHECK_FE_ALIGNED(fs_info, leaf, slot, fi, disk_num_bytes, sectorsize) ||
	    CHECK_FE_ALIGNED(fs_info, leaf, slot, fi, offset, sectorsize) ||
	    CHECK_FE_ALIGNED(fs_info, leaf, slot, fi, num_bytes, sectorsize))
		return -EUCLEAN;
	return 0;
}

static int check_csum_item(struct btrfs_fs_info *fs_info,
			   struct extent_buffer *leaf, struct btrfs_key *key,
			   int slot)
{
	u32 sectorsize = fs_info->sectorsize;
	u32 csumsize = btrfs_super_csum_size(fs_info->super_copy);

	if (key->objectid != BTRFS_EXTENT_CSUM_OBJECTID) {
		generic_err(fs_info, leaf, slot,
		"invalid key objectid for csum item, have %llu expect %llu",
			key->objectid, BTRFS_EXTENT_CSUM_OBJECTID);
		return -EUCLEAN;
	}
	if (!IS_ALIGNED(key->offset, sectorsize)) {
		generic_err(fs_info, leaf, slot,
	"unaligned key offset for csum item, have %llu should be aligned to %u",
			key->offset, sectorsize);
		return -EUCLEAN;
	}
	if (!IS_ALIGNED(btrfs_item_size_nr(leaf, slot), csumsize)) {
		generic_err(fs_info, leaf, slot,
	"unaligned item size for csum item, have %u should be aligned to %u",
			btrfs_item_size_nr(leaf, slot), csumsize);
		return -EUCLEAN;
	}
	return 0;
}

/*
 * Customized reported for dir_item, only important new info is key->objectid,
 * which represents inode number
 */
__printf(4, 5)
__cold
static void dir_item_err(const struct btrfs_fs_info *fs_info,
			 const struct extent_buffer *eb, int slot,
			 const char *fmt, ...)
{
	struct btrfs_key key;
	struct va_format vaf;
	va_list args;

	btrfs_item_key_to_cpu(eb, &key, slot);
	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	btrfs_crit(fs_info,
	"corrupt %s: root=%llu block=%llu slot=%d ino=%llu, %pV",
		btrfs_header_level(eb) == 0 ? "leaf" : "node",
		btrfs_header_owner(eb), btrfs_header_bytenr(eb), slot,
		key.objectid, &vaf);
	va_end(args);
}

static int check_dir_item(struct btrfs_fs_info *fs_info,
			  struct extent_buffer *leaf,
			  struct btrfs_key *key, int slot)
{
	struct btrfs_dir_item *di;
	u32 item_size = btrfs_item_size_nr(leaf, slot);
	u32 cur = 0;

	di = btrfs_item_ptr(leaf, slot, struct btrfs_dir_item);
	while (cur < item_size) {
		u32 name_len;
		u32 data_len;
		u32 max_name_len;
		u32 total_size;
		u32 name_hash;
		u8 dir_type;

		/* header itself should not cross item boundary */
		if (cur + sizeof(*di) > item_size) {
			dir_item_err(fs_info, leaf, slot,
		"dir item header crosses item boundary, have %zu boundary %u",
				cur + sizeof(*di), item_size);
			return -EUCLEAN;
		}

		/* dir type check */
		dir_type = btrfs_dir_type(leaf, di);
		if (dir_type >= BTRFS_FT_MAX) {
			dir_item_err(fs_info, leaf, slot,
			"invalid dir item type, have %u expect [0, %u)",
				dir_type, BTRFS_FT_MAX);
			return -EUCLEAN;
		}

		if (key->type == BTRFS_XATTR_ITEM_KEY &&
		    dir_type != BTRFS_FT_XATTR) {
			dir_item_err(fs_info, leaf, slot,
		"invalid dir item type for XATTR key, have %u expect %u",
				dir_type, BTRFS_FT_XATTR);
			return -EUCLEAN;
		}
		if (dir_type == BTRFS_FT_XATTR &&
		    key->type != BTRFS_XATTR_ITEM_KEY) {
			dir_item_err(fs_info, leaf, slot,
			"xattr dir type found for non-XATTR key");
			return -EUCLEAN;
		}
		if (dir_type == BTRFS_FT_XATTR)
			max_name_len = XATTR_NAME_MAX;
		else
			max_name_len = BTRFS_NAME_LEN;

		/* Name/data length check */
		name_len = btrfs_dir_name_len(leaf, di);
		data_len = btrfs_dir_data_len(leaf, di);
		if (name_len > max_name_len) {
			dir_item_err(fs_info, leaf, slot,
			"dir item name len too long, have %u max %u",
				name_len, max_name_len);
			return -EUCLEAN;
		}
		if (name_len + data_len > BTRFS_MAX_XATTR_SIZE(fs_info)) {
			dir_item_err(fs_info, leaf, slot,
			"dir item name and data len too long, have %u max %u",
				name_len + data_len,
				BTRFS_MAX_XATTR_SIZE(fs_info));
			return -EUCLEAN;
		}

		if (data_len && dir_type != BTRFS_FT_XATTR) {
			dir_item_err(fs_info, leaf, slot,
			"dir item with invalid data len, have %u expect 0",
				data_len);
			return -EUCLEAN;
		}

		total_size = sizeof(*di) + name_len + data_len;

		/* header and name/data should not cross item boundary */
		if (cur + total_size > item_size) {
			dir_item_err(fs_info, leaf, slot,
		"dir item data crosses item boundary, have %u boundary %u",
				cur + total_size, item_size);
			return -EUCLEAN;
		}

		/*
		 * Special check for XATTR/DIR_ITEM, as key->offset is name
		 * hash, should match its name
		 */
		if (key->type == BTRFS_DIR_ITEM_KEY ||
		    key->type == BTRFS_XATTR_ITEM_KEY) {
			char namebuf[max(BTRFS_NAME_LEN, XATTR_NAME_MAX)];

			read_extent_buffer(leaf, namebuf,
					(unsigned long)(di + 1), name_len);
			name_hash = btrfs_name_hash(namebuf, name_len);
			if (key->offset != name_hash) {
				dir_item_err(fs_info, leaf, slot,
		"name hash mismatch with key, have 0x%016x expect 0x%016llx",
					name_hash, key->offset);
				return -EUCLEAN;
			}
		}
		cur += total_size;
		di = (struct btrfs_dir_item *)((void *)di + total_size);
	}
	return 0;
}

__printf(4, 5)
__cold
static void block_group_err(const struct btrfs_fs_info *fs_info,
			    const struct extent_buffer *eb, int slot,
			    const char *fmt, ...)
{
	struct btrfs_key key;
	struct va_format vaf;
	va_list args;

	btrfs_item_key_to_cpu(eb, &key, slot);
	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	btrfs_crit(fs_info,
	"corrupt %s: root=%llu block=%llu slot=%d bg_start=%llu bg_len=%llu, %pV",
		btrfs_header_level(eb) == 0 ? "leaf" : "node",
		btrfs_header_owner(eb), btrfs_header_bytenr(eb), slot,
		key.objectid, key.offset, &vaf);
	va_end(args);
}

static int check_block_group_item(struct btrfs_fs_info *fs_info,
				  struct extent_buffer *leaf,
				  struct btrfs_key *key, int slot)
{
	struct btrfs_block_group_item bgi;
	u32 item_size = btrfs_item_size_nr(leaf, slot);
	u64 flags;
	u64 type;

	/*
	 * Here we don't really care about alignment since extent allocator can
	 * handle it.  We care more about the size, as if one block group is
	 * larger than maximum size, it's must be some obvious corruption.
	 */
	if (key->offset > BTRFS_MAX_DATA_CHUNK_SIZE || key->offset == 0) {
		block_group_err(fs_info, leaf, slot,
			"invalid block group size, have %llu expect (0, %llu]",
				key->offset, BTRFS_MAX_DATA_CHUNK_SIZE);
		return -EUCLEAN;
	}

	if (item_size != sizeof(bgi)) {
		block_group_err(fs_info, leaf, slot,
			"invalid item size, have %u expect %zu",
				item_size, sizeof(bgi));
		return -EUCLEAN;
	}

	read_extent_buffer(leaf, &bgi, btrfs_item_ptr_offset(leaf, slot),
			   sizeof(bgi));
	if (btrfs_block_group_chunk_objectid(&bgi) !=
	    BTRFS_FIRST_CHUNK_TREE_OBJECTID) {
		block_group_err(fs_info, leaf, slot,
		"invalid block group chunk objectid, have %llu expect %llu",
				btrfs_block_group_chunk_objectid(&bgi),
				BTRFS_FIRST_CHUNK_TREE_OBJECTID);
		return -EUCLEAN;
	}

	if (btrfs_block_group_used(&bgi) > key->offset) {
		block_group_err(fs_info, leaf, slot,
			"invalid block group used, have %llu expect [0, %llu)",
				btrfs_block_group_used(&bgi), key->offset);
		return -EUCLEAN;
	}

	flags = btrfs_block_group_flags(&bgi);
	if (hweight64(flags & BTRFS_BLOCK_GROUP_PROFILE_MASK) > 1) {
		block_group_err(fs_info, leaf, slot,
"invalid profile flags, have 0x%llx (%lu bits set) expect no more than 1 bit set",
			flags & BTRFS_BLOCK_GROUP_PROFILE_MASK,
			hweight64(flags & BTRFS_BLOCK_GROUP_PROFILE_MASK));
		return -EUCLEAN;
	}

	type = flags & BTRFS_BLOCK_GROUP_TYPE_MASK;
	if (type != BTRFS_BLOCK_GROUP_DATA &&
	    type != BTRFS_BLOCK_GROUP_METADATA &&
	    type != BTRFS_BLOCK_GROUP_SYSTEM &&
	    type != (BTRFS_BLOCK_GROUP_METADATA |
			   BTRFS_BLOCK_GROUP_DATA)) {
		block_group_err(fs_info, leaf, slot,
"invalid type, have 0x%llx (%lu bits set) expect either 0x%llx, 0x%llx, 0x%llu or 0x%llx",
			type, hweight64(type),
			BTRFS_BLOCK_GROUP_DATA, BTRFS_BLOCK_GROUP_METADATA,
			BTRFS_BLOCK_GROUP_SYSTEM,
			BTRFS_BLOCK_GROUP_METADATA | BTRFS_BLOCK_GROUP_DATA);
		return -EUCLEAN;
	}
	return 0;
}

/*
 * Common point to switch the item-specific validation.
 */
static int check_leaf_item(struct btrfs_fs_info *fs_info,
			   struct extent_buffer *leaf,
			   struct btrfs_key *key, int slot)
{
	int ret = 0;

	switch (key->type) {
	case BTRFS_EXTENT_DATA_KEY:
		ret = check_extent_data_item(fs_info, leaf, key, slot);
		break;
	case BTRFS_EXTENT_CSUM_KEY:
		ret = check_csum_item(fs_info, leaf, key, slot);
		break;
	case BTRFS_DIR_ITEM_KEY:
	case BTRFS_DIR_INDEX_KEY:
	case BTRFS_XATTR_ITEM_KEY:
		ret = check_dir_item(fs_info, leaf, key, slot);
		break;
	case BTRFS_BLOCK_GROUP_ITEM_KEY:
		ret = check_block_group_item(fs_info, leaf, key, slot);
		break;
	}
	return ret;
}

static int check_leaf(struct btrfs_fs_info *fs_info, struct extent_buffer *leaf,
		      bool check_item_data)
{
	/* No valid key type is 0, so all key should be larger than this key */
	struct btrfs_key prev_key = {0, 0, 0};
	struct btrfs_key key;
	u32 nritems = btrfs_header_nritems(leaf);
	int slot;

	/*
	 * Extent buffers from a relocation tree have a owner field that
	 * corresponds to the subvolume tree they are based on. So just from an
	 * extent buffer alone we can not find out what is the id of the
	 * corresponding subvolume tree, so we can not figure out if the extent
	 * buffer corresponds to the root of the relocation tree or not. So
	 * skip this check for relocation trees.
	 */
	if (nritems == 0 && !btrfs_header_flag(leaf, BTRFS_HEADER_FLAG_RELOC)) {
		u64 owner = btrfs_header_owner(leaf);
		struct btrfs_root *check_root;

		/* These trees must never be empty */
		if (owner == BTRFS_ROOT_TREE_OBJECTID ||
		    owner == BTRFS_CHUNK_TREE_OBJECTID ||
		    owner == BTRFS_EXTENT_TREE_OBJECTID ||
		    owner == BTRFS_DEV_TREE_OBJECTID ||
		    owner == BTRFS_FS_TREE_OBJECTID ||
		    owner == BTRFS_DATA_RELOC_TREE_OBJECTID) {
			generic_err(fs_info, leaf, 0,
			"invalid root, root %llu must never be empty",
				    owner);
			return -EUCLEAN;
		}
		key.objectid = owner;
		key.type = BTRFS_ROOT_ITEM_KEY;
		key.offset = (u64)-1;

		check_root = btrfs_get_fs_root(fs_info, &key, false);
		/*
		 * The only reason we also check NULL here is that during
		 * open_ctree() some roots has not yet been set up.
		 */
		if (!IS_ERR_OR_NULL(check_root)) {
			struct extent_buffer *eb;

			eb = btrfs_root_node(check_root);
			/* if leaf is the root, then it's fine */
			if (leaf != eb) {
				generic_err(fs_info, leaf, 0,
		"invalid nritems, have %u should not be 0 for non-root leaf",
					nritems);
				free_extent_buffer(eb);
				return -EUCLEAN;
			}
			free_extent_buffer(eb);
		}
		return 0;
	}

	if (nritems == 0)
		return 0;

	/*
	 * Check the following things to make sure this is a good leaf, and
	 * leaf users won't need to bother with similar sanity checks:
	 *
	 * 1) key ordering
	 * 2) item offset and size
	 *    No overlap, no hole, all inside the leaf.
	 * 3) item content
	 *    If possible, do comprehensive sanity check.
	 *    NOTE: All checks must only rely on the item data itself.
	 */
	for (slot = 0; slot < nritems; slot++) {
		u32 item_end_expected;
		int ret;

		btrfs_item_key_to_cpu(leaf, &key, slot);

		/* Make sure the keys are in the right order */
		if (btrfs_comp_cpu_keys(&prev_key, &key) >= 0) {
			generic_err(fs_info, leaf, slot,
	"bad key order, prev (%llu %u %llu) current (%llu %u %llu)",
				prev_key.objectid, prev_key.type,
				prev_key.offset, key.objectid, key.type,
				key.offset);
			return -EUCLEAN;
		}

		/*
		 * Make sure the offset and ends are right, remember that the
		 * item data starts at the end of the leaf and grows towards the
		 * front.
		 */
		if (slot == 0)
			item_end_expected = BTRFS_LEAF_DATA_SIZE(fs_info);
		else
			item_end_expected = btrfs_item_offset_nr(leaf,
								 slot - 1);
		if (btrfs_item_end_nr(leaf, slot) != item_end_expected) {
			generic_err(fs_info, leaf, slot,
				"unexpected item end, have %u expect %u",
				btrfs_item_end_nr(leaf, slot),
				item_end_expected);
			return -EUCLEAN;
		}

		/*
		 * Check to make sure that we don't point outside of the leaf,
		 * just in case all the items are consistent to each other, but
		 * all point outside of the leaf.
		 */
		if (btrfs_item_end_nr(leaf, slot) >
		    BTRFS_LEAF_DATA_SIZE(fs_info)) {
			generic_err(fs_info, leaf, slot,
			"slot end outside of leaf, have %u expect range [0, %u]",
				btrfs_item_end_nr(leaf, slot),
				BTRFS_LEAF_DATA_SIZE(fs_info));
			return -EUCLEAN;
		}

		/* Also check if the item pointer overlaps with btrfs item. */
		if (btrfs_item_nr_offset(slot) + sizeof(struct btrfs_item) >
		    btrfs_item_ptr_offset(leaf, slot)) {
			generic_err(fs_info, leaf, slot,
		"slot overlaps with its data, item end %lu data start %lu",
				btrfs_item_nr_offset(slot) +
				sizeof(struct btrfs_item),
				btrfs_item_ptr_offset(leaf, slot));
			return -EUCLEAN;
		}

		if (check_item_data) {
			/*
			 * Check if the item size and content meet other
			 * criteria
			 */
			ret = check_leaf_item(fs_info, leaf, &key, slot);
			if (ret < 0)
				return ret;
		}

		prev_key.objectid = key.objectid;
		prev_key.type = key.type;
		prev_key.offset = key.offset;
	}

	return 0;
}

int btrfs_check_leaf_full(struct btrfs_fs_info *fs_info,
			  struct extent_buffer *leaf)
{
	return check_leaf(fs_info, leaf, true);
}

int btrfs_check_leaf_relaxed(struct btrfs_fs_info *fs_info,
			     struct extent_buffer *leaf)
{
	return check_leaf(fs_info, leaf, false);
}

int btrfs_check_node(struct btrfs_fs_info *fs_info, struct extent_buffer *node)
{
	unsigned long nr = btrfs_header_nritems(node);
	struct btrfs_key key, next_key;
	int slot;
	u64 bytenr;
	int ret = 0;

	if (nr == 0 || nr > BTRFS_NODEPTRS_PER_BLOCK(fs_info)) {
		btrfs_crit(fs_info,
"corrupt node: root=%llu block=%llu, nritems too %s, have %lu expect range [1,%u]",
			   btrfs_header_owner(node), node->start,
			   nr == 0 ? "small" : "large", nr,
			   BTRFS_NODEPTRS_PER_BLOCK(fs_info));
		return -EUCLEAN;
	}

	for (slot = 0; slot < nr - 1; slot++) {
		bytenr = btrfs_node_blockptr(node, slot);
		btrfs_node_key_to_cpu(node, &key, slot);
		btrfs_node_key_to_cpu(node, &next_key, slot + 1);

		if (!bytenr) {
			generic_err(fs_info, node, slot,
				"invalid NULL node pointer");
			ret = -EUCLEAN;
			goto out;
		}
		if (!IS_ALIGNED(bytenr, fs_info->sectorsize)) {
			generic_err(fs_info, node, slot,
			"unaligned pointer, have %llu should be aligned to %u",
				bytenr, fs_info->sectorsize);
			ret = -EUCLEAN;
			goto out;
		}

		if (btrfs_comp_cpu_keys(&key, &next_key) >= 0) {
			generic_err(fs_info, node, slot,
	"bad key order, current (%llu %u %llu) next (%llu %u %llu)",
				key.objectid, key.type, key.offset,
				next_key.objectid, next_key.type,
				next_key.offset);
			ret = -EUCLEAN;
			goto out;
		}
	}
out:
	return ret;
}
