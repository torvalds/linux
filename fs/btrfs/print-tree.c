// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include "messages.h"
#include "ctree.h"
#include "disk-io.h"
#include "file-item.h"
#include "print-tree.h"
#include "accessors.h"
#include "tree-checker.h"
#include "volumes.h"
#include "raid-stripe-tree.h"

/*
 * Large enough buffer size for the stringification of any key type yet short
 * enough to use the stack and avoid allocations.
 */
#define KEY_TYPE_BUF_SIZE 32

struct root_name_map {
	u64 id;
	const char *name;
};

static const struct root_name_map root_map[] = {
	{ BTRFS_ROOT_TREE_OBJECTID,		"ROOT_TREE"		},
	{ BTRFS_EXTENT_TREE_OBJECTID,		"EXTENT_TREE"		},
	{ BTRFS_CHUNK_TREE_OBJECTID,		"CHUNK_TREE"		},
	{ BTRFS_DEV_TREE_OBJECTID,		"DEV_TREE"		},
	{ BTRFS_FS_TREE_OBJECTID,		"FS_TREE"		},
	{ BTRFS_CSUM_TREE_OBJECTID,		"CSUM_TREE"		},
	{ BTRFS_TREE_LOG_OBJECTID,		"TREE_LOG"		},
	{ BTRFS_QUOTA_TREE_OBJECTID,		"QUOTA_TREE"		},
	{ BTRFS_UUID_TREE_OBJECTID,		"UUID_TREE"		},
	{ BTRFS_FREE_SPACE_TREE_OBJECTID,	"FREE_SPACE_TREE"	},
	{ BTRFS_BLOCK_GROUP_TREE_OBJECTID,	"BLOCK_GROUP_TREE"	},
	{ BTRFS_DATA_RELOC_TREE_OBJECTID,	"DATA_RELOC_TREE"	},
	{ BTRFS_RAID_STRIPE_TREE_OBJECTID,	"RAID_STRIPE_TREE"	},
};

const char *btrfs_root_name(const struct btrfs_key *key, char *buf)
{
	int i;

	if (key->objectid == BTRFS_TREE_RELOC_OBJECTID) {
		snprintf(buf, BTRFS_ROOT_NAME_BUF_LEN,
			 "TREE_RELOC offset=%llu", key->offset);
		return buf;
	}

	for (i = 0; i < ARRAY_SIZE(root_map); i++) {
		if (root_map[i].id == key->objectid)
			return root_map[i].name;
	}

	snprintf(buf, BTRFS_ROOT_NAME_BUF_LEN, "%llu", key->objectid);
	return buf;
}

static void print_chunk(const struct extent_buffer *eb, struct btrfs_chunk *chunk)
{
	int num_stripes = btrfs_chunk_num_stripes(eb, chunk);
	int i;
	pr_info("\t\tchunk length %llu owner %llu type %llu num_stripes %d\n",
	       btrfs_chunk_length(eb, chunk), btrfs_chunk_owner(eb, chunk),
	       btrfs_chunk_type(eb, chunk), num_stripes);
	for (i = 0 ; i < num_stripes ; i++) {
		pr_info("\t\t\tstripe %d devid %llu offset %llu\n", i,
		      btrfs_stripe_devid_nr(eb, chunk, i),
		      btrfs_stripe_offset_nr(eb, chunk, i));
	}
}
static void print_dev_item(const struct extent_buffer *eb,
			   struct btrfs_dev_item *dev_item)
{
	pr_info("\t\tdev item devid %llu total_bytes %llu bytes used %llu\n",
	       btrfs_device_id(eb, dev_item),
	       btrfs_device_total_bytes(eb, dev_item),
	       btrfs_device_bytes_used(eb, dev_item));
}
static void print_extent_data_ref(const struct extent_buffer *eb,
				  struct btrfs_extent_data_ref *ref)
{
	pr_cont("extent data backref root %llu objectid %llu offset %llu count %u\n",
	       btrfs_extent_data_ref_root(eb, ref),
	       btrfs_extent_data_ref_objectid(eb, ref),
	       btrfs_extent_data_ref_offset(eb, ref),
	       btrfs_extent_data_ref_count(eb, ref));
}

