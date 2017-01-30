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
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "volumes.h"
#include "print-tree.h"
#include "compression.h"

#define __MAX_CSUM_ITEMS(r, size) ((unsigned long)(((BTRFS_LEAF_DATA_SIZE(r) - \
				   sizeof(struct btrfs_item) * 2) / \
				  size) - 1))

#define MAX_CSUM_ITEMS(r, size) (min_t(u32, __MAX_CSUM_ITEMS(r, size), \
				       PAGE_SIZE))

#define MAX_ORDERED_SUM_BYTES(fs_info) ((PAGE_SIZE - \
				   sizeof(struct btrfs_ordered_sum)) / \
				   sizeof(u32) * (fs_info)->sectorsize)

int btrfs_insert_file_extent(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     u64 objectid, u64 pos,
			     u64 disk_offset, u64 disk_num_bytes,
			     u64 num_bytes, u64 offset, u64 ram_bytes,
			     u8 compression, u8 encryption, u16 other_encoding)
{
	int ret = 0;
	struct btrfs_file_extent_item *item;
	struct btrfs_key file_key;
	struct btrfs_path *path;
	struct extent_buffer *leaf;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	file_key.objectid = objectid;
	file_key.offset = pos;
	file_key.type = BTRFS_EXTENT_DATA_KEY;

	path->leave_spinning = 1;
	ret = btrfs_insert_empty_item(trans, root, path, &file_key,
				      sizeof(*item));
	if (ret < 0)
		goto out;
	BUG_ON(ret); /* Can't happen */
	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0],
			      struct btrfs_file_extent_item);
	btrfs_set_file_extent_disk_bytenr(leaf, item, disk_offset);
	btrfs_set_file_extent_disk_num_bytes(leaf, item, disk_num_bytes);
	btrfs_set_file_extent_offset(leaf, item, offset);
	btrfs_set_file_extent_num_bytes(leaf, item, num_bytes);
	btrfs_set_file_extent_ram_bytes(leaf, item, ram_bytes);
	btrfs_set_file_extent_generation(leaf, item, trans->transid);
	btrfs_set_file_extent_type(leaf, item, BTRFS_FILE_EXTENT_REG);
	btrfs_set_file_extent_compression(leaf, item, compression);
	btrfs_set_file_extent_encryption(leaf, item, encryption);
	btrfs_set_file_extent_other_encoding(leaf, item, other_encoding);

	btrfs_mark_buffer_dirty(leaf);
out:
	btrfs_free_path(path);
	return ret;
}

static struct btrfs_csum_item *
btrfs_lookup_csum(struct btrfs_trans_handle *trans,
		  struct btrfs_root *root,
		  struct btrfs_path *path,
		  u64 bytenr, int cow)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret;
	struct btrfs_key file_key;
	struct btrfs_key found_key;
	struct btrfs_csum_item *item;
	struct extent_buffer *leaf;
	u64 csum_offset = 0;
	u16 csum_size = btrfs_super_csum_size(fs_info->super_copy);
	int csums_in_item;

	file_key.objectid = BTRFS_EXTENT_CSUM_OBJECTID;
	file_key.offset = bytenr;
	file_key.type = BTRFS_EXTENT_CSUM_KEY;
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
		if (found_key.type != BTRFS_EXTENT_CSUM_KEY)
			goto fail;

		csum_offset = (bytenr - found_key.offset) >>
				fs_info->sb->s_blocksize_bits;
		csums_in_item = btrfs_item_size_nr(leaf, path->slots[0]);
		csums_in_item /= csum_size;

		if (csum_offset == csums_in_item) {
			ret = -EFBIG;
			goto fail;
		} else if (csum_offset > csums_in_item) {
			goto fail;
		}
	}
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_csum_item);
	item = (struct btrfs_csum_item *)((unsigned char *)item +
					  csum_offset * csum_size);
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
	file_key.type = BTRFS_EXTENT_DATA_KEY;
	ret = btrfs_search_slot(trans, root, &file_key, path, ins_len, cow);
	return ret;
}

static void btrfs_io_bio_endio_readpage(struct btrfs_io_bio *bio, int err)
{
	kfree(bio->csum_allocated);
}

