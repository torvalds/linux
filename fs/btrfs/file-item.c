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

#include <linux/bio.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
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
			     u64 disk_offset, u64 disk_num_bytes,
			     u64 num_bytes, u64 offset)
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
	btrfs_set_file_extent_disk_bytenr(leaf, item, disk_offset);
	btrfs_set_file_extent_disk_num_bytes(leaf, item, disk_num_bytes);
	btrfs_set_file_extent_offset(leaf, item, offset);
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

#if 0 /* broken */
int btrfs_lookup_bio_sums(struct btrfs_root *root, struct inode *inode,
			  struct bio *bio)
{
	u32 sum;
	struct bio_vec *bvec = bio->bi_io_vec;
	int bio_index = 0;
	u64 offset;
	u64 item_start_offset = 0;
	u64 item_last_offset = 0;
	u32 diff;
	int ret;
	struct btrfs_path *path;
	struct btrfs_csum_item *item = NULL;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;

	path = btrfs_alloc_path();

	WARN_ON(bio->bi_vcnt <= 0);

	while(bio_index < bio->bi_vcnt) {
		offset = page_offset(bvec->bv_page) + bvec->bv_offset;
		ret = btrfs_find_ordered_sum(inode, offset, &sum);
		if (ret == 0)
			goto found;

		if (!item || offset < item_start_offset ||
		    offset >= item_last_offset) {
			struct btrfs_key found_key;
			u32 item_size;

			if (item)
				btrfs_release_path(root, path);
			item = btrfs_lookup_csum(NULL, root, path,
						 inode->i_ino, offset, 0);
			if (IS_ERR(item)) {
				ret = PTR_ERR(item);
				if (ret == -ENOENT || ret == -EFBIG)
					ret = 0;
				sum = 0;
				printk("no csum found for inode %lu start "
				       "%llu\n", inode->i_ino,
				       (unsigned long long)offset);
				item = NULL;
				goto found;
			}
			btrfs_item_key_to_cpu(path->nodes[0], &found_key,
					      path->slots[0]);

			item_start_offset = found_key.offset;
			item_size = btrfs_item_size_nr(path->nodes[0],
						       path->slots[0]);
			item_last_offset = item_start_offset +
				(item_size / BTRFS_CRC32_SIZE) *
				root->sectorsize;
			item = btrfs_item_ptr(path->nodes[0], path->slots[0],
					      struct btrfs_csum_item);
		}
		/*
		 * this byte range must be able to fit inside
		 * a single leaf so it will also fit inside a u32
		 */
		diff = offset - item_start_offset;
		diff = diff / root->sectorsize;
		diff = diff * BTRFS_CRC32_SIZE;

		read_extent_buffer(path->nodes[0], &sum,
				   ((unsigned long)item) + diff,
				   BTRFS_CRC32_SIZE);
found:
		set_state_private(io_tree, offset, sum);
		bio_index++;
		bvec++;
	}
	btrfs_free_path(path);
	return 0;
}
#endif

int btrfs_csum_one_bio(struct btrfs_root *root, struct inode *inode,
		       struct bio *bio)
{
	struct btrfs_ordered_sum *sums;
	struct btrfs_sector_sum *sector_sum;
	struct btrfs_ordered_extent *ordered;
	char *data;
	struct bio_vec *bvec = bio->bi_io_vec;
	int bio_index = 0;
	unsigned long total_bytes = 0;
	unsigned long this_sum_bytes = 0;
	u64 offset;

	WARN_ON(bio->bi_vcnt <= 0);
	sums = kzalloc(btrfs_ordered_sum_size(root, bio->bi_size), GFP_NOFS);
	if (!sums)
		return -ENOMEM;

	sector_sum = sums->sums;
	sums->file_offset = page_offset(bvec->bv_page) + bvec->bv_offset;
	sums->len = bio->bi_size;
	INIT_LIST_HEAD(&sums->list);
	ordered = btrfs_lookup_ordered_extent(inode, sums->file_offset);
	BUG_ON(!ordered);

	while(bio_index < bio->bi_vcnt) {
		offset = page_offset(bvec->bv_page) + bvec->bv_offset;
		if (offset >= ordered->file_offset + ordered->len ||
		    offset < ordered->file_offset) {
			unsigned long bytes_left;
			sums->len = this_sum_bytes;
			this_sum_bytes = 0;
			btrfs_add_ordered_sum(inode, ordered, sums);
			btrfs_put_ordered_extent(ordered);

			bytes_left = bio->bi_size - total_bytes;

			sums = kzalloc(btrfs_ordered_sum_size(root, bytes_left),
				       GFP_NOFS);
			BUG_ON(!sums);
			sector_sum = sums->sums;
			sums->len = bytes_left;
			sums->file_offset = offset;
			ordered = btrfs_lookup_ordered_extent(inode,
						      sums->file_offset);
			BUG_ON(!ordered);
		}

		data = kmap_atomic(bvec->bv_page, KM_USER0);
		sector_sum->sum = ~(u32)0;
		sector_sum->sum = btrfs_csum_data(root,
						  data + bvec->bv_offset,
						  sector_sum->sum,
						  bvec->bv_len);
		kunmap_atomic(data, KM_USER0);
		btrfs_csum_final(sector_sum->sum,
				 (char *)&sector_sum->sum);
		sector_sum->offset = page_offset(bvec->bv_page) +
			bvec->bv_offset;

		sector_sum++;
		bio_index++;
		total_bytes += bvec->bv_len;
		this_sum_bytes += bvec->bv_len;
		bvec++;
	}
	this_sum_bytes = 0;
	btrfs_add_ordered_sum(inode, ordered, sums);
	btrfs_put_ordered_extent(ordered);
	return 0;
}