static void print_extent_owner_ref(const struct extent_buffer *eb,
				   const struct btrfs_extent_owner_ref *ref)
{
	ASSERT(btrfs_fs_incompat(eb->fs_info, SIMPLE_QUOTA));
	pr_cont("extent data owner root %llu\n", btrfs_extent_owner_ref_root_id(eb, ref));
}

static void print_extent_item(const struct extent_buffer *eb, int slot, int type)
{
	struct btrfs_extent_item *ei;
	struct btrfs_extent_inline_ref *iref;
	struct btrfs_extent_data_ref *dref;
	struct btrfs_shared_data_ref *sref;
	struct btrfs_extent_owner_ref *oref;
	struct btrfs_disk_key key;
	unsigned long end;
	unsigned long ptr;
	u32 item_size = btrfs_item_size(eb, slot);
	u64 flags;
	u64 offset;
	int ref_index = 0;

	if (unlikely(item_size < sizeof(*ei))) {
		btrfs_err(eb->fs_info,
			  "unexpected extent item size, has %u expect >= %zu",
			  item_size, sizeof(*ei));
		return;
	}

	ei = btrfs_item_ptr(eb, slot, struct btrfs_extent_item);
	flags = btrfs_extent_flags(eb, ei);

	pr_info("\t\textent refs %llu gen %llu flags %llu\n",
	       btrfs_extent_refs(eb, ei), btrfs_extent_generation(eb, ei),
	       flags);

	if ((type == BTRFS_EXTENT_ITEM_KEY) &&
	    flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
		struct btrfs_tree_block_info *info;
		info = (struct btrfs_tree_block_info *)(ei + 1);
		btrfs_tree_block_key(eb, info, &key);
		pr_info("\t\ttree block key (%llu %u %llu) level %d\n",
		       btrfs_disk_key_objectid(&key), key.type,
		       btrfs_disk_key_offset(&key),
		       btrfs_tree_block_level(eb, info));
		iref = (struct btrfs_extent_inline_ref *)(info + 1);
	} else {
		iref = (struct btrfs_extent_inline_ref *)(ei + 1);
	}

	ptr = (unsigned long)iref;
	end = (unsigned long)ei + item_size;
	while (ptr < end) {
		iref = (struct btrfs_extent_inline_ref *)ptr;
		type = btrfs_extent_inline_ref_type(eb, iref);
		offset = btrfs_extent_inline_ref_offset(eb, iref);
		pr_info("\t\tref#%d: ", ref_index++);
		switch (type) {
		case BTRFS_TREE_BLOCK_REF_KEY:
			pr_cont("tree block backref root %llu\n", offset);
			break;
		case BTRFS_SHARED_BLOCK_REF_KEY:
			pr_cont("shared block backref parent %llu\n", offset);
			/*
			 * offset is supposed to be a tree block which
			 * must be aligned to nodesize.
			 */
			if (!IS_ALIGNED(offset, eb->fs_info->sectorsize))
				pr_info(
			"\t\t\t(parent %llu not aligned to sectorsize %u)\n",
					offset, eb->fs_info->sectorsize);
			break;
		case BTRFS_EXTENT_DATA_REF_KEY:
			dref = (struct btrfs_extent_data_ref *)(&iref->offset);
			print_extent_data_ref(eb, dref);
			break;
		case BTRFS_SHARED_DATA_REF_KEY:
			sref = (struct btrfs_shared_data_ref *)(iref + 1);
			pr_cont("shared data backref parent %llu count %u\n",
			       offset, btrfs_shared_data_ref_count(eb, sref));
			/*
			 * Offset is supposed to be a tree block which must be
			 * aligned to sectorsize.
			 */
			if (!IS_ALIGNED(offset, eb->fs_info->sectorsize))
				pr_info(
			"\t\t\t(parent %llu not aligned to sectorsize %u)\n",
				     offset, eb->fs_info->sectorsize);
			break;
		case BTRFS_EXTENT_OWNER_REF_KEY:
			oref = (struct btrfs_extent_owner_ref *)(&iref->offset);
			print_extent_owner_ref(eb, oref);
			break;
		default:
			pr_cont("(extent %llu has INVALID ref type %d)\n",
				  eb->start, type);
			return;
		}
		ptr += btrfs_extent_inline_ref_size(type);
	}
	WARN_ON(ptr > end);
}

