/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"

void btrfs_print_leaf(struct btrfs_root *root, struct extent_buffer *l)
{
	int i;
	u32 nr = btrfs_header_nritems(l);
	struct btrfs_item *item;
	struct btrfs_extent_item *ei;
	struct btrfs_root_item *ri;
	struct btrfs_dir_item *di;
	struct btrfs_inode_item *ii;
	struct btrfs_block_group_item *bi;
	struct btrfs_file_extent_item *fi;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_extent_ref *ref;
	u32 type;

	printk("leaf %llu total ptrs %d free space %d\n",
		(unsigned long long)btrfs_header_bytenr(l), nr,
		btrfs_leaf_free_space(root, l));
	for (i = 0 ; i < nr ; i++) {
		item = btrfs_item_nr(l, i);
		btrfs_item_key_to_cpu(l, &key, i);
		type = btrfs_key_type(&key);
		printk("\titem %d key (%llu %x %llu) itemoff %d itemsize %d\n",
			i,
			(unsigned long long)key.objectid, type,
			(unsigned long long)key.offset,
			btrfs_item_offset(l, item), btrfs_item_size(l, item));
		switch (type) {
		case BTRFS_INODE_ITEM_KEY:
			ii = btrfs_item_ptr(l, i, struct btrfs_inode_item);
			printk("\t\tinode generation %llu size %llu mode %o\n",
		              (unsigned long long)btrfs_inode_generation(l, ii),
			      (unsigned long long)btrfs_inode_size(l, ii),
			       btrfs_inode_mode(l, ii));
			break;
		case BTRFS_DIR_ITEM_KEY:
			di = btrfs_item_ptr(l, i, struct btrfs_dir_item);
			btrfs_dir_item_key_to_cpu(l, di, &found_key);
			printk("\t\tdir oid %llu type %u\n",
				(unsigned long long)found_key.objectid,
				btrfs_dir_type(l, di));
			break;
		case BTRFS_ROOT_ITEM_KEY:
			ri = btrfs_item_ptr(l, i, struct btrfs_root_item);
			printk("\t\troot data bytenr %llu refs %u\n",
				(unsigned long long)btrfs_disk_root_bytenr(l, ri),
				btrfs_disk_root_refs(l, ri));
			break;
		case BTRFS_EXTENT_ITEM_KEY:
			ei = btrfs_item_ptr(l, i, struct btrfs_extent_item);
			printk("\t\textent data refs %u\n",
				btrfs_extent_refs(l, ei));
			break;
		case BTRFS_EXTENT_REF_KEY:
			ref = btrfs_item_ptr(l, i, struct btrfs_extent_ref);
			printk("\t\textent back ref root %llu gen %llu "
			       "owner %llu offset %llu\n",
			       (unsigned long long)btrfs_ref_root(l, ref),
			       (unsigned long long)btrfs_ref_generation(l, ref),
			       (unsigned long long)btrfs_ref_objectid(l, ref),
			       (unsigned long long)btrfs_ref_offset(l, ref));
			break;

		case BTRFS_EXTENT_DATA_KEY:
			fi = btrfs_item_ptr(l, i,
					    struct btrfs_file_extent_item);
			if (btrfs_file_extent_type(l, fi) ==
			    BTRFS_FILE_EXTENT_INLINE) {
				printk("\t\tinline extent data size %u\n",
			           btrfs_file_extent_inline_len(l, item));
				break;
			}
			printk("\t\textent data disk bytenr %llu nr %llu\n",
			       (unsigned long long)btrfs_file_extent_disk_bytenr(l, fi),
			       (unsigned long long)btrfs_file_extent_disk_num_bytes(l, fi));
			printk("\t\textent data offset %llu nr %llu\n",
			  (unsigned long long)btrfs_file_extent_offset(l, fi),
			  (unsigned long long)btrfs_file_extent_num_bytes(l, fi));
			break;
		case BTRFS_BLOCK_GROUP_ITEM_KEY:
			bi = btrfs_item_ptr(l, i,
					    struct btrfs_block_group_item);
			printk("\t\tblock group used %llu\n",
			       (unsigned long long)btrfs_disk_block_group_used(l, bi));
			break;
		};
	}
}

void btrfs_print_tree(struct btrfs_root *root, struct extent_buffer *c)
{
	int i;
	u32 nr;
	struct btrfs_key key;
	int level;

	if (!c)
		return;
	nr = btrfs_header_nritems(c);
	level = btrfs_header_level(c);
	if (level == 0) {
		btrfs_print_leaf(root, c);
		return;
	}
	printk("node %llu level %d total ptrs %d free spc %u\n",
	       (unsigned long long)btrfs_header_bytenr(c),
	       btrfs_header_level(c), nr,
	       (u32)BTRFS_NODEPTRS_PER_BLOCK(root) - nr);
	for (i = 0; i < nr; i++) {
		btrfs_node_key_to_cpu(c, &key, i);
		printk("\tkey %d (%llu %u %llu) block %llu\n",
		       i,
		       (unsigned long long)key.objectid,
		       key.type,
		       (unsigned long long)key.offset,
		       (unsigned long long)btrfs_node_blockptr(c, i));
	}
	for (i = 0; i < nr; i++) {
		struct extent_buffer *next = read_tree_block(root,
					btrfs_node_blockptr(c, i),
					btrfs_level_size(root, level - 1));
		if (btrfs_is_leaf(next) &&
		    btrfs_header_level(c) != 1)
			BUG();
		if (btrfs_header_level(next) !=
			btrfs_header_level(c) - 1)
			BUG();
		btrfs_print_tree(root, next);
		free_extent_buffer(next);
	}
}