static int __btrfs_lookup_bio_sums(struct inode *inode, struct bio *bio,
				   u64 logical_offset, u32 *dst, int dio)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct bio_vec *bvec;
	struct btrfs_io_bio *btrfs_bio = btrfs_io_bio(bio);
	struct btrfs_csum_item *item = NULL;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct btrfs_path *path;
	u8 *csum;
	u64 offset = 0;
	u64 item_start_offset = 0;
	u64 item_last_offset = 0;
	u64 disk_bytenr;
	u64 page_bytes_left;
	u32 diff;
	int nblocks;
	int count = 0, i;
	u16 csum_size = btrfs_super_csum_size(fs_info->super_copy);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	nblocks = bio->bi_iter.bi_size >> inode->i_sb->s_blocksize_bits;
	if (!dst) {
		if (nblocks * csum_size > BTRFS_BIO_INLINE_CSUM_SIZE) {
			btrfs_bio->csum_allocated = kmalloc_array(nblocks,
					csum_size, GFP_NOFS);
			if (!btrfs_bio->csum_allocated) {
				btrfs_free_path(path);
				return -ENOMEM;
			}
			btrfs_bio->csum = btrfs_bio->csum_allocated;
			btrfs_bio->end_io = btrfs_io_bio_endio_readpage;
		} else {
			btrfs_bio->csum = btrfs_bio->csum_inline;
		}
		csum = btrfs_bio->csum;
	} else {
		csum = (u8 *)dst;
	}

	if (bio->bi_iter.bi_size > PAGE_SIZE * 8)
		path->reada = READA_FORWARD;

	WARN_ON(bio->bi_vcnt <= 0);

	/*
	 * the free space stuff is only read when it hasn't been
	 * updated in the current transaction.  So, we can safely
	 * read from the commit root and sidestep a nasty deadlock
	 * between reading the free space cache and updating the csum tree.
	 */
	if (btrfs_is_free_space_inode(inode)) {
		path->search_commit_root = 1;
		path->skip_locking = 1;
	}

	disk_bytenr = (u64)bio->bi_iter.bi_sector << 9;
	if (dio)
		offset = logical_offset;

	bio_for_each_segment_all(bvec, bio, i) {
		page_bytes_left = bvec->bv_len;
		if (count)
			goto next;

		if (!dio)
			offset = page_offset(bvec->bv_page) + bvec->bv_offset;
		count = btrfs_find_ordered_sum(inode, offset, disk_bytenr,
					       (u32 *)csum, nblocks);
		if (count)
			goto found;

		if (!item || disk_bytenr < item_start_offset ||
		    disk_bytenr >= item_last_offset) {
			struct btrfs_key found_key;
			u32 item_size;

			if (item)
				btrfs_release_path(path);
			item = btrfs_lookup_csum(NULL, fs_info->csum_root,
						 path, disk_bytenr, 0);
			if (IS_ERR(item)) {
				count = 1;
				memset(csum, 0, csum_size);
				if (BTRFS_I(inode)->root->root_key.objectid ==
				    BTRFS_DATA_RELOC_TREE_OBJECTID) {
					set_extent_bits(io_tree, offset,
						offset + fs_info->sectorsize - 1,
						EXTENT_NODATASUM);
				} else {
					btrfs_info_rl(fs_info,
						   "no csum found for inode %llu start %llu",
					       btrfs_ino(inode), offset);
				}
				item = NULL;
				btrfs_release_path(path);
				goto found;
			}
			btrfs_item_key_to_cpu(path->nodes[0], &found_key,
					      path->slots[0]);

			item_start_offset = found_key.offset;
			item_size = btrfs_item_size_nr(path->nodes[0],
						       path->slots[0]);
			item_last_offset = item_start_offset +
				(item_size / csum_size) *
				fs_info->sectorsize;
			item = btrfs_item_ptr(path->nodes[0], path->slots[0],
					      struct btrfs_csum_item);
		}
		/*
		 * this byte range must be able to fit inside
		 * a single leaf so it will also fit inside a u32
		 */
		diff = disk_bytenr - item_start_offset;
		diff = diff / fs_info->sectorsize;
		diff = diff * csum_size;
		count = min_t(int, nblocks, (item_last_offset - disk_bytenr) >>
					    inode->i_sb->s_blocksize_bits);
		read_extent_buffer(path->nodes[0], csum,
				   ((unsigned long)item) + diff,
				   csum_size * count);
found:
		csum += count * csum_size;
		nblocks -= count;
next:
		while (count--) {
			disk_bytenr += fs_info->sectorsize;
			offset += fs_info->sectorsize;
			page_bytes_left -= fs_info->sectorsize;
			if (!page_bytes_left)
				break; /* move to next bio */
		}
	}

	WARN_ON_ONCE(count);
	btrfs_free_path(path);
	return 0;
}