static void print_uuid_item(const struct extent_buffer *l, unsigned long offset,
			    u32 item_size)
{
	if (!IS_ALIGNED(item_size, sizeof(u64))) {
		btrfs_warn(l->fs_info, "uuid item with illegal size %lu",
			(unsigned long)item_size);
		return;
	}
	while (item_size) {
		__le64 subvol_id;

		read_extent_buffer(l, &subvol_id, offset, sizeof(subvol_id));
		pr_info("\t\tsubvol_id %llu\n", le64_to_cpu(subvol_id));
		item_size -= sizeof(u64);
		offset += sizeof(u64);
	}
}

static void print_raid_stripe_key(const struct extent_buffer *eb, u32 item_size,
				  struct btrfs_stripe_extent *stripe)
{
	const int num_stripes = btrfs_num_raid_stripes(item_size);

	for (int i = 0; i < num_stripes; i++)
		pr_info("\t\t\tstride %d devid %llu physical %llu\n",
			i, btrfs_raid_stride_devid(eb, &stripe->strides[i]),
			btrfs_raid_stride_physical(eb, &stripe->strides[i]));
}

/*
 * Helper to output refs and locking status of extent buffer.  Useful to debug
 * race condition related problems.
 */
static void print_eb_refs_lock(const struct extent_buffer *eb)
{
#ifdef CONFIG_BTRFS_DEBUG
	btrfs_info(eb->fs_info, "refs %u lock_owner %u current %u",
		   refcount_read(&eb->refs), eb->lock_owner, current->pid);
#endif
}

static void print_timespec(const struct extent_buffer *eb,
			   struct btrfs_timespec *timespec,
			   const char *prefix, const char *suffix)
{
	const u64 secs = btrfs_timespec_sec(eb, timespec);
	const u32 nsecs = btrfs_timespec_nsec(eb, timespec);

	pr_info("%s%llu.%u%s", prefix, secs, nsecs, suffix);
}

static void print_inode_item(const struct extent_buffer *eb, int i)
{
	struct btrfs_inode_item *ii = btrfs_item_ptr(eb, i, struct btrfs_inode_item);

	pr_info("\t\tinode generation %llu transid %llu size %llu nbytes %llu\n",
		btrfs_inode_generation(eb, ii), btrfs_inode_transid(eb, ii),
		btrfs_inode_size(eb, ii), btrfs_inode_nbytes(eb, ii));
	pr_info("\t\tblock group %llu mode %o links %u uid %u gid %u\n",
		btrfs_inode_block_group(eb, ii), btrfs_inode_mode(eb, ii),
		btrfs_inode_nlink(eb, ii), btrfs_inode_uid(eb, ii),
		btrfs_inode_gid(eb, ii));
	pr_info("\t\trdev %llu sequence %llu flags 0x%llx\n",
		btrfs_inode_rdev(eb, ii), btrfs_inode_sequence(eb, ii),
		btrfs_inode_flags(eb, ii));
	print_timespec(eb, &ii->atime, "\t\tatime ", "\n");
	print_timespec(eb, &ii->ctime, "\t\tctime ", "\n");
	print_timespec(eb, &ii->mtime, "\t\tmtime ", "\n");
	print_timespec(eb, &ii->otime, "\t\totime ", "\n");
}

static void print_dir_item(const struct extent_buffer *eb, int i)
{
	const u32 size = btrfs_item_size(eb, i);
	struct btrfs_dir_item *di = btrfs_item_ptr(eb, i, struct btrfs_dir_item);
	u32 cur = 0;

	while (cur < size) {
		const u32 name_len = btrfs_dir_name_len(eb, di);
		const u32 data_len = btrfs_dir_data_len(eb, di);
		const u32 len = sizeof(*di) + name_len + data_len;
		struct btrfs_key location;

		btrfs_dir_item_key_to_cpu(eb, di, &location);
		pr_info("\t\tlocation key (%llu %u %llu) type %d\n",
			location.objectid, location.type, location.offset,
			btrfs_dir_ftype(eb, di));
		pr_info("\t\ttransid %llu data_len %u name_len %u\n",
			btrfs_dir_transid(eb, di), data_len, name_len);
		di = (struct btrfs_dir_item *)((char *)di + len);
		cur += len;
	}
}

