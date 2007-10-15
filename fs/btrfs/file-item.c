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
#include "transaction.h"
#include "print-tree.h"

#define MAX_CSUM_ITEMS(r) ((((BTRFS_LEAF_DATA_SIZE(r) - \
			       sizeof(struct btrfs_item) * 2) / \
			       BTRFS_CRC32_SIZE) - 1))
int btrfs_insert_file_extent(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       u64 objectid, u64 pos,
			       u64 offset, u64 disk_num_bytes,
			       u64 num_bytes)
{
	int ret = 0;
	struct btrfs_file_extent_item *item;
	struct btrfs_key file_key;
	struct btrfs_path *path;
	struct extent_buffer *leaf;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	file_key.objectid = objectid;
	file_key.offset = pos;
	btrfs_set_key_type(&file_key, BTRFS_EXTENT_DATA_KEY);

	ret = btrfs_insert_empty_item(trans, root, path, &file_key,
				      sizeof(*item));
	if (ret < 0)
		goto out;
	BUG_ON(ret);
	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0],
			      struct btrfs_file_extent_item);
	btrfs_set_file_extent_disk_bytenr(leaf, item, offset);
	btrfs_set_file_extent_disk_num_bytes(leaf, item, disk_num_bytes);
	btrfs_set_file_extent_offset(leaf, item, 0);
	btrfs_set_file_extent_num_bytes(leaf, item, num_bytes);
	btrfs_set_file_extent_generation(leaf, item, trans->transid);
	btrfs_set_file_extent_type(leaf, item, BTRFS_FILE_EXTENT_REG);
	btrfs_mark_buffer_dirty(leaf);
out:
	btrfs_free_path(path);
	return ret;
}

struct btrfs_csum_item *btrfs_lookup_csum(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path,
					  u64 objectid, u64 offset,
					  int cow)
{
	int ret;
	struct btrfs_key file_key;
	struct btrfs_key found_key;
	struct btrfs_csum_item *item;
	struct extent_buffer *leaf;
	u64 csum_offset = 0;
	int csums_in_item;

	file_key.objectid = objectid;
	file_key.offset = offset;
	btrfs_set_key_type(&file_key, BTRFS_CSUM_ITEM_KEY);
	ret = btrfs_search_slot(trans, root, &file_key, path, 0, cow);
	if (ret < 0)
		goto fail;
	leaf = path->nodes[0];
	if (ret > 0) {
		ret = 1;
		if (path->slots[0] == 0)
			goto fail;
		path->slots[0]--;
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (btrfs_key_type(&found_key) != BTRFS_CSUM_ITEM_KEY ||
		    found_key.objectid != objectid) {
			goto fail;
		}
		csum_offset = (offset - found_key.offset) >>
				root->fs_info->sb->s_blocksize_bits;
		csums_in_item = btrfs_item_size_nr(leaf, path->slots[0]);
		csums_in_item /= BTRFS_CRC32_SIZE;

		if (csum_offset >= csums_in_item) {
			ret = -EFBIG;
			goto fail;
		}
	}
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_csum_item);
	item = (struct btrfs_csum_item *)((unsigned char *)item +
					  csum_offset * BTRFS_CRC32_SIZE);
	return item;
fail:
	if (ret > 0)
		ret = -ENOENT;
	return ERR_PTR(ret);
}


int btrfs_lookup_file_extent(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct btrfs_path *path, u64 objectid,
			     u64 offset, int mod)
{
	int ret;
	struct btrfs_key file_key;
	int ins_len = mod < 0 ? -1 : 0;
	int cow = mod != 0;

	file_key.objectid = objectid;
	file_key.offset = offset;
	btrfs_set_key_type(&file_key, BTRFS_EXTENT_DATA_KEY);
	ret = btrfs_search_slot(trans, root, &file_key, path, ins_len, cow);
	return ret;
}