int btrfs_lookup_bio_sums(struct inode *inode, struct bio *bio, u32 *dst)
{
	return __btrfs_lookup_bio_sums(inode, bio, 0, dst, 0);
}

int btrfs_lookup_bio_sums_dio(struct inode *inode, struct bio *bio, u64 offset)
{
	return __btrfs_lookup_bio_sums(inode, bio, offset, NULL, 1);
}

int btrfs_lookup_csums_range(struct btrfs_root *root, u64 start, u64 end,
			     struct list_head *list, int search_commit)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_key key;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_ordered_sum *sums;
	struct btrfs_csum_item *item;
	LIST_HEAD(tmplist);
	unsigned long offset;
	int ret;
	size_t size;
	u64 csum_end;
	u16 csum_size = btrfs_super_csum_size(fs_info->super_copy);

	ASSERT(IS_ALIGNED(start, fs_info->sectorsize) &&
	       IS_ALIGNED(end + 1, fs_info->sectorsize));

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	if (search_commit) {
		path->skip_locking = 1;
		path->reada = READA_FORWARD;
		path->search_commit_root = 1;
	}

	key.objectid = BTRFS_EXTENT_CSUM_OBJECTID;
	key.offset = start;
	key.type = BTRFS_EXTENT_CSUM_KEY;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto fail;
	if (ret > 0 && path->slots[0] > 0) {
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &key, path->slots[0] - 1);
		if (key.objectid == BTRFS_EXTENT_CSUM_OBJECTID &&
		    key.type == BTRFS_EXTENT_CSUM_KEY) {
			offset = (start - key.offset) >>
				 fs_info->sb->s_blocksize_bits;
			if (offset * csum_size <
			    btrfs_item_size_nr(leaf, path->slots[0] - 1))
				path->slots[0]--;
		}
	}

	while (start <= end) {
		leaf = path->nodes[0];
		if (path->slots[0] >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto fail;
			if (ret > 0)
				break;
			leaf = path->nodes[0];
		}

		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		if (key.objectid != BTRFS_EXTENT_CSUM_OBJECTID ||
		    key.type != BTRFS_EXTENT_CSUM_KEY ||
		    key.offset > end)
			break;

		if (key.offset > start)
			start = key.offset;

		size = btrfs_item_size_nr(leaf, path->slots[0]);
		csum_end = key.offset + (size / csum_size) * fs_info->sectorsize;
		if (csum_end <= start) {
			path->slots[0]++;
			continue;
		}

		csum_end = min(csum_end, end + 1);
		item = btrfs_item_ptr(path->nodes[0], path->slots[0],
				      struct btrfs_csum_item);
		while (start < csum_end) {
			size = min_t(size_t, csum_end - start,
				     MAX_ORDERED_SUM_BYTES(fs_info));
			sums = kzalloc(btrfs_ordered_sum_size(fs_info, size),
				       GFP_NOFS);
			if (!sums) {
				ret = -ENOMEM;
				goto fail;
			}

			sums->bytenr = start;
			sums->len = (int)size;

			offset = (start - key.offset) >>
				fs_info->sb->s_blocksize_bits;
			offset *= csum_size;
			size >>= fs_info->sb->s_blocksize_bits;

			read_extent_buffer(path->nodes[0],
					   sums->sums,
					   ((unsigned long)item) + offset,
					   csum_size * size);

			start += fs_info->sectorsize * size;
			list_add_tail(&sums->list, &tmplist);
		}
		path->slots[0]++;
	}
	ret = 0;
fail:
	while (ret < 0 && !list_empty(&tmplist)) {
		sums = list_entry(tmplist.next, struct btrfs_ordered_sum, list);
		list_del(&sums->list);
		kfree(sums);
	}
	list_splice_tail(&tmplist, list);

	btrfs_free_path(path);
	return ret;
}