int btrfs_csum_file_blocks(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root, struct inode *inode,
			   struct btrfs_ordered_sum *sums)
{
	u64 objectid = inode->i_ino;
	u64 offset;
	int ret;
	struct btrfs_key file_key;
	struct btrfs_key found_key;
	u64 next_offset;
	u64 total_bytes = 0;
	int found_next;
	struct btrfs_path *path;
	struct btrfs_csum_item *item;
	struct btrfs_csum_item *item_end;
	struct extent_buffer *leaf = NULL;
	u64 csum_offset;
	struct btrfs_sector_sum *sector_sum;
	u32 nritems;
	u32 ins_size;
	char *eb_map;
	char *eb_token;
	unsigned long map_len;
	unsigned long map_start;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	sector_sum = sums->sums;
again:
	next_offset = (u64)-1;
	found_next = 0;
	offset = sector_sum->offset;
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
		int slot = path->slots[0] + 1;
		/* we didn't find a csum item, insert one */
		nritems = btrfs_header_nritems(path->nodes[0]);
		if (path->slots[0] >= nritems - 1) {
			ret = btrfs_next_leaf(root, path);
			if (ret == 1)
				found_next = 1;
			if (ret != 0)
				goto insert;
			slot = 0;
		}
		btrfs_item_key_to_cpu(path->nodes[0], &found_key, slot);
		if (found_key.objectid != objectid ||
		    found_key.type != BTRFS_CSUM_ITEM_KEY) {
			found_next = 1;
			goto insert;
		}
		next_offset = found_key.offset;
		found_next = 1;
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
	if (found_next) {
		u64 tmp = min((u64)i_size_read(inode), next_offset);
		tmp -= offset & ~((u64)root->sectorsize -1);
		tmp >>= root->fs_info->sb->s_blocksize_bits;
		tmp = max((u64)1, tmp);
		tmp = min(tmp, (u64)MAX_CSUM_ITEMS(root));
		ins_size = BTRFS_CRC32_SIZE * tmp;
	} else {
		ins_size = BTRFS_CRC32_SIZE;
	}
	ret = btrfs_insert_empty_item(trans, root, path, &file_key,
				      ins_size);
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
	item_end = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_csum_item);
	item_end = (struct btrfs_csum_item *)((unsigned char *)item_end +
				      btrfs_item_size_nr(leaf, path->slots[0]));
	eb_token = NULL;
next_sector:

	if (!eb_token ||
	   (unsigned long)item  + BTRFS_CRC32_SIZE >= map_start + map_len) {
		int err;

		if (eb_token)
			unmap_extent_buffer(leaf, eb_token, KM_USER1);
		eb_token = NULL;
		err = map_private_extent_buffer(leaf, (unsigned long)item,
						BTRFS_CRC32_SIZE,
						&eb_token, &eb_map,
						&map_start, &map_len, KM_USER1);
		if (err)
			eb_token = NULL;
	}
	if (eb_token) {
		memcpy(eb_token + ((unsigned long)item & (PAGE_CACHE_SIZE - 1)),
		       &sector_sum->sum, BTRFS_CRC32_SIZE);
	} else {
		write_extent_buffer(leaf, &sector_sum->sum,
				    (unsigned long)item, BTRFS_CRC32_SIZE);
	}

	total_bytes += root->sectorsize;
	sector_sum++;
	if (total_bytes < sums->len) {
		item = (struct btrfs_csum_item *)((char *)item +
						  BTRFS_CRC32_SIZE);
		if (item < item_end && offset + PAGE_CACHE_SIZE ==
		    sector_sum->offset) {
			    offset = sector_sum->offset;
			goto next_sector;
		}
	}
	if (eb_token) {
		unmap_extent_buffer(leaf, eb_token, KM_USER1);
		eb_token = NULL;
	}
	btrfs_mark_buffer_dirty(path->nodes[0]);
	if (total_bytes < sums->len) {
		btrfs_release_path(root, path);
		goto again;
	}
fail:
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
	ret = btrfs_truncate_item(trans, root, path, new_item_size, 1);
	BUG_ON(ret);
	return ret;
}
