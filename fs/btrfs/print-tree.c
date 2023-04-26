// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"

struct root_name_map {
	u64 id;
	char name[16];
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

static void print_chunk(struct extent_buffer *eb, struct btrfs_chunk *chunk)
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
static void print_dev_item(struct extent_buffer *eb,
			   struct btrfs_dev_item *dev_item)
{
	pr_info("\t\tdev item devid %llu total_bytes %llu bytes used %llu\n",
	       btrfs_device_id(eb, dev_item),
	       btrfs_device_total_bytes(eb, dev_item),
	       btrfs_device_bytes_used(eb, dev_item));
}
static void print_extent_data_ref(struct extent_buffer *eb,
				  struct btrfs_extent_data_ref *ref)
{
	pr_cont("extent data backref root %llu objectid %llu offset %llu count %u\n",
	       btrfs_extent_data_ref_root(eb, ref),
	       btrfs_extent_data_ref_objectid(eb, ref),
	       btrfs_extent_data_ref_offset(eb, ref),
	       btrfs_extent_data_ref_count(eb, ref));
}

static void print_extent_item(struct extent_buffer *eb, int slot, int type)
{
	struct btrfs_extent_item *ei;
	struct btrfs_extent_inline_ref *iref;
	struct btrfs_extent_data_ref *dref;
	struct btrfs_shared_data_ref *sref;
	struct btrfs_disk_key key;
	unsigned long end;
	unsigned long ptr;
	u32 item_size = btrfs_item_size(eb, slot);
	u64 flags;
	u64 offset;
	int ref_index = 0;

	if (unlikely(item_size < sizeof(*ei))) {
		btrfs_print_v0_err(eb->fs_info);
		btrfs_handle_fs_error(eb->fs_info, -EINVAL, NULL);
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
		default:
			pr_cont("(extent %llu has INVALID ref type %d)\n",
				  eb->start, type);
			return;
		}
		ptr += btrfs_extent_inline_ref_size(type);
	}
	WARN_ON(ptr > end);
}

static void print_uuid_item(struct extent_buffer *l, unsigned long offset,
			    u32 item_size)
{
	if (!IS_ALIGNED(item_size, sizeof(u64))) {
		pr_warn("BTRFS: uuid item with illegal size %lu!\n",
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

/*
 * Helper to output refs and locking status of extent buffer.  Useful to debug
 * race condition related problems.
 */
static void print_eb_refs_lock(struct extent_buffer *eb)
{
#ifdef CONFIG_BTRFS_DEBUG
	btrfs_info(eb->fs_info, "refs %u lock_owner %u current %u",
		   atomic_read(&eb->refs), eb->lock_owner, current->pid);
#endif
}

void btrfs_print_leaf(struct extent_buffer *l)
{
	struct btrfs_fs_info *fs_info;
	int i;
	u32 type, nr;
	struct btrfs_root_item *ri;
	struct btrfs_dir_item *di;
	struct btrfs_inode_item *ii;
	struct btrfs_block_group_item *bi;
	struct btrfs_file_extent_item *fi;
	struct btrfs_extent_data_ref *dref;
	struct btrfs_shared_data_ref *sref;
	struct btrfs_dev_extent *dev_extent;
	struct btrfs_key key;
	struct btrfs_key found_key;

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
		btrfs_item_key_to_cpu(l, &key, i);
		type = key.type;
		pr_info("\titem %d key (%llu %u %llu) itemoff %d itemsize %d\n",
			i, key.objectid, type, key.offset,
			btrfs_item_offset(l, i), btrfs_item_size(l, i));
		switch (type) {
		case BTRFS_INODE_ITEM_KEY:
			ii = btrfs_item_ptr(l, i, struct btrfs_inode_item);
			pr_info("\t\tinode generation %llu size %llu mode %o\n",
			       btrfs_inode_generation(l, ii),
			       btrfs_inode_size(l, ii),
			       btrfs_inode_mode(l, ii));
			break;
		case BTRFS_DIR_ITEM_KEY:
			di = btrfs_item_ptr(l, i, struct btrfs_dir_item);
			btrfs_dir_item_key_to_cpu(l, di, &found_key);
			pr_info("\t\tdir oid %llu type %u\n",
				found_key.objectid,
				btrfs_dir_type(l, di));
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
			fi = btrfs_item_ptr(l, i,
					    struct btrfs_file_extent_item);
			if (btrfs_file_extent_type(l, fi) ==
			    BTRFS_FILE_EXTENT_INLINE) {
				pr_info("\t\tinline extent data size %llu\n",
				       btrfs_file_extent_ram_bytes(l, fi));
				break;
			}
			pr_info("\t\textent data disk bytenr %llu nr %llu\n",
			       btrfs_file_extent_disk_bytenr(l, fi),
			       btrfs_file_extent_disk_num_bytes(l, fi));
			pr_info("\t\textent data offset %llu nr %llu ram %llu\n",
			       btrfs_file_extent_offset(l, fi),
			       btrfs_file_extent_num_bytes(l, fi),
			       btrfs_file_extent_ram_bytes(l, fi));
			break;
		case BTRFS_EXTENT_REF_V0_KEY:
			btrfs_print_v0_err(fs_info);
			btrfs_handle_fs_error(fs_info, -EINVAL, NULL);
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
		}
	}
}

void btrfs_print_tree(struct extent_buffer *c, bool follow)
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
		struct btrfs_key first_key;
		struct extent_buffer *next;

		btrfs_node_key_to_cpu(c, &first_key, i);
		next = read_tree_block(fs_info, btrfs_node_blockptr(c, i),
				       btrfs_header_owner(c),
				       btrfs_node_ptr_generation(c, i),
				       level - 1, &first_key);
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