int btrfs_csum_one_bio(struct inode *inode, struct bio *bio,
		       u64 file_start, int contig)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_ordered_sum *sums;
	struct btrfs_ordered_extent *ordered = NULL;
	char *data;
	struct bio_vec *bvec;
	int index;
	int nr_sectors;
	int i, j;
	unsigned long total_bytes = 0;
	unsigned long this_sum_bytes = 0;
	u64 offset;

	WARN_ON(bio->bi_vcnt <= 0);
	sums = kzalloc(btrfs_ordered_sum_size(fs_info, bio->bi_iter.bi_size),
		       GFP_NOFS);
	if (!sums)
		return -ENOMEM;

	sums->len = bio->bi_iter.bi_size;
	INIT_LIST_HEAD(&sums->list);

	if (contig)
		offset = file_start;
	else
		offset = 0; /* shut up gcc */

	sums->bytenr = (u64)bio->bi_iter.bi_sector << 9;
	index = 0;

	bio_for_each_segment_all(bvec, bio, j) {
		if (!contig)
			offset = page_offset(bvec->bv_page) + bvec->bv_offset;

		if (!ordered) {
			ordered = btrfs_lookup_ordered_extent(inode, offset);
			BUG_ON(!ordered); /* Logic error */
		}

		data = kmap_atomic(bvec->bv_page);

		nr_sectors = BTRFS_BYTES_TO_BLKS(fs_info,
						 bvec->bv_len + fs_info->sectorsize
						 - 1);

		for (i = 0; i < nr_sectors; i++) {
			if (offset >= ordered->file_offset + ordered->len ||
				offset < ordered->file_offset) {
				unsigned long bytes_left;

				kunmap_atomic(data);
				sums->len = this_sum_bytes;
				this_sum_bytes = 0;
				btrfs_add_ordered_sum(inode, ordered, sums);
				btrfs_put_ordered_extent(ordered);

				bytes_left = bio->bi_iter.bi_size - total_bytes;

				sums = kzalloc(btrfs_ordered_sum_size(fs_info, bytes_left),
					       GFP_NOFS);
				BUG_ON(!sums); /* -ENOMEM */
				sums->len = bytes_left;
				ordered = btrfs_lookup_ordered_extent(inode,
								offset);
				ASSERT(ordered); /* Logic error */
				sums->bytenr = ((u64)bio->bi_iter.bi_sector << 9)
					+ total_bytes;
				index = 0;

				data = kmap_atomic(bvec->bv_page);
			}

			sums->sums[index] = ~(u32)0;
			sums->sums[index]
				= btrfs_csum_data(data + bvec->bv_offset
						+ (i * fs_info->sectorsize),
						sums->sums[index],
						fs_info->sectorsize);
			btrfs_csum_final(sums->sums[index],
					(char *)(sums->sums + index));
			index++;
			offset += fs_info->sectorsize;
			this_sum_bytes += fs_info->sectorsize;
			total_bytes += fs_info->sectorsize;
		}

		kunmap_atomic(data);
	}
	this_sum_bytes = 0;
	btrfs_add_ordered_sum(inode, ordered, sums);
	btrfs_put_ordered_extent(ordered);
	return 0;
}

/*
 * helper function for csum removal, this expects the
 * key to describe the csum pointed to by the path, and it expects
 * the csum to overlap the range [bytenr, len]
 *
 * The csum should not be entirely contained in the range and the
 * range should not be entirely contained in the csum.
 *
 * This calls btrfs_truncate_item with the correct args based on the
 * overlap, and fixes up the key as required.
 */
static noinline void truncate_one_csum(struct btrfs_fs_info *fs_info,
				       struct btrfs_path *path,
				       struct btrfs_key *key,
				       u64 bytenr, u64 len)
{
	struct extent_buffer *leaf;
	u16 csum_size = btrfs_super_csum_size(fs_info->super_copy);
	u64 csum_end;
	u64 end_byte = bytenr + len;
	u32 blocksize_bits = fs_info->sb->s_blocksize_bits;

	leaf = path->nodes[0];
	csum_end = btrfs_item_size_nr(leaf, path->slots[0]) / csum_size;
	csum_end <<= fs_info->sb->s_blocksize_bits;
	csum_end += key->offset;

	if (key->offset < bytenr && csum_end <= end_byte) {
		/*
		 *         [ bytenr - len ]
		 *         [   ]
		 *   [csum     ]
		 *   A simple truncate off the end of the item
		 */
		u32 new_size = (bytenr - key->offset) >> blocksize_bits;
		new_size *= csum_size;
		btrfs_truncate_item(fs_info, path, new_size, 1);
	} else if (key->offset >= bytenr && csum_end > end_byte &&
		   end_byte > key->offset) {
		/*
		 *         [ bytenr - len ]
		 *                 [ ]
		 *                 [csum     ]
		 * we need to truncate from the beginning of the csum
		 */
		u32 new_size = (csum_end - end_byte) >> blocksize_bits;
		new_size *= csum_size;

		btrfs_truncate_item(fs_info, path, new_size, 0);

		key->offset = end_byte;
		btrfs_set_item_key_safe(fs_info, path, key);
	} else {
		BUG();
	}
}

