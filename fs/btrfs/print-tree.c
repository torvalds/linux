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

#include <linux/module.h>
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"

void btrfs_print_leaf(struct btrfs_root *root, struct btrfs_leaf *l)
{
	int i;
	u32 nr = btrfs_header_nritems(&l->header);
	struct btrfs_item *item;
	struct btrfs_extent_item *ei;
	struct btrfs_root_item *ri;
	struct btrfs_dir_item *di;
	struct btrfs_inode_item *ii;
	struct btrfs_block_group_item *bi;
	struct btrfs_file_extent_item *fi;
	u32 type;

	printk("leaf %llu total ptrs %d free space %d\n",
		(unsigned long long)btrfs_header_blocknr(&l->header), nr,
		btrfs_leaf_free_space(root, l));
	for (i = 0 ; i < nr ; i++) {
		item = l->items + i;
		type = btrfs_disk_key_type(&item->key);
		printk("\titem %d key (%llu %x %llu) itemoff %d itemsize %d\n",
			i,
			(unsigned long long)btrfs_disk_key_objectid(&item->key),
			btrfs_disk_key_flags(&item->key),
			(unsigned long long)btrfs_disk_key_offset(&item->key),
			btrfs_item_offset(item),
			btrfs_item_size(item));
		switch (type) {
		case BTRFS_INODE_ITEM_KEY:
			ii = btrfs_item_ptr(l, i, struct btrfs_inode_item);
			printk("\t\tinode generation %llu size %llu mode %o\n",
			       (unsigned long long)btrfs_inode_generation(ii),
			       (unsigned long long)btrfs_inode_size(ii),
			       btrfs_inode_mode(ii));
			break;
		case BTRFS_DIR_ITEM_KEY:
			di = btrfs_item_ptr(l, i, struct btrfs_dir_item);
			printk("\t\tdir oid %llu flags %u type %u\n",
				(unsigned long long)btrfs_disk_key_objectid(
							    &di->location),
				btrfs_dir_flags(di),
				btrfs_dir_type(di));
			printk("\t\tname %.*s\n",
			       btrfs_dir_name_len(di),(char *)(di + 1));
			break;
		case BTRFS_ROOT_ITEM_KEY:
			ri = btrfs_item_ptr(l, i, struct btrfs_root_item);
			printk("\t\troot data blocknr %llu refs %u\n",
				(unsigned long long)btrfs_root_blocknr(ri),
				btrfs_root_refs(ri));
			break;
		case BTRFS_EXTENT_ITEM_KEY:
			ei = btrfs_item_ptr(l, i, struct btrfs_extent_item);
			printk("\t\textent data refs %u\n",
				btrfs_extent_refs(ei));
			break;

		case BTRFS_EXTENT_DATA_KEY:
			fi = btrfs_item_ptr(l, i,
					    struct btrfs_file_extent_item);
			if (btrfs_file_extent_type(fi) ==
			    BTRFS_FILE_EXTENT_INLINE) {
				printk("\t\tinline extent data size %u\n",
			           btrfs_file_extent_inline_len(l->items + i));
				break;
			}
			printk("\t\textent data disk block %llu nr %llu\n",
			       (unsigned long long)btrfs_file_extent_disk_blocknr(fi),
			       (unsigned long long)btrfs_file_extent_disk_num_blocks(fi));
			printk("\t\textent data offset %llu nr %llu\n",
			  (unsigned long long)btrfs_file_extent_offset(fi),
			  (unsigned long long)btrfs_file_extent_num_blocks(fi));
			break;
		case BTRFS_BLOCK_GROUP_ITEM_KEY:
			bi = btrfs_item_ptr(l, i,
					    struct btrfs_block_group_item);
			printk("\t\tblock group used %llu\n",
			       (unsigned long long)btrfs_block_group_used(bi));
			break;
		case BTRFS_STRING_ITEM_KEY:
			printk("\t\titem data %.*s\n", btrfs_item_size(item),
				btrfs_leaf_data(l) + btrfs_item_offset(item));
			break;
		};
	}
}

void btrfs_print_tree(struct btrfs_root *root, struct buffer_head *t)
{
	int i;
	u32 nr;
	struct btrfs_node *c;

	if (!t)
		return;
	c = btrfs_buffer_node(t);
	nr = btrfs_header_nritems(&c->header);
	if (btrfs_is_leaf(c)) {
		btrfs_print_leaf(root, (struct btrfs_leaf *)c);
		return;
	}
	printk("node %llu level %d total ptrs %d free spc %u\n",
	       (unsigned long long)btrfs_header_blocknr(&c->header),
	       btrfs_header_level(&c->header), nr,
	       (u32)BTRFS_NODEPTRS_PER_BLOCK(root) - nr);
	for (i = 0; i < nr; i++) {
		printk("\tkey %d (%llu %u %llu) block %llu\n",
		       i,
		       (unsigned long long)c->ptrs[i].key.objectid,
		       c->ptrs[i].key.flags,
		       (unsigned long long)c->ptrs[i].key.offset,
		       (unsigned long long)btrfs_node_blockptr(c, i));
	}
	for (i = 0; i < nr; i++) {
		struct buffer_head *next_buf = read_tree_block(root,
						btrfs_node_blockptr(c, i));
		struct btrfs_node *next = btrfs_buffer_node(next_buf);
		if (btrfs_is_leaf(next) &&
		    btrfs_header_level(&c->header) != 1)
			BUG();
		if (btrfs_header_level(&next->header) !=
			btrfs_header_level(&c->header) - 1)
			BUG();
		btrfs_print_tree(root, next_buf);
		btrfs_block_release(root, next_buf);
	}
}