static void print_inode_ref_item(const struct extent_buffer *eb, int i)
{
	const u32 size = btrfs_item_size(eb, i);
	struct btrfs_inode_ref *ref = btrfs_item_ptr(eb, i, struct btrfs_inode_ref);
	u32 cur = 0;

	while (cur < size) {
		const u64 index = btrfs_inode_ref_index(eb, ref);
		const u32 name_len = btrfs_inode_ref_name_len(eb, ref);
		const u32 len = sizeof(*ref) + name_len;

		pr_info("\t\tindex %llu name_len %u\n", index, name_len);
		ref = (struct btrfs_inode_ref *)((char *)ref + len);
		cur += len;
	}
}

static void print_inode_extref_item(const struct extent_buffer *eb, int i)
{
	const u32 size = btrfs_item_size(eb, i);
	struct btrfs_inode_extref *extref;
	u32 cur = 0;

	extref = btrfs_item_ptr(eb, i, struct btrfs_inode_extref);
	while (cur < size) {
		const u64 index = btrfs_inode_extref_index(eb, extref);
		const u32 name_len = btrfs_inode_extref_name_len(eb, extref);
		const u64 parent = btrfs_inode_extref_parent(eb, extref);
		const u32 len = sizeof(*extref) + name_len;

		pr_info("\t\tindex %llu parent %llu name_len %u\n",
			index, parent, name_len);
		extref = (struct btrfs_inode_extref *)((char *)extref + len);
		cur += len;
	}
}

static void print_dir_log_index_item(const struct extent_buffer *eb, int i)
{
	struct btrfs_dir_log_item *dlog;

	dlog = btrfs_item_ptr(eb, i, struct btrfs_dir_log_item);
	pr_info("\t\tdir log end %llu\n", btrfs_dir_log_end(eb, dlog));
}

static void print_extent_csum(const struct extent_buffer *eb, int i)
{
	const struct btrfs_fs_info *fs_info = eb->fs_info;
	const u32 size = btrfs_item_size(eb, i);
	const u32 csum_bytes = (size / fs_info->csum_size) * fs_info->sectorsize;
	struct btrfs_key key;

	btrfs_item_key_to_cpu(eb, &key, i);
	pr_info("\t\trange start %llu end %llu length %u\n",
		key.offset, key.offset + csum_bytes, csum_bytes);
}

static void print_file_extent_item(const struct extent_buffer *eb, int i)
{
	struct btrfs_file_extent_item *fi;

	fi = btrfs_item_ptr(eb, i, struct btrfs_file_extent_item);
	pr_info("\t\tgeneration %llu type %hhu\n",
		btrfs_file_extent_generation(eb, fi),
		btrfs_file_extent_type(eb, fi));

	if (btrfs_file_extent_type(eb, fi) == BTRFS_FILE_EXTENT_INLINE) {
		pr_info("\t\tinline extent data size %u ram_bytes %llu compression %hhu\n",
			btrfs_file_extent_inline_item_len(eb, i),
			btrfs_file_extent_ram_bytes(eb, fi),
			btrfs_file_extent_compression(eb, fi));
		return;
	}

	pr_info("\t\textent data disk bytenr %llu nr %llu\n",
		btrfs_file_extent_disk_bytenr(eb, fi),
		btrfs_file_extent_disk_num_bytes(eb, fi));
	pr_info("\t\textent data offset %llu nr %llu ram %llu\n",
		btrfs_file_extent_offset(eb, fi),
		btrfs_file_extent_num_bytes(eb, fi),
		btrfs_file_extent_ram_bytes(eb, fi));
	pr_info("\t\textent compression %hhu\n",
		btrfs_file_extent_compression(eb, fi));
}