/*
 * deletes the csum items from the csum tree for a given
 * range of bytes.
 */
int btrfs_del_csums(struct btrfs_trans_handle *trans,
		    struct btrfs_fs_info *fs_info, u64 bytenr, u64 len)
{
	struct btrfs_root *root = fs_info->csum_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	u64 end_byte = bytenr + len;
	u64 csum_end;
	struct extent_buffer *leaf;
	int ret;
	u16 csum_size = btrfs_super_csum_size(fs_info->super_copy);
	int blocksize_bits = fs_info->sb->s_blocksize_bits;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while (1) {
		key.objectid = BTRFS_EXTENT_CSUM_OBJECTID;
		key.offset = end_byte - 1;
		key.type = BTRFS_EXTENT_CSUM_KEY;

		path->leave_spinning = 1;
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret > 0) {
			if (path->slots[0] == 0)
				break;
			path->slots[0]--;
		} else if (ret < 0) {
			break;
		}

		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);

		if (key.objectid != BTRFS_EXTENT_CSUM_OBJECTID ||
		    key.type != BTRFS_EXTENT_CSUM_KEY) {
			break;
		}

		if (key.offset >= end_byte)
			break;

		csum_end = btrfs_item_size_nr(leaf, path->slots[0]) / csum_size;
		csum_end <<= blocksize_bits;
		csum_end += key.offset;

		/* this csum ends before we start, we're done */
		if (csum_end <= bytenr)
			break;

		/* delete the entire item, it is inside our range */
		if (key.offset >= bytenr && csum_end <= end_byte) {
			ret = btrfs_del_item(trans, root, path);
			if (ret)
				goto out;
			if (key.offset == bytenr)
				break;
		} else if (key.offset < bytenr && csum_end > end_byte) {
			unsigned long offset;
			unsigned long shift_len;
			unsigned long item_offset;
			/*
			 *        [ bytenr - len ]
			 *     [csum                ]
			 *
			 * Our bytes are in the middle of the csum,
			 * we need to split this item and insert a new one.
			 *
			 * But we can't drop the path because the
			 * csum could change, get removed, extended etc.
			 *
			 * The trick here is the max size of a csum item leaves
			 * enough room in the tree block for a single
			 * item header.  So, we split the item in place,
			 * adding a new header pointing to the existing
			 * bytes.  Then we loop around again and we have
			 * a nicely formed csum item that we can neatly
			 * truncate.
			 */
			offset = (bytenr - key.offset) >> blocksize_bits;
			offset *= csum_size;

			shift_len = (len >> blocksize_bits) * csum_size;

			item_offset = btrfs_item_ptr_offset(leaf,
							    path->slots[0]);

			memzero_extent_buffer(leaf, item_offset + offset,
					     shift_len);
			key.offset = bytenr;

			/*
			 * btrfs_split_item returns -EAGAIN when the
			 * item changed size or key
			 */
			ret = btrfs_split_item(trans, root, path, &key, offset);
			if (ret && ret != -EAGAIN) {
				btrfs_abort_transaction(trans, ret);
				goto out;
			}

			key.offset = end_byte - 1;
		} else {
			truncate_one_csum(fs_info, path, &key, bytenr, len);
			if (key.offset < bytenr)
				break;
		}
		btrfs_release_path(path);
	}
	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_csum_file_blocks(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   struct btrfs_ordered_sum *sums)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_key file_key;
	struct btrfs_key found_key;
	struct btrfs_path *path;
	struct btrfs_csum_item *item;
	struct btrfs_csum_item *item_end;
	struct extent_buffer *leaf = NULL;
	u64 next_offset;
	u64 total_bytes = 0;
	u64 csum_offset;
	u64 bytenr;
	u32 nritems;
	u32 ins_size;
	int index = 0;
	int found_next;
	int ret;
	u16 csum_size = btrfs_super_csum_size(fs_info->super_copy);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
