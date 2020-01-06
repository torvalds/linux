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

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/error-injection.h>
#include "ctree.h"
#include "tree-checker.h"
#include "disk-io.h"
#include "compression.h"
#include "volumes.h"
#include "misc.h"

/*
 * Error message should follow the following format:
 * corrupt <type>: <identifier>, <reason>[, <bad_value>]
 *
 * @type:	leaf or node
 * @identifier:	the necessary info to locate the leaf/node.
 * 		It's recommended to decode key.objecitd/offset if it's
 * 		meaningful.
 * @reason:	describe the error
 * @bad_value:	optional, it's recommended to output bad value and its
 *		expected value (range).
 *
 * Since comma is used to separate the components, only space is allowed
 * inside each component.
 */

/*
 * Append generic "corrupt leaf/node root=%llu block=%llu slot=%d: " to @fmt.
 * Allows callers to customize the output.
 */
__printf(3, 4)
__cold
static void generic_err(const struct extent_buffer *eb, int slot,
			const char *fmt, ...)
{
	const struct btrfs_fs_info *fs_info = eb->fs_info;
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
__printf(3, 4)
__cold
static void file_extent_err(const struct extent_buffer *eb, int slot,
			    const char *fmt, ...)
{
	const struct btrfs_fs_info *fs_info = eb->fs_info;
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
#define CHECK_FE_ALIGNED(leaf, slot, fi, name, alignment)		      \
({									      \
	if (!IS_ALIGNED(btrfs_file_extent_##name((leaf), (fi)), (alignment))) \
		file_extent_err((leaf), (slot),				      \
	"invalid %s for file extent, have %llu, should be aligned to %u",     \
			(#name), btrfs_file_extent_##name((leaf), (fi)),      \
			(alignment));					      \
	(!IS_ALIGNED(btrfs_file_extent_##name((leaf), (fi)), (alignment)));   \
})

static u64 file_extent_end(struct extent_buffer *leaf,
			   struct btrfs_key *key,
			   struct btrfs_file_extent_item *extent)
{
	u64 end;
	u64 len;

	if (btrfs_file_extent_type(leaf, extent) == BTRFS_FILE_EXTENT_INLINE) {
		len = btrfs_file_extent_ram_bytes(leaf, extent);
		end = ALIGN(key->offset + len, leaf->fs_info->sectorsize);
	} else {
		len = btrfs_file_extent_num_bytes(leaf, extent);
		end = key->offset + len;
	}
	return end;
}

/*
 * Customized report for dir_item, the only new important information is
 * key->objectid, which represents inode number
 */
__printf(3, 4)
__cold
static void dir_item_err(const struct extent_buffer *eb, int slot,
			 const char *fmt, ...)
{
	const struct btrfs_fs_info *fs_info = eb->fs_info;
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

/*
 * This functions checks prev_key->objectid, to ensure current key and prev_key
 * share the same objectid as inode number.
 *
 * This is to detect missing INODE_ITEM in subvolume trees.
 *
 * Return true if everything is OK or we don't need to check.
 * Return false if anything is wrong.
 */
static bool check_prev_ino(struct extent_buffer *leaf,
			   struct btrfs_key *key, int slot,
			   struct btrfs_key *prev_key)
{
	/* No prev key, skip check */
	if (slot == 0)
		return true;

	/* Only these key->types needs to be checked */
	ASSERT(key->type == BTRFS_XATTR_ITEM_KEY ||
	       key->type == BTRFS_INODE_REF_KEY ||
	       key->type == BTRFS_DIR_INDEX_KEY ||
	       key->type == BTRFS_DIR_ITEM_KEY ||
	       key->type == BTRFS_EXTENT_DATA_KEY);

	/*
	 * Only subvolume trees along with their reloc trees need this check.
	 * Things like log tree doesn't follow this ino requirement.
	 */
	if (!is_fstree(btrfs_header_owner(leaf)))
		return true;

	if (key->objectid == prev_key->objectid)
		return true;

	/* Error found */
	dir_item_err(leaf, slot,
		"invalid previous key objectid, have %llu expect %llu",
		prev_key->objectid, key->objectid);
	return false;
}
static int check_extent_data_item(struct extent_buffer *leaf,
				  struct btrfs_key *key, int slot,
				  struct btrfs_key *prev_key)
{
	struct btrfs_fs_info *fs_info = leaf->fs_info;
	struct btrfs_file_extent_item *fi;
	u32 sectorsize = fs_info->sectorsize;
	u32 item_size = btrfs_item_size_nr(leaf, slot);
	u64 extent_end;

	if (!IS_ALIGNED(key->offset, sectorsize)) {
		file_extent_err(leaf, slot,
"unaligned file_offset for file extent, have %llu should be aligned to %u",
			key->offset, sectorsize);
		return -EUCLEAN;
	}

	/*
	 * Previous key must have the same key->objectid (ino).
	 * It can be XATTR_ITEM, INODE_ITEM or just another EXTENT_DATA.
	 * But if objectids mismatch, it means we have a missing
	 * INODE_ITEM.
	 */
	if (!check_prev_ino(leaf, key, slot, prev_key))
		return -EUCLEAN;

	fi = btrfs_item_ptr(leaf, slot, struct btrfs_file_extent_item);

	/*
	 * Make sure the item contains at least inline header, so the file
	 * extent type is not some garbage.
	 */
	if (item_size < BTRFS_FILE_EXTENT_INLINE_DATA_START) {
		file_extent_err(leaf, slot,
				"invalid item size, have %u expect [%lu, %u)",
				item_size, BTRFS_FILE_EXTENT_INLINE_DATA_START,
				SZ_4K);
		return -EUCLEAN;
	}
	if (btrfs_file_extent_type(leaf, fi) >= BTRFS_NR_FILE_EXTENT_TYPES) {
		file_extent_err(leaf, slot,
		"invalid type for file extent, have %u expect range [0, %u]",
			btrfs_file_extent_type(leaf, fi),
			BTRFS_NR_FILE_EXTENT_TYPES - 1);
		return -EUCLEAN;
	}

	/*
	 * Support for new compression/encryption must introduce incompat flag,
	 * and must be caught in open_ctree().
	 */
	if (btrfs_file_extent_compression(leaf, fi) >= BTRFS_NR_COMPRESS_TYPES) {
		file_extent_err(leaf, slot,
	"invalid compression for file extent, have %u expect range [0, %u]",
			btrfs_file_extent_compression(leaf, fi),
			BTRFS_NR_COMPRESS_TYPES - 1);
		return -EUCLEAN;
	}
	if (btrfs_file_extent_encryption(leaf, fi)) {
		file_extent_err(leaf, slot,
			"invalid encryption for file extent, have %u expect 0",
			btrfs_file_extent_encryption(leaf, fi));
		return -EUCLEAN;
	}
	if (btrfs_file_extent_type(leaf, fi) == BTRFS_FILE_EXTENT_INLINE) {
		/* Inline extent must have 0 as key offset */
		if (key->offset) {
			file_extent_err(leaf, slot,
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
			file_extent_err(leaf, slot,
	"invalid ram_bytes for uncompressed inline extent, have %u expect %llu",
				item_size, BTRFS_FILE_EXTENT_INLINE_DATA_START +
				btrfs_file_extent_ram_bytes(leaf, fi));
			return -EUCLEAN;
		}
		return 0;
	}

	/* Regular or preallocated extent has fixed item size */
	if (item_size != sizeof(*fi)) {
		file_extent_err(leaf, slot,
	"invalid item size for reg/prealloc file extent, have %u expect %zu",
			item_size, sizeof(*fi));
		return -EUCLEAN;
	}
	if (CHECK_FE_ALIGNED(leaf, slot, fi, ram_bytes, sectorsize) ||
	    CHECK_FE_ALIGNED(leaf, slot, fi, disk_bytenr, sectorsize) ||
	    CHECK_FE_ALIGNED(leaf, slot, fi, disk_num_bytes, sectorsize) ||
	    CHECK_FE_ALIGNED(leaf, slot, fi, offset, sectorsize) ||
	    CHECK_FE_ALIGNED(leaf, slot, fi, num_bytes, sectorsize))
		return -EUCLEAN;

	/* Catch extent end overflow */
	if (check_add_overflow(btrfs_file_extent_num_bytes(leaf, fi),
			       key->offset, &extent_end)) {
		file_extent_err(leaf, slot,
	"extent end overflow, have file offset %llu extent num bytes %llu",
				key->offset,
				btrfs_file_extent_num_bytes(leaf, fi));
		return -EUCLEAN;
	}

	/*
	 * Check that no two consecutive file extent items, in the same leaf,
	 * present ranges that overlap each other.
	 */
	if (slot > 0 &&
	    prev_key->objectid == key->objectid &&
	    prev_key->type == BTRFS_EXTENT_DATA_KEY) {
		struct btrfs_file_extent_item *prev_fi;
		u64 prev_end;

		prev_fi = btrfs_item_ptr(leaf, slot - 1,
					 struct btrfs_file_extent_item);
		prev_end = file_extent_end(leaf, prev_key, prev_fi);
		if (prev_end > key->offset) {
			file_extent_err(leaf, slot - 1,
"file extent end range (%llu) goes beyond start offset (%llu) of the next file extent",
					prev_end, key->offset);
			return -EUCLEAN;
		}
	}

	return 0;
}

static int check_csum_item(struct extent_buffer *leaf, struct btrfs_key *key,
			   int slot)
{
	struct btrfs_fs_info *fs_info = leaf->fs_info;
	u32 sectorsize = fs_info->sectorsize;
	u32 csumsize = btrfs_super_csum_size(fs_info->super_copy);

	if (key->objectid != BTRFS_EXTENT_CSUM_OBJECTID) {
		generic_err(leaf, slot,
		"invalid key objectid for csum item, have %llu expect %llu",
			key->objectid, BTRFS_EXTENT_CSUM_OBJECTID);
		return -EUCLEAN;
	}
	if (!IS_ALIGNED(key->offset, sectorsize)) {
		generic_err(leaf, slot,
	"unaligned key offset for csum item, have %llu should be aligned to %u",
			key->offset, sectorsize);
		return -EUCLEAN;
	}
	if (!IS_ALIGNED(btrfs_item_size_nr(leaf, slot), csumsize)) {
		generic_err(leaf, slot,
	"unaligned item size for csum item, have %u should be aligned to %u",
			btrfs_item_size_nr(leaf, slot), csumsize);
		return -EUCLEAN;
	}
	return 0;
}

static int check_dir_item(struct extent_buffer *leaf,
			  struct btrfs_key *key, struct btrfs_key *prev_key,
			  int slot)
{
	struct btrfs_fs_info *fs_info = leaf->fs_info;
	struct btrfs_dir_item *di;
	u32 item_size = btrfs_item_size_nr(leaf, slot);
	u32 cur = 0;

	if (!check_prev_ino(leaf, key, slot, prev_key))
		return -EUCLEAN;
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
			dir_item_err(leaf, slot,
		"dir item header crosses item boundary, have %zu boundary %u",
				cur + sizeof(*di), item_size);
			return -EUCLEAN;
		}

		/* dir type check */
		dir_type = btrfs_dir_type(leaf, di);
		if (dir_type >= BTRFS_FT_MAX) {
			dir_item_err(leaf, slot,
			"invalid dir item type, have %u expect [0, %u)",
				dir_type, BTRFS_FT_MAX);
			return -EUCLEAN;
		}

		if (key->type == BTRFS_XATTR_ITEM_KEY &&
		    dir_type != BTRFS_FT_XATTR) {
			dir_item_err(leaf, slot,
		"invalid dir item type for XATTR key, have %u expect %u",
				dir_type, BTRFS_FT_XATTR);
			return -EUCLEAN;
		}
		if (dir_type == BTRFS_FT_XATTR &&
		    key->type != BTRFS_XATTR_ITEM_KEY) {
			dir_item_err(leaf, slot,
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
			dir_item_err(leaf, slot,
			"dir item name len too long, have %u max %u",
				name_len, max_name_len);
			return -EUCLEAN;
		}
		if (name_len + data_len > BTRFS_MAX_XATTR_SIZE(fs_info)) {
			dir_item_err(leaf, slot,
			"dir item name and data len too long, have %u max %u",
				name_len + data_len,
				BTRFS_MAX_XATTR_SIZE(fs_info));
			return -EUCLEAN;
		}

		if (data_len && dir_type != BTRFS_FT_XATTR) {
			dir_item_err(leaf, slot,
			"dir item with invalid data len, have %u expect 0",
				data_len);
			return -EUCLEAN;
		}

		total_size = sizeof(*di) + name_len + data_len;

		/* header and name/data should not cross item boundary */
		if (cur + total_size > item_size) {
			dir_item_err(leaf, slot,
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
				dir_item_err(leaf, slot,
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

__printf(3, 4)
__cold
static void block_group_err(const struct extent_buffer *eb, int slot,
			    const char *fmt, ...)
{
	const struct btrfs_fs_info *fs_info = eb->fs_info;
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

static int check_block_group_item(struct extent_buffer *leaf,
				  struct btrfs_key *key, int slot)
{
	struct btrfs_block_group_item bgi;
	u32 item_size = btrfs_item_size_nr(leaf, slot);
	u64 flags;
	u64 type;

	/*
	 * Here we don't really care about alignment since extent allocator can
	 * handle it.  We care more about the size.
	 */
	if (key->offset == 0) {
		block_group_err(leaf, slot,
				"invalid block group size 0");
		return -EUCLEAN;
	}

	if (item_size != sizeof(bgi)) {
		block_group_err(leaf, slot,
			"invalid item size, have %u expect %zu",
				item_size, sizeof(bgi));
		return -EUCLEAN;
	}

	read_extent_buffer(leaf, &bgi, btrfs_item_ptr_offset(leaf, slot),
			   sizeof(bgi));
	if (btrfs_stack_block_group_chunk_objectid(&bgi) !=
	    BTRFS_FIRST_CHUNK_TREE_OBJECTID) {
		block_group_err(leaf, slot,
		"invalid block group chunk objectid, have %llu expect %llu",
				btrfs_stack_block_group_chunk_objectid(&bgi),
				BTRFS_FIRST_CHUNK_TREE_OBJECTID);
		return -EUCLEAN;
	}

	if (btrfs_stack_block_group_used(&bgi) > key->offset) {
		block_group_err(leaf, slot,
			"invalid block group used, have %llu expect [0, %llu)",
				btrfs_stack_block_group_used(&bgi), key->offset);
		return -EUCLEAN;
	}

	flags = btrfs_stack_block_group_flags(&bgi);
	if (hweight64(flags & BTRFS_BLOCK_GROUP_PROFILE_MASK) > 1) {
		block_group_err(leaf, slot,
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
		block_group_err(leaf, slot,
"invalid type, have 0x%llx (%lu bits set) expect either 0x%llx, 0x%llx, 0x%llx or 0x%llx",
			type, hweight64(type),
			BTRFS_BLOCK_GROUP_DATA, BTRFS_BLOCK_GROUP_METADATA,
			BTRFS_BLOCK_GROUP_SYSTEM,
			BTRFS_BLOCK_GROUP_METADATA | BTRFS_BLOCK_GROUP_DATA);
		return -EUCLEAN;
	}
	return 0;
}

__printf(4, 5)
__cold
static void chunk_err(const struct extent_buffer *leaf,
		      const struct btrfs_chunk *chunk, u64 logical,
		      const char *fmt, ...)
{
	const struct btrfs_fs_info *fs_info = leaf->fs_info;
	bool is_sb;
	struct va_format vaf;
	va_list args;
	int i;
	int slot = -1;

	/* Only superblock eb is able to have such small offset */
	is_sb = (leaf->start == BTRFS_SUPER_INFO_OFFSET);

	if (!is_sb) {
		/*
		 * Get the slot number by iterating through all slots, this
		 * would provide better readability.
		 */
		for (i = 0; i < btrfs_header_nritems(leaf); i++) {
			if (btrfs_item_ptr_offset(leaf, i) ==
					(unsigned long)chunk) {
				slot = i;
				break;
			}
		}
	}
	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	if (is_sb)
		btrfs_crit(fs_info,
		"corrupt superblock syschunk array: chunk_start=%llu, %pV",
			   logical, &vaf);
	else
		btrfs_crit(fs_info,
	"corrupt leaf: root=%llu block=%llu slot=%d chunk_start=%llu, %pV",
			   BTRFS_CHUNK_TREE_OBJECTID, leaf->start, slot,
			   logical, &vaf);
	va_end(args);
}

/*
 * The common chunk check which could also work on super block sys chunk array.
 *
 * Return -EUCLEAN if anything is corrupted.
 * Return 0 if everything is OK.
 */
int btrfs_check_chunk_valid(struct extent_buffer *leaf,
			    struct btrfs_chunk *chunk, u64 logical)
{
	struct btrfs_fs_info *fs_info = leaf->fs_info;
	u64 length;
	u64 stripe_len;
	u16 num_stripes;
	u16 sub_stripes;
	u64 type;
	u64 features;
	bool mixed = false;

	length = btrfs_chunk_length(leaf, chunk);
	stripe_len = btrfs_chunk_stripe_len(leaf, chunk);
	num_stripes = btrfs_chunk_num_stripes(leaf, chunk);
	sub_stripes = btrfs_chunk_sub_stripes(leaf, chunk);
	type = btrfs_chunk_type(leaf, chunk);

	if (!num_stripes) {
		chunk_err(leaf, chunk, logical,
			  "invalid chunk num_stripes, have %u", num_stripes);
		return -EUCLEAN;
	}
	if (!IS_ALIGNED(logical, fs_info->sectorsize)) {
		chunk_err(leaf, chunk, logical,
		"invalid chunk logical, have %llu should aligned to %u",
			  logical, fs_info->sectorsize);
		return -EUCLEAN;
	}
	if (btrfs_chunk_sector_size(leaf, chunk) != fs_info->sectorsize) {
		chunk_err(leaf, chunk, logical,
			  "invalid chunk sectorsize, have %u expect %u",
			  btrfs_chunk_sector_size(leaf, chunk),
			  fs_info->sectorsize);
		return -EUCLEAN;
	}
	if (!length || !IS_ALIGNED(length, fs_info->sectorsize)) {
		chunk_err(leaf, chunk, logical,
			  "invalid chunk length, have %llu", length);
		return -EUCLEAN;
	}
	if (!is_power_of_2(stripe_len) || stripe_len != BTRFS_STRIPE_LEN) {
		chunk_err(leaf, chunk, logical,
			  "invalid chunk stripe length: %llu",
			  stripe_len);
		return -EUCLEAN;
	}
	if (~(BTRFS_BLOCK_GROUP_TYPE_MASK | BTRFS_BLOCK_GROUP_PROFILE_MASK) &
	    type) {
		chunk_err(leaf, chunk, logical,
			  "unrecognized chunk type: 0x%llx",
			  ~(BTRFS_BLOCK_GROUP_TYPE_MASK |
			    BTRFS_BLOCK_GROUP_PROFILE_MASK) &
			  btrfs_chunk_type(leaf, chunk));
		return -EUCLEAN;
	}

	if (!has_single_bit_set(type & BTRFS_BLOCK_GROUP_PROFILE_MASK) &&
	    (type & BTRFS_BLOCK_GROUP_PROFILE_MASK) != 0) {
		chunk_err(leaf, chunk, logical,
		"invalid chunk profile flag: 0x%llx, expect 0 or 1 bit set",
			  type & BTRFS_BLOCK_GROUP_PROFILE_MASK);
		return -EUCLEAN;
	}
	if ((type & BTRFS_BLOCK_GROUP_TYPE_MASK) == 0) {
		chunk_err(leaf, chunk, logical,
	"missing chunk type flag, have 0x%llx one bit must be set in 0x%llx",
			  type, BTRFS_BLOCK_GROUP_TYPE_MASK);
		return -EUCLEAN;
	}

	if ((type & BTRFS_BLOCK_GROUP_SYSTEM) &&
	    (type & (BTRFS_BLOCK_GROUP_METADATA | BTRFS_BLOCK_GROUP_DATA))) {
		chunk_err(leaf, chunk, logical,
			  "system chunk with data or metadata type: 0x%llx",
			  type);
		return -EUCLEAN;
	}

	features = btrfs_super_incompat_flags(fs_info->super_copy);
	if (features & BTRFS_FEATURE_INCOMPAT_MIXED_GROUPS)
		mixed = true;

	if (!mixed) {
		if ((type & BTRFS_BLOCK_GROUP_METADATA) &&
		    (type & BTRFS_BLOCK_GROUP_DATA)) {
			chunk_err(leaf, chunk, logical,
			"mixed chunk type in non-mixed mode: 0x%llx", type);
			return -EUCLEAN;
		}
	}

	if ((type & BTRFS_BLOCK_GROUP_RAID10 && sub_stripes != 2) ||
	    (type & BTRFS_BLOCK_GROUP_RAID1 && num_stripes != 2) ||
	    (type & BTRFS_BLOCK_GROUP_RAID5 && num_stripes < 2) ||
	    (type & BTRFS_BLOCK_GROUP_RAID6 && num_stripes < 3) ||
	    (type & BTRFS_BLOCK_GROUP_DUP && num_stripes != 2) ||
	    ((type & BTRFS_BLOCK_GROUP_PROFILE_MASK) == 0 && num_stripes != 1)) {
		chunk_err(leaf, chunk, logical,
			"invalid num_stripes:sub_stripes %u:%u for profile %llu",
			num_stripes, sub_stripes,
			type & BTRFS_BLOCK_GROUP_PROFILE_MASK);
		return -EUCLEAN;
	}

	return 0;
}

__printf(3, 4)
__cold
static void dev_item_err(const struct extent_buffer *eb, int slot,
			 const char *fmt, ...)
{
	struct btrfs_key key;
	struct va_format vaf;
	va_list args;

	btrfs_item_key_to_cpu(eb, &key, slot);
	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	btrfs_crit(eb->fs_info,
	"corrupt %s: root=%llu block=%llu slot=%d devid=%llu %pV",
		btrfs_header_level(eb) == 0 ? "leaf" : "node",
		btrfs_header_owner(eb), btrfs_header_bytenr(eb), slot,
		key.objectid, &vaf);
	va_end(args);
}

static int check_dev_item(struct extent_buffer *leaf,
			  struct btrfs_key *key, int slot)
{
	struct btrfs_dev_item *ditem;

	if (key->objectid != BTRFS_DEV_ITEMS_OBJECTID) {
		dev_item_err(leaf, slot,
			     "invalid objectid: has=%llu expect=%llu",
			     key->objectid, BTRFS_DEV_ITEMS_OBJECTID);
		return -EUCLEAN;
	}
	ditem = btrfs_item_ptr(leaf, slot, struct btrfs_dev_item);
	if (btrfs_device_id(leaf, ditem) != key->offset) {
		dev_item_err(leaf, slot,
			     "devid mismatch: key has=%llu item has=%llu",
			     key->offset, btrfs_device_id(leaf, ditem));
		return -EUCLEAN;
	}

	/*
	 * For device total_bytes, we don't have reliable way to check it, as
	 * it can be 0 for device removal. Device size check can only be done
	 * by dev extents check.
	 */
	if (btrfs_device_bytes_used(leaf, ditem) >
	    btrfs_device_total_bytes(leaf, ditem)) {
		dev_item_err(leaf, slot,
			     "invalid bytes used: have %llu expect [0, %llu]",
			     btrfs_device_bytes_used(leaf, ditem),
			     btrfs_device_total_bytes(leaf, ditem));
		return -EUCLEAN;
	}
	/*
	 * Remaining members like io_align/type/gen/dev_group aren't really
	 * utilized.  Skip them to make later usage of them easier.
	 */
	return 0;
}

/* Inode item error output has the same format as dir_item_err() */
#define inode_item_err(fs_info, eb, slot, fmt, ...)			\
	dir_item_err(eb, slot, fmt, __VA_ARGS__)

static int check_inode_item(struct extent_buffer *leaf,
			    struct btrfs_key *key, int slot)
{
	struct btrfs_fs_info *fs_info = leaf->fs_info;
	struct btrfs_inode_item *iitem;
	u64 super_gen = btrfs_super_generation(fs_info->super_copy);
	u32 valid_mask = (S_IFMT | S_ISUID | S_ISGID | S_ISVTX | 0777);
	u32 mode;

	if ((key->objectid < BTRFS_FIRST_FREE_OBJECTID ||
	     key->objectid > BTRFS_LAST_FREE_OBJECTID) &&
	    key->objectid != BTRFS_ROOT_TREE_DIR_OBJECTID &&
	    key->objectid != BTRFS_FREE_INO_OBJECTID) {
		generic_err(leaf, slot,
	"invalid key objectid: has %llu expect %llu or [%llu, %llu] or %llu",
			    key->objectid, BTRFS_ROOT_TREE_DIR_OBJECTID,
			    BTRFS_FIRST_FREE_OBJECTID,
			    BTRFS_LAST_FREE_OBJECTID,
			    BTRFS_FREE_INO_OBJECTID);
		return -EUCLEAN;
	}
	if (key->offset != 0) {
		inode_item_err(fs_info, leaf, slot,
			"invalid key offset: has %llu expect 0",
			key->offset);
		return -EUCLEAN;
	}
	iitem = btrfs_item_ptr(leaf, slot, struct btrfs_inode_item);

	/* Here we use super block generation + 1 to handle log tree */
	if (btrfs_inode_generation(leaf, iitem) > super_gen + 1) {
		inode_item_err(fs_info, leaf, slot,
			"invalid inode generation: has %llu expect (0, %llu]",
			       btrfs_inode_generation(leaf, iitem),
			       super_gen + 1);
		return -EUCLEAN;
	}
	/* Note for ROOT_TREE_DIR_ITEM, mkfs could set its transid 0 */
	if (btrfs_inode_transid(leaf, iitem) > super_gen + 1) {
		inode_item_err(fs_info, leaf, slot,
			"invalid inode generation: has %llu expect [0, %llu]",
			       btrfs_inode_transid(leaf, iitem), super_gen + 1);
		return -EUCLEAN;
	}

	/*
	 * For size and nbytes it's better not to be too strict, as for dir
	 * item its size/nbytes can easily get wrong, but doesn't affect
	 * anything in the fs. So here we skip the check.
	 */
	mode = btrfs_inode_mode(leaf, iitem);
	if (mode & ~valid_mask) {
		inode_item_err(fs_info, leaf, slot,
			       "unknown mode bit detected: 0x%x",
			       mode & ~valid_mask);
		return -EUCLEAN;
	}

	/*
	 * S_IFMT is not bit mapped so we can't completely rely on
	 * is_power_of_2/has_single_bit_set, but it can save us from checking
	 * FIFO/CHR/DIR/REG.  Only needs to check BLK, LNK and SOCKS
	 */
	if (!has_single_bit_set(mode & S_IFMT)) {
		if (!S_ISLNK(mode) && !S_ISBLK(mode) && !S_ISSOCK(mode)) {
			inode_item_err(fs_info, leaf, slot,
			"invalid mode: has 0%o expect valid S_IF* bit(s)",
				       mode & S_IFMT);
			return -EUCLEAN;
		}
	}
	if (S_ISDIR(mode) && btrfs_inode_nlink(leaf, iitem) > 1) {
		inode_item_err(fs_info, leaf, slot,
		       "invalid nlink: has %u expect no more than 1 for dir",
			btrfs_inode_nlink(leaf, iitem));
		return -EUCLEAN;
	}
	if (btrfs_inode_flags(leaf, iitem) & ~BTRFS_INODE_FLAG_MASK) {
		inode_item_err(fs_info, leaf, slot,
			       "unknown flags detected: 0x%llx",
			       btrfs_inode_flags(leaf, iitem) &
			       ~BTRFS_INODE_FLAG_MASK);
		return -EUCLEAN;
	}
	return 0;
}

static int check_root_item(struct extent_buffer *leaf, struct btrfs_key *key,
			   int slot)
{
	struct btrfs_fs_info *fs_info = leaf->fs_info;
	struct btrfs_root_item ri;
	const u64 valid_root_flags = BTRFS_ROOT_SUBVOL_RDONLY |
				     BTRFS_ROOT_SUBVOL_DEAD;

	/* No such tree id */
	if (key->objectid == 0) {
		generic_err(leaf, slot, "invalid root id 0");
		return -EUCLEAN;
	}

	/*
	 * Some older kernel may create ROOT_ITEM with non-zero offset, so here
	 * we only check offset for reloc tree whose key->offset must be a
	 * valid tree.
	 */
	if (key->objectid == BTRFS_TREE_RELOC_OBJECTID && key->offset == 0) {
		generic_err(leaf, slot, "invalid root id 0 for reloc tree");
		return -EUCLEAN;
	}

	if (btrfs_item_size_nr(leaf, slot) != sizeof(ri)) {
		generic_err(leaf, slot,
			    "invalid root item size, have %u expect %zu",
			    btrfs_item_size_nr(leaf, slot), sizeof(ri));
	}

	read_extent_buffer(leaf, &ri, btrfs_item_ptr_offset(leaf, slot),
			   sizeof(ri));

	/* Generation related */
	if (btrfs_root_generation(&ri) >
	    btrfs_super_generation(fs_info->super_copy) + 1) {
		generic_err(leaf, slot,
			"invalid root generation, have %llu expect (0, %llu]",
			    btrfs_root_generation(&ri),
			    btrfs_super_generation(fs_info->super_copy) + 1);
		return -EUCLEAN;
	}
	if (btrfs_root_generation_v2(&ri) >
	    btrfs_super_generation(fs_info->super_copy) + 1) {
		generic_err(leaf, slot,
		"invalid root v2 generation, have %llu expect (0, %llu]",
			    btrfs_root_generation_v2(&ri),
			    btrfs_super_generation(fs_info->super_copy) + 1);
		return -EUCLEAN;
	}
	if (btrfs_root_last_snapshot(&ri) >
	    btrfs_super_generation(fs_info->super_copy) + 1) {
		generic_err(leaf, slot,
		"invalid root last_snapshot, have %llu expect (0, %llu]",
			    btrfs_root_last_snapshot(&ri),
			    btrfs_super_generation(fs_info->super_copy) + 1);
		return -EUCLEAN;
	}

	/* Alignment and level check */
	if (!IS_ALIGNED(btrfs_root_bytenr(&ri), fs_info->sectorsize)) {
		generic_err(leaf, slot,
		"invalid root bytenr, have %llu expect to be aligned to %u",
			    btrfs_root_bytenr(&ri), fs_info->sectorsize);
		return -EUCLEAN;
	}
	if (btrfs_root_level(&ri) >= BTRFS_MAX_LEVEL) {
		generic_err(leaf, slot,
			    "invalid root level, have %u expect [0, %u]",
			    btrfs_root_level(&ri), BTRFS_MAX_LEVEL - 1);
		return -EUCLEAN;
	}
	if (ri.drop_level >= BTRFS_MAX_LEVEL) {
		generic_err(leaf, slot,
			    "invalid root level, have %u expect [0, %u]",
			    ri.drop_level, BTRFS_MAX_LEVEL - 1);
		return -EUCLEAN;
	}

	/* Flags check */
	if (btrfs_root_flags(&ri) & ~valid_root_flags) {
		generic_err(leaf, slot,
			    "invalid root flags, have 0x%llx expect mask 0x%llx",
			    btrfs_root_flags(&ri), valid_root_flags);
		return -EUCLEAN;
	}
	return 0;
}

__printf(3,4)
__cold
static void extent_err(const struct extent_buffer *eb, int slot,
		       const char *fmt, ...)
{
	struct btrfs_key key;
	struct va_format vaf;
	va_list args;
	u64 bytenr;
	u64 len;

	btrfs_item_key_to_cpu(eb, &key, slot);
	bytenr = key.objectid;
	if (key.type == BTRFS_METADATA_ITEM_KEY ||
	    key.type == BTRFS_TREE_BLOCK_REF_KEY ||
	    key.type == BTRFS_SHARED_BLOCK_REF_KEY)
		len = eb->fs_info->nodesize;
	else
		len = key.offset;
	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	btrfs_crit(eb->fs_info,
	"corrupt %s: block=%llu slot=%d extent bytenr=%llu len=%llu %pV",
		btrfs_header_level(eb) == 0 ? "leaf" : "node",
		eb->start, slot, bytenr, len, &vaf);
	va_end(args);
}

static int check_extent_item(struct extent_buffer *leaf,
			     struct btrfs_key *key, int slot)
{
	struct btrfs_fs_info *fs_info = leaf->fs_info;
	struct btrfs_extent_item *ei;
	bool is_tree_block = false;
	unsigned long ptr;	/* Current pointer inside inline refs */
	unsigned long end;	/* Extent item end */
	const u32 item_size = btrfs_item_size_nr(leaf, slot);
	u64 flags;
	u64 generation;
	u64 total_refs;		/* Total refs in btrfs_extent_item */
	u64 inline_refs = 0;	/* found total inline refs */

	if (key->type == BTRFS_METADATA_ITEM_KEY &&
	    !btrfs_fs_incompat(fs_info, SKINNY_METADATA)) {
		generic_err(leaf, slot,
"invalid key type, METADATA_ITEM type invalid when SKINNY_METADATA feature disabled");
		return -EUCLEAN;
	}
	/* key->objectid is the bytenr for both key types */
	if (!IS_ALIGNED(key->objectid, fs_info->sectorsize)) {
		generic_err(leaf, slot,
		"invalid key objectid, have %llu expect to be aligned to %u",
			   key->objectid, fs_info->sectorsize);
		return -EUCLEAN;
	}

	/* key->offset is tree level for METADATA_ITEM_KEY */
	if (key->type == BTRFS_METADATA_ITEM_KEY &&
	    key->offset >= BTRFS_MAX_LEVEL) {
		extent_err(leaf, slot,
			   "invalid tree level, have %llu expect [0, %u]",
			   key->offset, BTRFS_MAX_LEVEL - 1);
		return -EUCLEAN;
	}

	/*
	 * EXTENT/METADATA_ITEM consists of:
	 * 1) One btrfs_extent_item
	 *    Records the total refs, type and generation of the extent.
	 *
	 * 2) One btrfs_tree_block_info (for EXTENT_ITEM and tree backref only)
	 *    Records the first key and level of the tree block.
	 *
	 * 2) Zero or more btrfs_extent_inline_ref(s)
	 *    Each inline ref has one btrfs_extent_inline_ref shows:
	 *    2.1) The ref type, one of the 4
	 *         TREE_BLOCK_REF	Tree block only
	 *         SHARED_BLOCK_REF	Tree block only
	 *         EXTENT_DATA_REF	Data only
	 *         SHARED_DATA_REF	Data only
	 *    2.2) Ref type specific data
	 *         Either using btrfs_extent_inline_ref::offset, or specific
	 *         data structure.
	 */
	if (item_size < sizeof(*ei)) {
		extent_err(leaf, slot,
			   "invalid item size, have %u expect [%zu, %u)",
			   item_size, sizeof(*ei),
			   BTRFS_LEAF_DATA_SIZE(fs_info));
		return -EUCLEAN;
	}
	end = item_size + btrfs_item_ptr_offset(leaf, slot);

	/* Checks against extent_item */
	ei = btrfs_item_ptr(leaf, slot, struct btrfs_extent_item);
	flags = btrfs_extent_flags(leaf, ei);
	total_refs = btrfs_extent_refs(leaf, ei);
	generation = btrfs_extent_generation(leaf, ei);
	if (generation > btrfs_super_generation(fs_info->super_copy) + 1) {
		extent_err(leaf, slot,
			   "invalid generation, have %llu expect (0, %llu]",
			   generation,
			   btrfs_super_generation(fs_info->super_copy) + 1);
		return -EUCLEAN;
	}
	if (!has_single_bit_set(flags & (BTRFS_EXTENT_FLAG_DATA |
					 BTRFS_EXTENT_FLAG_TREE_BLOCK))) {
		extent_err(leaf, slot,
		"invalid extent flag, have 0x%llx expect 1 bit set in 0x%llx",
			flags, BTRFS_EXTENT_FLAG_DATA |
			BTRFS_EXTENT_FLAG_TREE_BLOCK);
		return -EUCLEAN;
	}
	is_tree_block = !!(flags & BTRFS_EXTENT_FLAG_TREE_BLOCK);
	if (is_tree_block) {
		if (key->type == BTRFS_EXTENT_ITEM_KEY &&
		    key->offset != fs_info->nodesize) {
			extent_err(leaf, slot,
				   "invalid extent length, have %llu expect %u",
				   key->offset, fs_info->nodesize);
			return -EUCLEAN;
		}
	} else {
		if (key->type != BTRFS_EXTENT_ITEM_KEY) {
			extent_err(leaf, slot,
			"invalid key type, have %u expect %u for data backref",
				   key->type, BTRFS_EXTENT_ITEM_KEY);
			return -EUCLEAN;
		}
		if (!IS_ALIGNED(key->offset, fs_info->sectorsize)) {
			extent_err(leaf, slot,
			"invalid extent length, have %llu expect aligned to %u",
				   key->offset, fs_info->sectorsize);
			return -EUCLEAN;
		}
	}
	ptr = (unsigned long)(struct btrfs_extent_item *)(ei + 1);

	/* Check the special case of btrfs_tree_block_info */
	if (is_tree_block && key->type != BTRFS_METADATA_ITEM_KEY) {
		struct btrfs_tree_block_info *info;

		info = (struct btrfs_tree_block_info *)ptr;
		if (btrfs_tree_block_level(leaf, info) >= BTRFS_MAX_LEVEL) {
			extent_err(leaf, slot,
			"invalid tree block info level, have %u expect [0, %u]",
				   btrfs_tree_block_level(leaf, info),
				   BTRFS_MAX_LEVEL - 1);
			return -EUCLEAN;
		}
		ptr = (unsigned long)(struct btrfs_tree_block_info *)(info + 1);
	}

	/* Check inline refs */
	while (ptr < end) {
		struct btrfs_extent_inline_ref *iref;
		struct btrfs_extent_data_ref *dref;
		struct btrfs_shared_data_ref *sref;
		u64 dref_offset;
		u64 inline_offset;
		u8 inline_type;

		if (ptr + sizeof(*iref) > end) {
			extent_err(leaf, slot,
"inline ref item overflows extent item, ptr %lu iref size %zu end %lu",
				   ptr, sizeof(*iref), end);
			return -EUCLEAN;
		}
		iref = (struct btrfs_extent_inline_ref *)ptr;
		inline_type = btrfs_extent_inline_ref_type(leaf, iref);
		inline_offset = btrfs_extent_inline_ref_offset(leaf, iref);
		if (ptr + btrfs_extent_inline_ref_size(inline_type) > end) {
			extent_err(leaf, slot,
"inline ref item overflows extent item, ptr %lu iref size %u end %lu",
				   ptr, inline_type, end);
			return -EUCLEAN;
		}

		switch (inline_type) {
		/* inline_offset is subvolid of the owner, no need to check */
		case BTRFS_TREE_BLOCK_REF_KEY:
			inline_refs++;
			break;
		/* Contains parent bytenr */
		case BTRFS_SHARED_BLOCK_REF_KEY:
			if (!IS_ALIGNED(inline_offset, fs_info->sectorsize)) {
				extent_err(leaf, slot,
		"invalid tree parent bytenr, have %llu expect aligned to %u",
					   inline_offset, fs_info->sectorsize);
				return -EUCLEAN;
			}
			inline_refs++;
			break;
		/*
		 * Contains owner subvolid, owner key objectid, adjusted offset.
		 * The only obvious corruption can happen in that offset.
		 */
		case BTRFS_EXTENT_DATA_REF_KEY:
			dref = (struct btrfs_extent_data_ref *)(&iref->offset);
			dref_offset = btrfs_extent_data_ref_offset(leaf, dref);
			if (!IS_ALIGNED(dref_offset, fs_info->sectorsize)) {
				extent_err(leaf, slot,
		"invalid data ref offset, have %llu expect aligned to %u",
					   dref_offset, fs_info->sectorsize);
				return -EUCLEAN;
			}
			inline_refs += btrfs_extent_data_ref_count(leaf, dref);
			break;
		/* Contains parent bytenr and ref count */
		case BTRFS_SHARED_DATA_REF_KEY:
			sref = (struct btrfs_shared_data_ref *)(iref + 1);
			if (!IS_ALIGNED(inline_offset, fs_info->sectorsize)) {
				extent_err(leaf, slot,
		"invalid data parent bytenr, have %llu expect aligned to %u",
					   inline_offset, fs_info->sectorsize);
				return -EUCLEAN;
			}
			inline_refs += btrfs_shared_data_ref_count(leaf, sref);
			break;
		default:
			extent_err(leaf, slot, "unknown inline ref type: %u",
				   inline_type);
			return -EUCLEAN;
		}
		ptr += btrfs_extent_inline_ref_size(inline_type);
	}
	/* No padding is allowed */
	if (ptr != end) {
		extent_err(leaf, slot,
			   "invalid extent item size, padding bytes found");
		return -EUCLEAN;
	}

	/* Finally, check the inline refs against total refs */
	if (inline_refs > total_refs) {
		extent_err(leaf, slot,
			"invalid extent refs, have %llu expect >= inline %llu",
			   total_refs, inline_refs);
		return -EUCLEAN;
	}
	return 0;
}

static int check_simple_keyed_refs(struct extent_buffer *leaf,
				   struct btrfs_key *key, int slot)
{
	u32 expect_item_size = 0;

	if (key->type == BTRFS_SHARED_DATA_REF_KEY)
		expect_item_size = sizeof(struct btrfs_shared_data_ref);

	if (btrfs_item_size_nr(leaf, slot) != expect_item_size) {
		generic_err(leaf, slot,
		"invalid item size, have %u expect %u for key type %u",
			    btrfs_item_size_nr(leaf, slot),
			    expect_item_size, key->type);
		return -EUCLEAN;
	}
	if (!IS_ALIGNED(key->objectid, leaf->fs_info->sectorsize)) {
		generic_err(leaf, slot,
"invalid key objectid for shared block ref, have %llu expect aligned to %u",
			    key->objectid, leaf->fs_info->sectorsize);
		return -EUCLEAN;
	}
	if (key->type != BTRFS_TREE_BLOCK_REF_KEY &&
	    !IS_ALIGNED(key->offset, leaf->fs_info->sectorsize)) {
		extent_err(leaf, slot,
		"invalid tree parent bytenr, have %llu expect aligned to %u",
			   key->offset, leaf->fs_info->sectorsize);
		return -EUCLEAN;
	}
	return 0;
}

static int check_extent_data_ref(struct extent_buffer *leaf,
				 struct btrfs_key *key, int slot)
{
	struct btrfs_extent_data_ref *dref;
	unsigned long ptr = btrfs_item_ptr_offset(leaf, slot);
	const unsigned long end = ptr + btrfs_item_size_nr(leaf, slot);

	if (btrfs_item_size_nr(leaf, slot) % sizeof(*dref) != 0) {
		generic_err(leaf, slot,
	"invalid item size, have %u expect aligned to %zu for key type %u",
			    btrfs_item_size_nr(leaf, slot),
			    sizeof(*dref), key->type);
	}
	if (!IS_ALIGNED(key->objectid, leaf->fs_info->sectorsize)) {
		generic_err(leaf, slot,
"invalid key objectid for shared block ref, have %llu expect aligned to %u",
			    key->objectid, leaf->fs_info->sectorsize);
		return -EUCLEAN;
	}
	for (; ptr < end; ptr += sizeof(*dref)) {
		u64 root_objectid;
		u64 owner;
		u64 offset;
		u64 hash;

		dref = (struct btrfs_extent_data_ref *)ptr;
		root_objectid = btrfs_extent_data_ref_root(leaf, dref);
		owner = btrfs_extent_data_ref_objectid(leaf, dref);
		offset = btrfs_extent_data_ref_offset(leaf, dref);
		hash = hash_extent_data_ref(root_objectid, owner, offset);
		if (hash != key->offset) {
			extent_err(leaf, slot,
	"invalid extent data ref hash, item has 0x%016llx key has 0x%016llx",
				   hash, key->offset);
			return -EUCLEAN;
		}
		if (!IS_ALIGNED(offset, leaf->fs_info->sectorsize)) {
			extent_err(leaf, slot,
	"invalid extent data backref offset, have %llu expect aligned to %u",
				   offset, leaf->fs_info->sectorsize);
		}
	}
	return 0;
}

#define inode_ref_err(fs_info, eb, slot, fmt, args...)			\
	inode_item_err(fs_info, eb, slot, fmt, ##args)
static int check_inode_ref(struct extent_buffer *leaf,
			   struct btrfs_key *key, struct btrfs_key *prev_key,
			   int slot)
{
	struct btrfs_inode_ref *iref;
	unsigned long ptr;
	unsigned long end;

	if (!check_prev_ino(leaf, key, slot, prev_key))
		return -EUCLEAN;
	/* namelen can't be 0, so item_size == sizeof() is also invalid */
	if (btrfs_item_size_nr(leaf, slot) <= sizeof(*iref)) {
		inode_ref_err(fs_info, leaf, slot,
			"invalid item size, have %u expect (%zu, %u)",
			btrfs_item_size_nr(leaf, slot),
			sizeof(*iref), BTRFS_LEAF_DATA_SIZE(leaf->fs_info));
		return -EUCLEAN;
	}

	ptr = btrfs_item_ptr_offset(leaf, slot);
	end = ptr + btrfs_item_size_nr(leaf, slot);
	while (ptr < end) {
		u16 namelen;

		if (ptr + sizeof(iref) > end) {
			inode_ref_err(fs_info, leaf, slot,
			"inode ref overflow, ptr %lu end %lu inode_ref_size %zu",
				ptr, end, sizeof(iref));
			return -EUCLEAN;
		}

		iref = (struct btrfs_inode_ref *)ptr;
		namelen = btrfs_inode_ref_name_len(leaf, iref);
		if (ptr + sizeof(*iref) + namelen > end) {
			inode_ref_err(fs_info, leaf, slot,
				"inode ref overflow, ptr %lu end %lu namelen %u",
				ptr, end, namelen);
			return -EUCLEAN;
		}

		/*
		 * NOTE: In theory we should record all found index numbers
		 * to find any duplicated indexes, but that will be too time
		 * consuming for inodes with too many hard links.
		 */
		ptr += sizeof(*iref) + namelen;
	}
	return 0;
}

/*
 * Common point to switch the item-specific validation.
 */
static int check_leaf_item(struct extent_buffer *leaf,
			   struct btrfs_key *key, int slot,
			   struct btrfs_key *prev_key)
{
	int ret = 0;
	struct btrfs_chunk *chunk;

	switch (key->type) {
	case BTRFS_EXTENT_DATA_KEY:
		ret = check_extent_data_item(leaf, key, slot, prev_key);
		break;
	case BTRFS_EXTENT_CSUM_KEY:
		ret = check_csum_item(leaf, key, slot);
		break;
	case BTRFS_DIR_ITEM_KEY:
	case BTRFS_DIR_INDEX_KEY:
	case BTRFS_XATTR_ITEM_KEY:
		ret = check_dir_item(leaf, key, prev_key, slot);
		break;
	case BTRFS_INODE_REF_KEY:
		ret = check_inode_ref(leaf, key, prev_key, slot);
		break;
	case BTRFS_BLOCK_GROUP_ITEM_KEY:
		ret = check_block_group_item(leaf, key, slot);
		break;
	case BTRFS_CHUNK_ITEM_KEY:
		chunk = btrfs_item_ptr(leaf, slot, struct btrfs_chunk);
		ret = btrfs_check_chunk_valid(leaf, chunk, key->offset);
		break;
	case BTRFS_DEV_ITEM_KEY:
		ret = check_dev_item(leaf, key, slot);
		break;
	case BTRFS_INODE_ITEM_KEY:
		ret = check_inode_item(leaf, key, slot);
		break;
	case BTRFS_ROOT_ITEM_KEY:
		ret = check_root_item(leaf, key, slot);
		break;
	case BTRFS_EXTENT_ITEM_KEY:
	case BTRFS_METADATA_ITEM_KEY:
		ret = check_extent_item(leaf, key, slot);
		break;
	case BTRFS_TREE_BLOCK_REF_KEY:
	case BTRFS_SHARED_DATA_REF_KEY:
	case BTRFS_SHARED_BLOCK_REF_KEY:
		ret = check_simple_keyed_refs(leaf, key, slot);
		break;
	case BTRFS_EXTENT_DATA_REF_KEY:
		ret = check_extent_data_ref(leaf, key, slot);
		break;
	}
	return ret;
}

static int check_leaf(struct extent_buffer *leaf, bool check_item_data)
{
	struct btrfs_fs_info *fs_info = leaf->fs_info;
	/* No valid key type is 0, so all key should be larger than this key */
	struct btrfs_key prev_key = {0, 0, 0};
	struct btrfs_key key;
	u32 nritems = btrfs_header_nritems(leaf);
	int slot;

	if (btrfs_header_level(leaf) != 0) {
		generic_err(leaf, 0,
			"invalid level for leaf, have %d expect 0",
			btrfs_header_level(leaf));
		return -EUCLEAN;
	}

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

		/* These trees must never be empty */
		if (owner == BTRFS_ROOT_TREE_OBJECTID ||
		    owner == BTRFS_CHUNK_TREE_OBJECTID ||
		    owner == BTRFS_EXTENT_TREE_OBJECTID ||
		    owner == BTRFS_DEV_TREE_OBJECTID ||
		    owner == BTRFS_FS_TREE_OBJECTID ||
		    owner == BTRFS_DATA_RELOC_TREE_OBJECTID) {
			generic_err(leaf, 0,
			"invalid root, root %llu must never be empty",
				    owner);
			return -EUCLEAN;
		}
		/* Unknown tree */
		if (owner == 0) {
			generic_err(leaf, 0,
				"invalid owner, root 0 is not defined");
			return -EUCLEAN;
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
			generic_err(leaf, slot,
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
			generic_err(leaf, slot,
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
			generic_err(leaf, slot,
			"slot end outside of leaf, have %u expect range [0, %u]",
				btrfs_item_end_nr(leaf, slot),
				BTRFS_LEAF_DATA_SIZE(fs_info));
			return -EUCLEAN;
		}

		/* Also check if the item pointer overlaps with btrfs item. */
		if (btrfs_item_nr_offset(slot) + sizeof(struct btrfs_item) >
		    btrfs_item_ptr_offset(leaf, slot)) {
			generic_err(leaf, slot,
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
			ret = check_leaf_item(leaf, &key, slot, &prev_key);
			if (ret < 0)
				return ret;
		}

		prev_key.objectid = key.objectid;
		prev_key.type = key.type;
		prev_key.offset = key.offset;
	}

	return 0;
}

int btrfs_check_leaf_full(struct extent_buffer *leaf)
{
	return check_leaf(leaf, true);
}
ALLOW_ERROR_INJECTION(btrfs_check_leaf_full, ERRNO);

int btrfs_check_leaf_relaxed(struct extent_buffer *leaf)
{
	return check_leaf(leaf, false);
}

int btrfs_check_node(struct extent_buffer *node)
{
	struct btrfs_fs_info *fs_info = node->fs_info;
	unsigned long nr = btrfs_header_nritems(node);
	struct btrfs_key key, next_key;
	int slot;
	int level = btrfs_header_level(node);
	u64 bytenr;
	int ret = 0;

	if (level <= 0 || level >= BTRFS_MAX_LEVEL) {
		generic_err(node, 0,
			"invalid level for node, have %d expect [1, %d]",
			level, BTRFS_MAX_LEVEL - 1);
		return -EUCLEAN;
	}
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
			generic_err(node, slot,
				"invalid NULL node pointer");
			ret = -EUCLEAN;
			goto out;
		}
		if (!IS_ALIGNED(bytenr, fs_info->sectorsize)) {
			generic_err(node, slot,
			"unaligned pointer, have %llu should be aligned to %u",
				bytenr, fs_info->sectorsize);
			ret = -EUCLEAN;
			goto out;
		}

		if (btrfs_comp_cpu_keys(&key, &next_key) >= 0) {
			generic_err(node, slot,
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
ALLOW_ERROR_INJECTION(btrfs_check_node, ERRNO);