static void key_type_string(const struct btrfs_key *key, char *buf, int buf_size)
{
	static const char *key_to_str[256] = {
		[BTRFS_INODE_ITEM_KEY]			= "INODE_ITEM",
		[BTRFS_INODE_REF_KEY]			= "INODE_REF",
		[BTRFS_INODE_EXTREF_KEY]		= "INODE_EXTREF",
		[BTRFS_DIR_ITEM_KEY]			= "DIR_ITEM",
		[BTRFS_DIR_INDEX_KEY]			= "DIR_INDEX",
		[BTRFS_DIR_LOG_ITEM_KEY]		= "DIR_LOG_ITEM",
		[BTRFS_DIR_LOG_INDEX_KEY]		= "DIR_LOG_INDEX",
		[BTRFS_XATTR_ITEM_KEY]			= "XATTR_ITEM",
		[BTRFS_VERITY_DESC_ITEM_KEY]		= "VERITY_DESC_ITEM",
		[BTRFS_VERITY_MERKLE_ITEM_KEY]		= "VERITY_MERKLE_ITEM",
		[BTRFS_ORPHAN_ITEM_KEY]			= "ORPHAN_ITEM",
		[BTRFS_ROOT_ITEM_KEY]			= "ROOT_ITEM",
		[BTRFS_ROOT_REF_KEY]			= "ROOT_REF",
		[BTRFS_ROOT_BACKREF_KEY]		= "ROOT_BACKREF",
		[BTRFS_EXTENT_ITEM_KEY]			= "EXTENT_ITEM",
		[BTRFS_METADATA_ITEM_KEY]		= "METADATA_ITEM",
		[BTRFS_TREE_BLOCK_REF_KEY]		= "TREE_BLOCK_REF",
		[BTRFS_SHARED_BLOCK_REF_KEY]		= "SHARED_BLOCK_REF",
		[BTRFS_EXTENT_DATA_REF_KEY]		= "EXTENT_DATA_REF",
		[BTRFS_SHARED_DATA_REF_KEY]		= "SHARED_DATA_REF",
		[BTRFS_EXTENT_OWNER_REF_KEY]		= "EXTENT_OWNER_REF",
		[BTRFS_EXTENT_CSUM_KEY]			= "EXTENT_CSUM",
		[BTRFS_EXTENT_DATA_KEY]			= "EXTENT_DATA",
		[BTRFS_BLOCK_GROUP_ITEM_KEY]		= "BLOCK_GROUP_ITEM",
		[BTRFS_FREE_SPACE_INFO_KEY]		= "FREE_SPACE_INFO",
		[BTRFS_FREE_SPACE_EXTENT_KEY]		= "FREE_SPACE_EXTENT",
		[BTRFS_FREE_SPACE_BITMAP_KEY]		= "FREE_SPACE_BITMAP",
		[BTRFS_CHUNK_ITEM_KEY]			= "CHUNK_ITEM",
		[BTRFS_DEV_ITEM_KEY]			= "DEV_ITEM",
		[BTRFS_DEV_EXTENT_KEY]			= "DEV_EXTENT",
		[BTRFS_TEMPORARY_ITEM_KEY]		= "TEMPORARY_ITEM",
		[BTRFS_DEV_REPLACE_KEY]			= "DEV_REPLACE",
		[BTRFS_STRING_ITEM_KEY]			= "STRING_ITEM",
		[BTRFS_QGROUP_STATUS_KEY]		= "QGROUP_STATUS",
		[BTRFS_QGROUP_RELATION_KEY]		= "QGROUP_RELATION",
		[BTRFS_QGROUP_INFO_KEY]			= "QGROUP_INFO",
		[BTRFS_QGROUP_LIMIT_KEY]		= "QGROUP_LIMIT",
		[BTRFS_PERSISTENT_ITEM_KEY]		= "PERSISTENT_ITEM",
		[BTRFS_UUID_KEY_SUBVOL]			= "UUID_KEY_SUBVOL",
		[BTRFS_UUID_KEY_RECEIVED_SUBVOL]	= "UUID_KEY_RECEIVED_SUBVOL",
		[BTRFS_RAID_STRIPE_KEY]			= "RAID_STRIPE",
	};

	if (key->type == 0 && key->objectid == BTRFS_FREE_SPACE_OBJECTID)
		scnprintf(buf, buf_size, "UNTYPED");
	else if (key_to_str[key->type])
		scnprintf(buf, buf_size, key_to_str[key->type]);
	else
		scnprintf(buf, buf_size, "UNKNOWN.%d", key->type);
}