again:
	next_offset = (u64)-1;
	found_next = 0;
	bytenr = sums->bytenr + total_bytes;
	file_key.objectid = BTRFS_EXTENT_CSUM_OBJECTID;
	file_key.offset = bytenr;
	file_key.type = BTRFS_EXTENT_CSUM_KEY;

	item = btrfs_lookup_csum(trans, root, path, bytenr, 1);
	if (!IS_ERR(item)) {
		ret = 0;
		leaf = path->nodes[0];
		item_end = btrfs_item_ptr(leaf, path->slots[0],
					  struct btrfs_csum_item);
		item_end = (struct btrfs_csum_item *)((char *)item_end +
			   btrfs_item_size_nr(leaf, path->slots[0]));
		goto found;
	}
	ret = PTR_ERR(item);
	if (ret != -EFBIG && ret != -ENOENT)
		goto fail_unlock;

	if (ret == -EFBIG) {
		u32 item_size;
		/* we found one, but it isn't big enough yet */
		leaf = path->nodes[0];
		item_size = btrfs_item_size_nr(leaf, path->slots[0]);
		if ((item_size / csum_size) >=
		    MAX_CSUM_ITEMS(fs_info, csum_size)) {
			/* already at max size, make a new one */
			goto insert;
		}
	} else {
		int slot = path->slots[0] + 1;
		/* we didn't find a csum item, insert one */
		nritems = btrfs_header_nritems(path->nodes[0]);
		if (!nritems || (path->slots[0] >= nritems - 1)) {
			ret = btrfs_next_leaf(root, path);
			if (ret == 1)
				found_next = 1;
			if (ret != 0)
				goto insert;
			slot = path->slots[0];
		}
		btrfs_item_key_to_cpu(path->nodes[0], &found_key, slot);
		if (found_key.objectid != BTRFS_EXTENT_CSUM_OBJECTID ||
		    found_key.type != BTRFS_EXTENT_CSUM_KEY) {
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
	btrfs_release_path(path);
	ret = btrfs_search_slot(trans, root, &file_key, path,
				csum_size, 1);
	if (ret < 0)
		goto fail_unlock;

	if (ret > 0) {
		if (path->slots[0] == 0)
			goto insert;
		path->slots[0]--;
	}

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
	csum_offset = (bytenr - found_key.offset) >>
			fs_info->sb->s_blocksize_bits;

	if (found_key.type != BTRFS_EXTENT_CSUM_KEY ||
	    found_key.objectid != BTRFS_EXTENT_CSUM_OBJECTID ||
	    csum_offset >= MAX_CSUM_ITEMS(fs_info, csum_size)) {
		goto insert;
	}

	if (csum_offset == btrfs_item_size_nr(leaf, path->slots[0]) /
	    csum_size) {
		int extend_nr;
		u64 tmp;
		u32 diff;
		u32 free_space;

		if (btrfs_leaf_free_space(fs_info, leaf) <
				 sizeof(struct btrfs_item) + csum_size * 2)
			goto insert;

		free_space = btrfs_leaf_free_space(fs_info, leaf) -
					 sizeof(struct btrfs_item) - csum_size;
		tmp = sums->len - total_bytes;
		tmp >>= fs_info->sb->s_blocksize_bits;
		WARN_ON(tmp < 1);

		extend_nr = max_t(int, 1, (int)tmp);
		diff = (csum_offset + extend_nr) * csum_size;
		diff = min(diff,
			   MAX_CSUM_ITEMS(fs_info, csum_size) * csum_size);

		diff = diff - btrfs_item_size_nr(leaf, path->slots[0]);
		diff = min(free_space, diff);
		diff /= csum_size;
		diff *= csum_size;

		btrfs_extend_item(fs_info, path, diff);
		ret = 0;
		goto csum;
	}

insert:
	btrfs_release_path(path);
	csum_offset = 0;
	if (found_next) {
		u64 tmp;

		tmp = sums->len - total_bytes;
		tmp >>= fs_info->sb->s_blocksize_bits;
		tmp = min(tmp, (next_offset - file_key.offset) >>
					 fs_info->sb->s_blocksize_bits);

		tmp = max((u64)1, tmp);
		tmp = min(tmp, (u64)MAX_CSUM_ITEMS(fs_info, csum_size));
		ins_size = csum_size * tmp;
	} else {
		ins_size = csum_size;
	}
	path->leave_spinning = 1;
	ret = btrfs_insert_empty_item(trans, root, path, &file_key,
				      ins_size);
	path->leave_spinning = 0;
	if (ret < 0)
		goto fail_unlock;
	if (WARN_ON(ret != 0))
		goto fail_unlock;
	leaf = path->nodes[0];
csum:
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_csum_item);
	item_end = (struct btrfs_csum_item *)((unsigned char *)item +
				      btrfs_item_size_nr(leaf, path->slots[0]));
	item = (struct btrfs_csum_item *)((unsigned char *)item +
					  csum_offset * csum_size);
found:
	ins_size = (u32)(sums->len - total_bytes) >>
		   fs_info->sb->s_blocksize_bits;
	ins_size *= csum_size;
	ins_size = min_t(u32, (unsigned long)item_end - (unsigned long)item,
			      ins_size);
	write_extent_buffer(leaf, sums->sums + index, (unsigned long)item,
			    ins_size);

	ins_size /= csum_size;
	total_bytes += ins_size * fs_info->sectorsize;
	index += ins_size;

	btrfs_mark_buffer_dirty(path->nodes[0]);
	if (total_bytes < sums->len) {
		btrfs_release_path(path);
		cond_resched();
		goto again;
	}
out:
	btrfs_free_path(path);
	return ret;

fail_unlock:
	goto out;
}