int btrfs_csum_file_block(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  u64 objectid, u64 offset,
			  char *data, size_t len)
{
	int ret;
	struct btrfs_key file_key;
	struct btrfs_key found_key;
	struct btrfs_path *path;
	struct btrfs_csum_item *item;
	struct extent_buffer *leaf = NULL;
	u64 csum_offset;
	u32 csum_result = ~(u32)0;

	path = btrfs_alloc_path();
	BUG_ON(!path);

	file_key.objectid = objectid;
	file_key.offset = offset;
	btrfs_set_key_type(&file_key, BTRFS_CSUM_ITEM_KEY);

	item = btrfs_lookup_csum(trans, root, path, objectid, offset, 1);
	if (!IS_ERR(item)) {
		leaf = path->nodes[0];
		goto found;
	}
	ret = PTR_ERR(item);
	if (ret == -EFBIG) {
		u32 item_size;
		/* we found one, but it isn't big enough yet */
		leaf = path->nodes[0];
		item_size = btrfs_item_size_nr(leaf, path->slots[0]);
		if ((item_size / BTRFS_CRC32_SIZE) >= MAX_CSUM_ITEMS(root)) {
			/* already at max size, make a new one */
			goto insert;
		}
	} else {
		/* we didn't find a csum item, insert one */
		goto insert;
	}

	/*
	 * at this point, we know the tree has an item, but it isn't big
	 * enough yet to put our csum in.  Grow it
	 */
	btrfs_release_path(root, path);
	ret = btrfs_search_slot(trans, root, &file_key, path,
				BTRFS_CRC32_SIZE, 1);
	if (ret < 0)
		goto fail;
	if (ret == 0) {
		BUG();
	}
	if (path->slots[0] == 0) {
		goto insert;
	}
	path->slots[0]--;
	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
	csum_offset = (offset - found_key.offset) >>
			root->fs_info->sb->s_blocksize_bits;
	if (btrfs_key_type(&found_key) != BTRFS_CSUM_ITEM_KEY ||
	    found_key.objectid != objectid ||
	    csum_offset >= MAX_CSUM_ITEMS(root)) {
		goto insert;
	}
	if (csum_offset >= btrfs_item_size_nr(leaf, path->slots[0]) /
	    BTRFS_CRC32_SIZE) {
		u32 diff = (csum_offset + 1) * BTRFS_CRC32_SIZE;
		diff = diff - btrfs_item_size_nr(leaf, path->slots[0]);
		if (diff != BTRFS_CRC32_SIZE)
			goto insert;
		ret = btrfs_extend_item(trans, root, path, diff);
		BUG_ON(ret);
		goto csum;
	}

insert:
	btrfs_release_path(root, path);
	csum_offset = 0;
	ret = btrfs_insert_empty_item(trans, root, path, &file_key,
				      BTRFS_CRC32_SIZE);
	if (ret < 0)
		goto fail;
	if (ret != 0) {
		WARN_ON(1);
		goto fail;
	}
csum:
	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_csum_item);
	ret = 0;
	item = (struct btrfs_csum_item *)((unsigned char *)item +
					  csum_offset * BTRFS_CRC32_SIZE);
found:
	csum_result = btrfs_csum_data(root, data, csum_result, len);
	btrfs_csum_final(csum_result, (char *)&csum_result);
	write_extent_buffer(leaf, &csum_result, (unsigned long)item,
			    BTRFS_CRC32_SIZE);
	btrfs_mark_buffer_dirty(path->nodes[0]);
fail:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	return ret;
}

int btrfs_csum_truncate(struct btrfs_trans_handle *trans,
			struct btrfs_root *root, struct btrfs_path *path,
			u64 isize)
{
	struct btrfs_key key;
	struct extent_buffer *leaf = path->nodes[0];
	int slot = path->slots[0];
	int ret;
	u32 new_item_size;
	u64 new_item_span;
	u64 blocks;

	btrfs_item_key_to_cpu(leaf, &key, slot);
	if (isize <= key.offset)
		return 0;
	new_item_span = isize - key.offset;
	blocks = (new_item_span + root->sectorsize - 1) >>
		root->fs_info->sb->s_blocksize_bits;
	new_item_size = blocks * BTRFS_CRC32_SIZE;
	if (new_item_size >= btrfs_item_size_nr(leaf, slot))
		return 0;
	ret = btrfs_truncate_item(trans, root, path, new_item_size);
	BUG_ON(ret);
	return ret;
}