void btrfs_print_leaf(const struct extent_buffer *l)
{
	struct btrfs_fs_info *fs_info;
	int i;
	u32 type, nr;
	struct btrfs_root_item *ri;
	struct btrfs_block_group_item *bi;
	struct btrfs_extent_data_ref *dref;
	struct btrfs_shared_data_ref *sref;
	struct btrfs_dev_extent *dev_extent;
	struct btrfs_key key;

	if (!l)
		return;

	fs_info = l->fs_info;
	nr = btrfs_header_nritems(l);

	btrfs_info(fs_info,
		   "leaf %llu gen %llu total ptrs %d free space %d owner %llu",
		   btrfs_header_bytenr(l), btrfs_header_generation(l), nr,
		   btrfs_leaf_free_space(l), btrfs_header_owner(l));
	print_eb_refs_lock(l);
	for (i = 0 ; i < nr ; i++) {
		char key_buf[KEY_TYPE_BUF_SIZE];

		btrfs_item_key_to_cpu(l, &key, i);
		type = key.type;
		key_type_string(&key, key_buf, KEY_TYPE_BUF_SIZE);

		pr_info("\titem %d key (%llu %s %llu) itemoff %d itemsize %d\n",
			i, key.objectid, key_buf, key.offset,
			btrfs_item_offset(l, i), btrfs_item_size(l, i));
		switch (type) {
		case BTRFS_INODE_ITEM_KEY:
			print_inode_item(l, i);
			break;
		case BTRFS_INODE_REF_KEY:
			print_inode_ref_item(l, i);
			break;
		case BTRFS_INODE_EXTREF_KEY:
			print_inode_extref_item(l, i);
			break;
		case BTRFS_DIR_ITEM_KEY:
		case BTRFS_DIR_INDEX_KEY:
		case BTRFS_XATTR_ITEM_KEY:
			print_dir_item(l, i);
			break;
		case BTRFS_DIR_LOG_INDEX_KEY:
			print_dir_log_index_item(l, i);
			break;
		case BTRFS_EXTENT_CSUM_KEY:
			print_extent_csum(l, i);
			break;
		case BTRFS_ROOT_ITEM_KEY:
			ri = btrfs_item_ptr(l, i, struct btrfs_root_item);
			pr_info("\t\troot data bytenr %llu refs %u\n",
				btrfs_disk_root_bytenr(l, ri),
				btrfs_disk_root_refs(l, ri));
			break;
		case BTRFS_EXTENT_ITEM_KEY:
		case BTRFS_METADATA_ITEM_KEY:
			print_extent_item(l, i, type);
			break;
		case BTRFS_TREE_BLOCK_REF_KEY:
			pr_info("\t\ttree block backref\n");
			break;
		case BTRFS_SHARED_BLOCK_REF_KEY:
			pr_info("\t\tshared block backref\n");
			break;
		case BTRFS_EXTENT_DATA_REF_KEY:
			dref = btrfs_item_ptr(l, i,
					      struct btrfs_extent_data_ref);
			print_extent_data_ref(l, dref);
			break;
		case BTRFS_SHARED_DATA_REF_KEY:
			sref = btrfs_item_ptr(l, i,
					      struct btrfs_shared_data_ref);
			pr_info("\t\tshared data backref count %u\n",
			       btrfs_shared_data_ref_count(l, sref));
			break;
		case BTRFS_EXTENT_DATA_KEY:
			print_file_extent_item(l, i);
			break;
		case BTRFS_BLOCK_GROUP_ITEM_KEY:
			bi = btrfs_item_ptr(l, i,
					    struct btrfs_block_group_item);
			pr_info(
		   "\t\tblock group used %llu chunk_objectid %llu flags %llu\n",
				btrfs_block_group_used(l, bi),
				btrfs_block_group_chunk_objectid(l, bi),
				btrfs_block_group_flags(l, bi));
			break;
		case BTRFS_CHUNK_ITEM_KEY:
			print_chunk(l, btrfs_item_ptr(l, i,
						      struct btrfs_chunk));
			break;
		case BTRFS_DEV_ITEM_KEY:
			print_dev_item(l, btrfs_item_ptr(l, i,
					struct btrfs_dev_item));
			break;
		case BTRFS_DEV_EXTENT_KEY:
			dev_extent = btrfs_item_ptr(l, i,
						    struct btrfs_dev_extent);
			pr_info("\t\tdev extent chunk_tree %llu\n\t\tchunk objectid %llu chunk offset %llu length %llu\n",
			       btrfs_dev_extent_chunk_tree(l, dev_extent),
			       btrfs_dev_extent_chunk_objectid(l, dev_extent),
			       btrfs_dev_extent_chunk_offset(l, dev_extent),
			       btrfs_dev_extent_length(l, dev_extent));
			break;
		case BTRFS_PERSISTENT_ITEM_KEY:
			pr_info("\t\tpersistent item objectid %llu offset %llu\n",
					key.objectid, key.offset);
			switch (key.objectid) {
			case BTRFS_DEV_STATS_OBJECTID:
				pr_info("\t\tdevice stats\n");
				break;
			default:
				pr_info("\t\tunknown persistent item\n");
			}
			break;
		case BTRFS_TEMPORARY_ITEM_KEY:
			pr_info("\t\ttemporary item objectid %llu offset %llu\n",
					key.objectid, key.offset);
			switch (key.objectid) {
			case BTRFS_BALANCE_OBJECTID:
				pr_info("\t\tbalance status\n");
				break;
			default:
				pr_info("\t\tunknown temporary item\n");
			}
			break;
		case BTRFS_DEV_REPLACE_KEY:
			pr_info("\t\tdev replace\n");
			break;
		case BTRFS_UUID_KEY_SUBVOL:
		case BTRFS_UUID_KEY_RECEIVED_SUBVOL:
			print_uuid_item(l, btrfs_item_ptr_offset(l, i),
					btrfs_item_size(l, i));
			break;
		case BTRFS_RAID_STRIPE_KEY:
			print_raid_stripe_key(l, btrfs_item_size(l, i),
				btrfs_item_ptr(l, i, struct btrfs_stripe_extent));
			break;
		}
	}
}