void btrfs_extent_item_to_extent_map(struct inode *inode,
				     const struct btrfs_path *path,
				     struct btrfs_file_extent_item *fi,
				     const bool new_inline,
				     struct extent_map *em)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_buffer *leaf = path->nodes[0];
	const int slot = path->slots[0];
	struct btrfs_key key;
	u64 extent_start, extent_end;
	u64 bytenr;
	u8 type = btrfs_file_extent_type(leaf, fi);
	int compress_type = btrfs_file_extent_compression(leaf, fi);

	em->bdev = fs_info->fs_devices->latest_bdev;
	btrfs_item_key_to_cpu(leaf, &key, slot);
	extent_start = key.offset;

	if (type == BTRFS_FILE_EXTENT_REG ||
	    type == BTRFS_FILE_EXTENT_PREALLOC) {
		extent_end = extent_start +
			btrfs_file_extent_num_bytes(leaf, fi);
	} else if (type == BTRFS_FILE_EXTENT_INLINE) {
		size_t size;
		size = btrfs_file_extent_inline_len(leaf, slot, fi);
		extent_end = ALIGN(extent_start + size,
				   fs_info->sectorsize);
	}

	em->ram_bytes = btrfs_file_extent_ram_bytes(leaf, fi);
	if (type == BTRFS_FILE_EXTENT_REG ||
	    type == BTRFS_FILE_EXTENT_PREALLOC) {
		em->start = extent_start;
		em->len = extent_end - extent_start;
		em->orig_start = extent_start -
			btrfs_file_extent_offset(leaf, fi);
		em->orig_block_len = btrfs_file_extent_disk_num_bytes(leaf, fi);
		bytenr = btrfs_file_extent_disk_bytenr(leaf, fi);
		if (bytenr == 0) {
			em->block_start = EXTENT_MAP_HOLE;
			return;
		}
		if (compress_type != BTRFS_COMPRESS_NONE) {
			set_bit(EXTENT_FLAG_COMPRESSED, &em->flags);
			em->compress_type = compress_type;
			em->block_start = bytenr;
			em->block_len = em->orig_block_len;
		} else {
			bytenr += btrfs_file_extent_offset(leaf, fi);
			em->block_start = bytenr;
			em->block_len = em->len;
			if (type == BTRFS_FILE_EXTENT_PREALLOC)
				set_bit(EXTENT_FLAG_PREALLOC, &em->flags);
		}
	} else if (type == BTRFS_FILE_EXTENT_INLINE) {
		em->block_start = EXTENT_MAP_INLINE;
		em->start = extent_start;
		em->len = extent_end - extent_start;
		/*
		 * Initialize orig_start and block_len with the same values
		 * as in inode.c:btrfs_get_extent().
		 */
		em->orig_start = EXTENT_MAP_HOLE;
		em->block_len = (u64)-1;
		if (!new_inline && compress_type != BTRFS_COMPRESS_NONE) {
			set_bit(EXTENT_FLAG_COMPRESSED, &em->flags);
			em->compress_type = compress_type;
		}
	} else {
		btrfs_err(fs_info,
			  "unknown file extent item type %d, inode %llu, offset %llu, root %llu",
			  type, btrfs_ino(inode), extent_start,
			  root->root_key.objectid);
	}
}