void btrfs_print_tree(const struct extent_buffer *c, bool follow)
{
	struct btrfs_fs_info *fs_info;
	int i; u32 nr;
	struct btrfs_key key;
	int level;

	if (!c)
		return;
	fs_info = c->fs_info;
	nr = btrfs_header_nritems(c);
	level = btrfs_header_level(c);
	if (level == 0) {
		btrfs_print_leaf(c);
		return;
	}
	btrfs_info(fs_info,
		   "node %llu level %d gen %llu total ptrs %d free spc %u owner %llu",
		   btrfs_header_bytenr(c), level, btrfs_header_generation(c),
		   nr, (u32)BTRFS_NODEPTRS_PER_BLOCK(fs_info) - nr,
		   btrfs_header_owner(c));
	print_eb_refs_lock(c);
	for (i = 0; i < nr; i++) {
		btrfs_node_key_to_cpu(c, &key, i);
		pr_info("\tkey %d (%llu %u %llu) block %llu gen %llu\n",
		       i, key.objectid, key.type, key.offset,
		       btrfs_node_blockptr(c, i),
		       btrfs_node_ptr_generation(c, i));
	}
	if (!follow)
		return;
	for (i = 0; i < nr; i++) {
		struct btrfs_tree_parent_check check = {
			.level = level - 1,
			.transid = btrfs_node_ptr_generation(c, i),
			.owner_root = btrfs_header_owner(c),
			.has_first_key = true
		};
		struct extent_buffer *next;

		btrfs_node_key_to_cpu(c, &check.first_key, i);
		next = read_tree_block(fs_info, btrfs_node_blockptr(c, i), &check);
		if (IS_ERR(next))
			continue;
		if (!extent_buffer_uptodate(next)) {
			free_extent_buffer(next);
			continue;
		}

		if (btrfs_is_leaf(next) &&
		   level != 1)
			BUG();
		if (btrfs_header_level(next) !=
		       level - 1)
			BUG();
		btrfs_print_tree(next, follow);
		free_extent_buffer(next);
	}
}
