// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/sched/mm.h>
#include <crypto/hash.h>
#include "messages.h"
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "bio.h"
#include "compression.h"
#include "fs.h"
#include "accessors.h"
#include "file-item.h"

#define __MAX_CSUM_ITEMS(r, size) ((unsigned long)(((BTRFS_LEAF_DATA_SIZE(r) - \
				   sizeof(struct btrfs_item) * 2) / \
				  size) - 1))

#define MAX_CSUM_ITEMS(r, size) (min_t(u32, __MAX_CSUM_ITEMS(r, size), \
				       PAGE_SIZE))

/*
 * Set inode's size according to filesystem options.
 *
 * @inode:      inode we want to update the disk_i_size for
 * @new_i_size: i_size we want to set to, 0 if we use i_size
 *
 * With NO_HOLES set this simply sets the disk_is_size to whatever i_size_read()
 * returns as it is perfectly fine with a file that has holes without hole file
 * extent items.
 *
 * However without NO_HOLES we need to only return the area that is contiguous
 * from the 0 offset of the file.  Otherwise we could end up adjust i_size up
 * to an extent that has a gap in between.
 *
 * Finally new_i_size should only be set in the case of truncate where we're not
 * ready to use i_size_read() as the limiter yet.
 */
void btrfs_inode_safe_disk_i_size_write(struct btrfs_inode *inode, u64 new_i_size)
{
	u64 start, end, i_size;
	int ret;

	spin_lock(&inode->lock);
	i_size = new_i_size ?: i_size_read(&inode->vfs_inode);
	if (!inode->file_extent_tree) {
		inode->disk_i_size = i_size;
		goto out_unlock;
	}

	ret = find_contiguous_extent_bit(inode->file_extent_tree, 0, &start,
					 &end, EXTENT_DIRTY);
	if (!ret && start == 0)
		i_size = min(i_size, end + 1);
	else
		i_size = 0;
	inode->disk_i_size = i_size;
out_unlock:
	spin_unlock(&inode->lock);
}

/*
 * Mark range within a file as having a new extent inserted.
 *
 * @inode: inode being modified
 * @start: start file offset of the file extent we've inserted
 * @len:   logical length of the file extent item
 *
 * Call when we are inserting a new file extent where there was none before.
 * Does not need to call this in the case where we're replacing an existing file
 * extent, however if not sure it's fine to call this multiple times.
 *
 * The start and len must match the file extent item, so thus must be sectorsize
 * aligned.
 */
int btrfs_inode_set_file_extent_range(struct btrfs_inode *inode, u64 start,
				      u64 len)
{
	if (!inode->file_extent_tree)
		return 0;

	if (len == 0)
		return 0;

	ASSERT(IS_ALIGNED(start + len, inode->root->fs_info->sectorsize));

	return set_extent_bit(inode->file_extent_tree, start, start + len - 1,
			      EXTENT_DIRTY, NULL);
}

/*
 * Mark an inode range as not having a backing extent.
 *
 * @inode: inode being modified
 * @start: start file offset of the file extent we've inserted
 * @len:   logical length of the file extent item
 *
 * Called when we drop a file extent, for example when we truncate.  Doesn't
 * need to be called for cases where we're replacing a file extent, like when
 * we've COWed a file extent.
 *
 * The start and len must match the file extent item, so thus must be sectorsize
 * aligned.
 */
int btrfs_inode_clear_file_extent_range(struct btrfs_inode *inode, u64 start,
					u64 len)
{
	if (!inode->file_extent_tree)
		return 0;

	if (len == 0)
		return 0;

	ASSERT(IS_ALIGNED(start + len, inode->root->fs_info->sectorsize) ||
	       len == (u64)-1);

	return clear_extent_bit(inode->file_extent_tree, start,
				start + len - 1, EXTENT_DIRTY, NULL);
}

static size_t bytes_to_csum_size(const struct btrfs_fs_info *fs_info, u32 bytes)
{
	ASSERT(IS_ALIGNED(bytes, fs_info->sectorsize));

	return (bytes >> fs_info->sectorsize_bits) * fs_info->csum_size;
}

static size_t csum_size_to_bytes(const struct btrfs_fs_info *fs_info, u32 csum_size)
{
	ASSERT(IS_ALIGNED(csum_size, fs_info->csum_size));

	return (csum_size / fs_info->csum_size) << fs_info->sectorsize_bits;
}

static inline u32 max_ordered_sum_bytes(const struct btrfs_fs_info *fs_info)
{
	u32 max_csum_size = round_down(PAGE_SIZE - sizeof(struct btrfs_ordered_sum),
				       fs_info->csum_size);

	return csum_size_to_bytes(fs_info, max_csum_size);
}

/*
 * Calculate the total size needed to allocate for an ordered sum structure
 * spanning @bytes in the file.
 */
static int btrfs_ordered_sum_size(const struct btrfs_fs_info *fs_info, unsigned long bytes)
{
	return sizeof(struct btrfs_ordered_sum) + bytes_to_csum_size(fs_info, bytes);
}

int btrfs_insert_hole_extent(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     u64 objectid, u64 pos, u64 num_bytes)
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

	ret = btrfs_insert_empty_item(trans, root, path, &file_key,
				      sizeof(*item));
	if (ret < 0)
		goto out;
	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0],
			      struct btrfs_file_extent_item);
	btrfs_set_file_extent_disk_bytenr(leaf, item, 0);
	btrfs_set_file_extent_disk_num_bytes(leaf, item, 0);
	btrfs_set_file_extent_offset(leaf, item, 0);
	btrfs_set_file_extent_num_bytes(leaf, item, num_bytes);
	btrfs_set_file_extent_ram_bytes(leaf, item, num_bytes);
	btrfs_set_file_extent_generation(leaf, item, trans->transid);
	btrfs_set_file_extent_type(leaf, item, BTRFS_FILE_EXTENT_REG);
	btrfs_set_file_extent_compression(leaf, item, 0);
	btrfs_set_file_extent_encryption(leaf, item, 0);
	btrfs_set_file_extent_other_encoding(leaf, item, 0);

	btrfs_mark_buffer_dirty(trans, leaf);
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
	const u32 csum_size = fs_info->csum_size;
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
				fs_info->sectorsize_bits;
		csums_in_item = btrfs_item_size(leaf, path->slots[0]);
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
	struct btrfs_key file_key;
	int ins_len = mod < 0 ? -1 : 0;
	int cow = mod != 0;

	file_key.objectid = objectid;
	file_key.offset = offset;
	file_key.type = BTRFS_EXTENT_DATA_KEY;

	return btrfs_search_slot(trans, root, &file_key, path, ins_len, cow);
}

/*
 * Find checksums for logical bytenr range [disk_bytenr, disk_bytenr + len) and
 * store the result to @dst.
 *
 * Return >0 for the number of sectors we found.
 * Return 0 for the range [disk_bytenr, disk_bytenr + sectorsize) has no csum
 * for it. Caller may want to try next sector until one range is hit.
 * Return <0 for fatal error.
 */
static int search_csum_tree(struct btrfs_fs_info *fs_info,
			    struct btrfs_path *path, u64 disk_bytenr,
			    u64 len, u8 *dst)
{
	struct btrfs_root *csum_root;
	struct btrfs_csum_item *item = NULL;
	struct btrfs_key key;
	const u32 sectorsize = fs_info->sectorsize;
	const u32 csum_size = fs_info->csum_size;
	u32 itemsize;
	int ret;
	u64 csum_start;
	u64 csum_len;

	ASSERT(IS_ALIGNED(disk_bytenr, sectorsize) &&
	       IS_ALIGNED(len, sectorsize));

	/* Check if the current csum item covers disk_bytenr */
	if (path->nodes[0]) {
		item = btrfs_item_ptr(path->nodes[0], path->slots[0],
				      struct btrfs_csum_item);
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		itemsize = btrfs_item_size(path->nodes[0], path->slots[0]);

		csum_start = key.offset;
		csum_len = (itemsize / csum_size) * sectorsize;

		if (in_range(disk_bytenr, csum_start, csum_len))
			goto found;
	}

	/* Current item doesn't contain the desired range, search again */
	btrfs_release_path(path);
	csum_root = btrfs_csum_root(fs_info, disk_bytenr);
	item = btrfs_lookup_csum(NULL, csum_root, path, disk_bytenr, 0);
	if (IS_ERR(item)) {
		ret = PTR_ERR(item);
		goto out;
	}
	btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
	itemsize = btrfs_item_size(path->nodes[0], path->slots[0]);

	csum_start = key.offset;
	csum_len = (itemsize / csum_size) * sectorsize;
	ASSERT(in_range(disk_bytenr, csum_start, csum_len));

found:
	ret = (min(csum_start + csum_len, disk_bytenr + len) -
		   disk_bytenr) >> fs_info->sectorsize_bits;
	read_extent_buffer(path->nodes[0], dst, (unsigned long)item,
			ret * csum_size);
out:
	if (ret == -ENOENT || ret == -EFBIG)
		ret = 0;
	return ret;
}

/*
 * Lookup the checksum for the read bio in csum tree.
 *
 * Return: BLK_STS_RESOURCE if allocating memory fails, BLK_STS_OK otherwise.
 */
blk_status_t btrfs_lookup_bio_sums(struct btrfs_bio *bbio)
{
	struct btrfs_inode *inode = bbio->inode;
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct bio *bio = &bbio->bio;
	struct btrfs_path *path;
	const u32 sectorsize = fs_info->sectorsize;
	const u32 csum_size = fs_info->csum_size;
	u32 orig_len = bio->bi_iter.bi_size;
	u64 orig_disk_bytenr = bio->bi_iter.bi_sector << SECTOR_SHIFT;
	const unsigned int nblocks = orig_len >> fs_info->sectorsize_bits;
	blk_status_t ret = BLK_STS_OK;
	u32 bio_offset = 0;

	if ((inode->flags & BTRFS_INODE_NODATASUM) ||
	    test_bit(BTRFS_FS_STATE_NO_DATA_CSUMS, &fs_info->fs_state))
		return BLK_STS_OK;

	/*
	 * This function is only called for read bio.
	 *
	 * This means two things:
	 * - All our csums should only be in csum tree
	 *   No ordered extents csums, as ordered extents are only for write
	 *   path.
	 * - No need to bother any other info from bvec
	 *   Since we're looking up csums, the only important info is the
	 *   disk_bytenr and the length, which can be extracted from bi_iter
	 *   directly.
	 */
	ASSERT(bio_op(bio) == REQ_OP_READ);
	path = btrfs_alloc_path();
	if (!path)
		return BLK_STS_RESOURCE;

	if (nblocks * csum_size > BTRFS_BIO_INLINE_CSUM_SIZE) {
		bbio->csum = kmalloc_array(nblocks, csum_size, GFP_NOFS);
		if (!bbio->csum) {
			btrfs_free_path(path);
			return BLK_STS_RESOURCE;
		}
	} else {
		bbio->csum = bbio->csum_inline;
	}

	/*
	 * If requested number of sectors is larger than one leaf can contain,
	 * kick the readahead for csum tree.
	 */
	if (nblocks > fs_info->csums_per_leaf)
		path->reada = READA_FORWARD;

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

	while (bio_offset < orig_len) {
		int count;
		u64 cur_disk_bytenr = orig_disk_bytenr + bio_offset;
		u8 *csum_dst = bbio->csum +
			(bio_offset >> fs_info->sectorsize_bits) * csum_size;

		count = search_csum_tree(fs_info, path, cur_disk_bytenr,
					 orig_len - bio_offset, csum_dst);
		if (count < 0) {
			ret = errno_to_blk_status(count);
			if (bbio->csum != bbio->csum_inline)
				kfree(bbio->csum);
			bbio->csum = NULL;
			break;
		}

		/*
		 * We didn't find a csum for this range.  We need to make sure
		 * we complain loudly about this, because we are not NODATASUM.
		 *
		 * However for the DATA_RELOC inode we could potentially be
		 * relocating data extents for a NODATASUM inode, so the inode
		 * itself won't be marked with NODATASUM, but the extent we're
		 * copying is in fact NODATASUM.  If we don't find a csum we
		 * assume this is the case.
		 */
		if (count == 0) {
			memset(csum_dst, 0, csum_size);
			count = 1;

			if (btrfs_root_id(inode->root) == BTRFS_DATA_RELOC_TREE_OBJECTID) {
				u64 file_offset = bbio->file_offset + bio_offset;

				set_extent_bit(&inode->io_tree, file_offset,
					       file_offset + sectorsize - 1,
					       EXTENT_NODATASUM, NULL);
			} else {
				btrfs_warn_rl(fs_info,
			"csum hole found for disk bytenr range [%llu, %llu)",
				cur_disk_bytenr, cur_disk_bytenr + sectorsize);
			}
		}
		bio_offset += count * sectorsize;
	}

	btrfs_free_path(path);
	return ret;
}

/*
 * Search for checksums for a given logical range.
 *
 * @root:		The root where to look for checksums.
 * @start:		Logical address of target checksum range.
 * @end:		End offset (inclusive) of the target checksum range.
 * @list:		List for adding each checksum that was found.
 *			Can be NULL in case the caller only wants to check if
 *			there any checksums for the range.
 * @nowait:		Indicate if the search must be non-blocking or not.
 *
 * Return < 0 on error, 0 if no checksums were found, or 1 if checksums were
 * found.
 */
int btrfs_lookup_csums_list(struct btrfs_root *root, u64 start, u64 end,
			    struct list_head *list, bool nowait)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_key key;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_ordered_sum *sums;
	struct btrfs_csum_item *item;
	int ret;
	bool found_csums = false;

	ASSERT(IS_ALIGNED(start, fs_info->sectorsize) &&
	       IS_ALIGNED(end + 1, fs_info->sectorsize));

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->nowait = nowait;

	key.objectid = BTRFS_EXTENT_CSUM_OBJECTID;
	key.offset = start;
	key.type = BTRFS_EXTENT_CSUM_KEY;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	if (ret > 0 && path->slots[0] > 0) {
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &key, path->slots[0] - 1);

		/*
		 * There are two cases we can hit here for the previous csum
		 * item:
		 *
		 *		|<- search range ->|
		 *	|<- csum item ->|
		 *
		 * Or
		 *				|<- search range ->|
		 *	|<- csum item ->|
		 *
		 * Check if the previous csum item covers the leading part of
		 * the search range.  If so we have to start from previous csum
		 * item.
		 */
		if (key.objectid == BTRFS_EXTENT_CSUM_OBJECTID &&
		    key.type == BTRFS_EXTENT_CSUM_KEY) {
			if (bytes_to_csum_size(fs_info, start - key.offset) <
			    btrfs_item_size(leaf, path->slots[0] - 1))
				path->slots[0]--;
		}
	}

	while (start <= end) {
		u64 csum_end;

		leaf = path->nodes[0];
		if (path->slots[0] >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
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

		csum_end = key.offset + csum_size_to_bytes(fs_info,
					btrfs_item_size(leaf, path->slots[0]));
		if (csum_end <= start) {
			path->slots[0]++;
			continue;
		}

		found_csums = true;
		if (!list)
			goto out;

		csum_end = min(csum_end, end + 1);
		item = btrfs_item_ptr(path->nodes[0], path->slots[0],
				      struct btrfs_csum_item);
		while (start < csum_end) {
			unsigned long offset;
			size_t size;

			size = min_t(size_t, csum_end - start,
				     max_ordered_sum_bytes(fs_info));
			sums = kzalloc(btrfs_ordered_sum_size(fs_info, size),
				       GFP_NOFS);
			if (!sums) {
				ret = -ENOMEM;
				goto out;
			}

			sums->logical = start;
			sums->len = size;

			offset = bytes_to_csum_size(fs_info, start - key.offset);

			read_extent_buffer(path->nodes[0],
					   sums->sums,
					   ((unsigned long)item) + offset,
					   bytes_to_csum_size(fs_info, size));

			start += size;
			list_add_tail(&sums->list, list);
		}
		path->slots[0]++;
	}
out:
	btrfs_free_path(path);
	if (ret < 0) {
		if (list) {
			struct btrfs_ordered_sum *tmp_sums;

			list_for_each_entry_safe(sums, tmp_sums, list, list)
				kfree(sums);
		}

		return ret;
	}

	return found_csums ? 1 : 0;
}

/*
 * Do the same work as btrfs_lookup_csums_list(), the difference is in how
 * we return the result.
 *
 * This version will set the corresponding bits in @csum_bitmap to represent
 * that there is a csum found.
 * Each bit represents a sector. Thus caller should ensure @csum_buf passed
 * in is large enough to contain all csums.
 */
int btrfs_lookup_csums_bitmap(struct btrfs_root *root, struct btrfs_path *path,
			      u64 start, u64 end, u8 *csum_buf,
			      unsigned long *csum_bitmap)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_csum_item *item;
	const u64 orig_start = start;
	bool free_path = false;
	int ret;

	ASSERT(IS_ALIGNED(start, fs_info->sectorsize) &&
	       IS_ALIGNED(end + 1, fs_info->sectorsize));

	if (!path) {
		path = btrfs_alloc_path();
		if (!path)
			return -ENOMEM;
		free_path = true;
	}

	/* Check if we can reuse the previous path. */
	if (path->nodes[0]) {
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);

		if (key.objectid == BTRFS_EXTENT_CSUM_OBJECTID &&
		    key.type == BTRFS_EXTENT_CSUM_KEY &&
		    key.offset <= start)
			goto search_forward;
		btrfs_release_path(path);
	}

	key.objectid = BTRFS_EXTENT_CSUM_OBJECTID;
	key.type = BTRFS_EXTENT_CSUM_KEY;
	key.offset = start;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto fail;
	if (ret > 0 && path->slots[0] > 0) {
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &key, path->slots[0] - 1);

		/*
		 * There are two cases we can hit here for the previous csum
		 * item:
		 *
		 *		|<- search range ->|
		 *	|<- csum item ->|
		 *
		 * Or
		 *				|<- search range ->|
		 *	|<- csum item ->|
		 *
		 * Check if the previous csum item covers the leading part of
		 * the search range.  If so we have to start from previous csum
		 * item.
		 */
		if (key.objectid == BTRFS_EXTENT_CSUM_OBJECTID &&
		    key.type == BTRFS_EXTENT_CSUM_KEY) {
			if (bytes_to_csum_size(fs_info, start - key.offset) <
			    btrfs_item_size(leaf, path->slots[0] - 1))
				path->slots[0]--;
		}
	}

search_forward:
	while (start <= end) {
		u64 csum_end;

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

		csum_end = key.offset + csum_size_to_bytes(fs_info,
					btrfs_item_size(leaf, path->slots[0]));
		if (csum_end <= start) {
			path->slots[0]++;
			continue;
		}

		csum_end = min(csum_end, end + 1);
		item = btrfs_item_ptr(path->nodes[0], path->slots[0],
				      struct btrfs_csum_item);
		while (start < csum_end) {
			unsigned long offset;
			size_t size;
			u8 *csum_dest = csum_buf + bytes_to_csum_size(fs_info,
						start - orig_start);

			size = min_t(size_t, csum_end - start, end + 1 - start);

			offset = bytes_to_csum_size(fs_info, start - key.offset);

			read_extent_buffer(path->nodes[0], csum_dest,
					   ((unsigned long)item) + offset,
					   bytes_to_csum_size(fs_info, size));

			bitmap_set(csum_bitmap,
				(start - orig_start) >> fs_info->sectorsize_bits,
				size >> fs_info->sectorsize_bits);

			start += size;
		}
		path->slots[0]++;
	}
	ret = 0;
fail:
	if (free_path)
		btrfs_free_path(path);
	return ret;
}

/*
 * Calculate checksums of the data contained inside a bio.
 */
blk_status_t btrfs_csum_one_bio(struct btrfs_bio *bbio)
{
	struct btrfs_ordered_extent *ordered = bbio->ordered;
	struct btrfs_inode *inode = bbio->inode;
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	SHASH_DESC_ON_STACK(shash, fs_info->csum_shash);
	struct bio *bio = &bbio->bio;
	struct btrfs_ordered_sum *sums;
	char *data;
	struct bvec_iter iter;
	struct bio_vec bvec;
	int index;
	unsigned int blockcount;
	int i;
	unsigned nofs_flag;

	nofs_flag = memalloc_nofs_save();
	sums = kvzalloc(btrfs_ordered_sum_size(fs_info, bio->bi_iter.bi_size),
		       GFP_KERNEL);
	memalloc_nofs_restore(nofs_flag);

	if (!sums)
		return BLK_STS_RESOURCE;

	sums->len = bio->bi_iter.bi_size;
	INIT_LIST_HEAD(&sums->list);

	sums->logical = bio->bi_iter.bi_sector << SECTOR_SHIFT;
	index = 0;

	shash->tfm = fs_info->csum_shash;

	bio_for_each_segment(bvec, bio, iter) {
		blockcount = BTRFS_BYTES_TO_BLKS(fs_info,
						 bvec.bv_len + fs_info->sectorsize
						 - 1);

		for (i = 0; i < blockcount; i++) {
			data = bvec_kmap_local(&bvec);
			crypto_shash_digest(shash,
					    data + (i * fs_info->sectorsize),
					    fs_info->sectorsize,
					    sums->sums + index);
			kunmap_local(data);
			index += fs_info->csum_size;
		}

	}

	bbio->sums = sums;
	btrfs_add_ordered_sum(ordered, sums);
	return 0;
}

/*
 * Nodatasum I/O on zoned file systems still requires an btrfs_ordered_sum to
 * record the updated logical address on Zone Append completion.
 * Allocate just the structure with an empty sums array here for that case.
 */
blk_status_t btrfs_alloc_dummy_sum(struct btrfs_bio *bbio)
{
	bbio->sums = kmalloc(sizeof(*bbio->sums), GFP_NOFS);
	if (!bbio->sums)
		return BLK_STS_RESOURCE;
	bbio->sums->len = bbio->bio.bi_iter.bi_size;
	bbio->sums->logical = bbio->bio.bi_iter.bi_sector << SECTOR_SHIFT;
	btrfs_add_ordered_sum(bbio->ordered, bbio->sums);
	return 0;
}

/*
 * Remove one checksum overlapping a range.
 *
 * This expects the key to describe the csum pointed to by the path, and it
 * expects the csum to overlap the range [bytenr, len]
 *
 * The csum should not be entirely contained in the range and the range should
 * not be entirely contained in the csum.
 *
 * This calls btrfs_truncate_item with the correct args based on the overlap,
 * and fixes up the key as required.
 */
static noinline void truncate_one_csum(struct btrfs_trans_handle *trans,
				       struct btrfs_path *path,
				       struct btrfs_key *key,
				       u64 bytenr, u64 len)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct extent_buffer *leaf;
	const u32 csum_size = fs_info->csum_size;
	u64 csum_end;
	u64 end_byte = bytenr + len;
	u32 blocksize_bits = fs_info->sectorsize_bits;

	leaf = path->nodes[0];
	csum_end = btrfs_item_size(leaf, path->slots[0]) / csum_size;
	csum_end <<= blocksize_bits;
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
		btrfs_truncate_item(trans, path, new_size, 1);
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

		btrfs_truncate_item(trans, path, new_size, 0);

		key->offset = end_byte;
		btrfs_set_item_key_safe(trans, path, key);
	} else {
		BUG();
	}
}

/*
 * Delete the csum items from the csum tree for a given range of bytes.
 */
int btrfs_del_csums(struct btrfs_trans_handle *trans,
		    struct btrfs_root *root, u64 bytenr, u64 len)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_path *path;
	struct btrfs_key key;
	u64 end_byte = bytenr + len;
	u64 csum_end;
	struct extent_buffer *leaf;
	int ret = 0;
	const u32 csum_size = fs_info->csum_size;
	u32 blocksize_bits = fs_info->sectorsize_bits;

	ASSERT(btrfs_root_id(root) == BTRFS_CSUM_TREE_OBJECTID ||
	       btrfs_root_id(root) == BTRFS_TREE_LOG_OBJECTID);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	while (1) {
		key.objectid = BTRFS_EXTENT_CSUM_OBJECTID;
		key.offset = end_byte - 1;
		key.type = BTRFS_EXTENT_CSUM_KEY;

		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret > 0) {
			ret = 0;
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

		csum_end = btrfs_item_size(leaf, path->slots[0]) / csum_size;
		csum_end <<= blocksize_bits;
		csum_end += key.offset;

		/* this csum ends before we start, we're done */
		if (csum_end <= bytenr)
			break;

		/* delete the entire item, it is inside our range */
		if (key.offset >= bytenr && csum_end <= end_byte) {
			int del_nr = 1;

			/*
			 * Check how many csum items preceding this one in this
			 * leaf correspond to our range and then delete them all
			 * at once.
			 */
			if (key.offset > bytenr && path->slots[0] > 0) {
				int slot = path->slots[0] - 1;

				while (slot >= 0) {
					struct btrfs_key pk;

					btrfs_item_key_to_cpu(leaf, &pk, slot);
					if (pk.offset < bytenr ||
					    pk.type != BTRFS_EXTENT_CSUM_KEY ||
					    pk.objectid !=
					    BTRFS_EXTENT_CSUM_OBJECTID)
						break;
					path->slots[0] = slot;
					del_nr++;
					key.offset = pk.offset;
					slot--;
				}
			}
			ret = btrfs_del_items(trans, root, path,
					      path->slots[0], del_nr);
			if (ret)
				break;
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
				break;
			}
			ret = 0;

			key.offset = end_byte - 1;
		} else {
			truncate_one_csum(trans, path, &key, bytenr, len);
			if (key.offset < bytenr)
				break;
		}
		btrfs_release_path(path);
	}
	btrfs_free_path(path);
	return ret;
}

static int find_next_csum_offset(struct btrfs_root *root,
				 struct btrfs_path *path,
				 u64 *next_offset)
{
	const u32 nritems = btrfs_header_nritems(path->nodes[0]);
	struct btrfs_key found_key;
	int slot = path->slots[0] + 1;
	int ret;

	if (nritems == 0 || slot >= nritems) {
		ret = btrfs_next_leaf(root, path);
		if (ret < 0) {
			return ret;
		} else if (ret > 0) {
			*next_offset = (u64)-1;
			return 0;
		}
		slot = path->slots[0];
	}

	btrfs_item_key_to_cpu(path->nodes[0], &found_key, slot);

	if (found_key.objectid != BTRFS_EXTENT_CSUM_OBJECTID ||
	    found_key.type != BTRFS_EXTENT_CSUM_KEY)
		*next_offset = (u64)-1;
	else
		*next_offset = found_key.offset;

	return 0;
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
	u32 ins_size;
	int index = 0;
	int found_next;
	int ret;
	const u32 csum_size = fs_info->csum_size;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
again:
	next_offset = (u64)-1;
	found_next = 0;
	bytenr = sums->logical + total_bytes;
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
			   btrfs_item_size(leaf, path->slots[0]));
		goto found;
	}
	ret = PTR_ERR(item);
	if (ret != -EFBIG && ret != -ENOENT)
		goto out;

	if (ret == -EFBIG) {
		u32 item_size;
		/* we found one, but it isn't big enough yet */
		leaf = path->nodes[0];
		item_size = btrfs_item_size(leaf, path->slots[0]);
		if ((item_size / csum_size) >=
		    MAX_CSUM_ITEMS(fs_info, csum_size)) {
			/* already at max size, make a new one */
			goto insert;
		}
	} else {
		/* We didn't find a csum item, insert one. */
		ret = find_next_csum_offset(root, path, &next_offset);
		if (ret < 0)
			goto out;
		found_next = 1;
		goto insert;
	}

	/*
	 * At this point, we know the tree has a checksum item that ends at an
	 * offset matching the start of the checksum range we want to insert.
	 * We try to extend that item as much as possible and then add as many
	 * checksums to it as they fit.
	 *
	 * First check if the leaf has enough free space for at least one
	 * checksum. If it has go directly to the item extension code, otherwise
	 * release the path and do a search for insertion before the extension.
	 */
	if (btrfs_leaf_free_space(leaf) >= csum_size) {
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		csum_offset = (bytenr - found_key.offset) >>
			fs_info->sectorsize_bits;
		goto extend_csum;
	}

	btrfs_release_path(path);
	path->search_for_extension = 1;
	ret = btrfs_search_slot(trans, root, &file_key, path,
				csum_size, 1);
	path->search_for_extension = 0;
	if (ret < 0)
		goto out;

	if (ret > 0) {
		if (path->slots[0] == 0)
			goto insert;
		path->slots[0]--;
	}

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
	csum_offset = (bytenr - found_key.offset) >> fs_info->sectorsize_bits;

	if (found_key.type != BTRFS_EXTENT_CSUM_KEY ||
	    found_key.objectid != BTRFS_EXTENT_CSUM_OBJECTID ||
	    csum_offset >= MAX_CSUM_ITEMS(fs_info, csum_size)) {
		goto insert;
	}

extend_csum:
	if (csum_offset == btrfs_item_size(leaf, path->slots[0]) /
	    csum_size) {
		int extend_nr;
		u64 tmp;
		u32 diff;

		tmp = sums->len - total_bytes;
		tmp >>= fs_info->sectorsize_bits;
		WARN_ON(tmp < 1);
		extend_nr = max_t(int, 1, tmp);

		/*
		 * A log tree can already have checksum items with a subset of
		 * the checksums we are trying to log. This can happen after
		 * doing a sequence of partial writes into prealloc extents and
		 * fsyncs in between, with a full fsync logging a larger subrange
		 * of an extent for which a previous fast fsync logged a smaller
		 * subrange. And this happens in particular due to merging file
		 * extent items when we complete an ordered extent for a range
		 * covered by a prealloc extent - this is done at
		 * btrfs_mark_extent_written().
		 *
		 * So if we try to extend the previous checksum item, which has
		 * a range that ends at the start of the range we want to insert,
		 * make sure we don't extend beyond the start offset of the next
		 * checksum item. If we are at the last item in the leaf, then
		 * forget the optimization of extending and add a new checksum
		 * item - it is not worth the complexity of releasing the path,
		 * getting the first key for the next leaf, repeat the btree
		 * search, etc, because log trees are temporary anyway and it
		 * would only save a few bytes of leaf space.
		 */
		if (btrfs_root_id(root) == BTRFS_TREE_LOG_OBJECTID) {
			if (path->slots[0] + 1 >=
			    btrfs_header_nritems(path->nodes[0])) {
				ret = find_next_csum_offset(root, path, &next_offset);
				if (ret < 0)
					goto out;
				found_next = 1;
				goto insert;
			}

			ret = find_next_csum_offset(root, path, &next_offset);
			if (ret < 0)
				goto out;

			tmp = (next_offset - bytenr) >> fs_info->sectorsize_bits;
			if (tmp <= INT_MAX)
				extend_nr = min_t(int, extend_nr, tmp);
		}

		diff = (csum_offset + extend_nr) * csum_size;
		diff = min(diff,
			   MAX_CSUM_ITEMS(fs_info, csum_size) * csum_size);

		diff = diff - btrfs_item_size(leaf, path->slots[0]);
		diff = min_t(u32, btrfs_leaf_free_space(leaf), diff);
		diff /= csum_size;
		diff *= csum_size;

		btrfs_extend_item(trans, path, diff);
		ret = 0;
		goto csum;
	}

insert:
	btrfs_release_path(path);
	csum_offset = 0;
	if (found_next) {
		u64 tmp;

		tmp = sums->len - total_bytes;
		tmp >>= fs_info->sectorsize_bits;
		tmp = min(tmp, (next_offset - file_key.offset) >>
					 fs_info->sectorsize_bits);

		tmp = max_t(u64, 1, tmp);
		tmp = min_t(u64, tmp, MAX_CSUM_ITEMS(fs_info, csum_size));
		ins_size = csum_size * tmp;
	} else {
		ins_size = csum_size;
	}
	ret = btrfs_insert_empty_item(trans, root, path, &file_key,
				      ins_size);
	if (ret < 0)
		goto out;
	leaf = path->nodes[0];
csum:
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_csum_item);
	item_end = (struct btrfs_csum_item *)((unsigned char *)item +
				      btrfs_item_size(leaf, path->slots[0]));
	item = (struct btrfs_csum_item *)((unsigned char *)item +
					  csum_offset * csum_size);
found:
	ins_size = (u32)(sums->len - total_bytes) >> fs_info->sectorsize_bits;
	ins_size *= csum_size;
	ins_size = min_t(u32, (unsigned long)item_end - (unsigned long)item,
			      ins_size);
	write_extent_buffer(leaf, sums->sums + index, (unsigned long)item,
			    ins_size);

	index += ins_size;
	ins_size /= csum_size;
	total_bytes += ins_size * fs_info->sectorsize;

	btrfs_mark_buffer_dirty(trans, path->nodes[0]);
	if (total_bytes < sums->len) {
		btrfs_release_path(path);
		cond_resched();
		goto again;
	}
out:
	btrfs_free_path(path);
	return ret;
}

void btrfs_extent_item_to_extent_map(struct btrfs_inode *inode,
				     const struct btrfs_path *path,
				     const struct btrfs_file_extent_item *fi,
				     struct extent_map *em)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct btrfs_root *root = inode->root;
	struct extent_buffer *leaf = path->nodes[0];
	const int slot = path->slots[0];
	struct btrfs_key key;
	u64 extent_start;
	u8 type = btrfs_file_extent_type(leaf, fi);
	int compress_type = btrfs_file_extent_compression(leaf, fi);

	btrfs_item_key_to_cpu(leaf, &key, slot);
	extent_start = key.offset;
	em->ram_bytes = btrfs_file_extent_ram_bytes(leaf, fi);
	em->generation = btrfs_file_extent_generation(leaf, fi);
	if (type == BTRFS_FILE_EXTENT_REG ||
	    type == BTRFS_FILE_EXTENT_PREALLOC) {
		const u64 disk_bytenr = btrfs_file_extent_disk_bytenr(leaf, fi);

		em->start = extent_start;
		em->len = btrfs_file_extent_end(path) - extent_start;
		if (disk_bytenr == 0) {
			em->disk_bytenr = EXTENT_MAP_HOLE;
			em->disk_num_bytes = 0;
			em->offset = 0;
			return;
		}
		em->disk_bytenr = disk_bytenr;
		em->disk_num_bytes = btrfs_file_extent_disk_num_bytes(leaf, fi);
		em->offset = btrfs_file_extent_offset(leaf, fi);
		if (compress_type != BTRFS_COMPRESS_NONE) {
			extent_map_set_compression(em, compress_type);
		} else {
			/*
			 * Older kernels can create regular non-hole data
			 * extents with ram_bytes smaller than disk_num_bytes.
			 * Not a big deal, just always use disk_num_bytes
			 * for ram_bytes.
			 */
			em->ram_bytes = em->disk_num_bytes;
			if (type == BTRFS_FILE_EXTENT_PREALLOC)
				em->flags |= EXTENT_FLAG_PREALLOC;
		}
	} else if (type == BTRFS_FILE_EXTENT_INLINE) {
		/* Tree-checker has ensured this. */
		ASSERT(extent_start == 0);

		em->disk_bytenr = EXTENT_MAP_INLINE;
		em->start = 0;
		em->len = fs_info->sectorsize;
		em->offset = 0;
		extent_map_set_compression(em, compress_type);
	} else {
		btrfs_err(fs_info,
			  "unknown file extent item type %d, inode %llu, offset %llu, "
			  "root %llu", type, btrfs_ino(inode), extent_start,
			  btrfs_root_id(root));
	}
}

/*
 * Returns the end offset (non inclusive) of the file extent item the given path
 * points to. If it points to an inline extent, the returned offset is rounded
 * up to the sector size.
 */
u64 btrfs_file_extent_end(const struct btrfs_path *path)
{
	const struct extent_buffer *leaf = path->nodes[0];
	const int slot = path->slots[0];
	struct btrfs_file_extent_item *fi;
	struct btrfs_key key;
	u64 end;

	btrfs_item_key_to_cpu(leaf, &key, slot);
	ASSERT(key.type == BTRFS_EXTENT_DATA_KEY);
	fi = btrfs_item_ptr(leaf, slot, struct btrfs_file_extent_item);

	if (btrfs_file_extent_type(leaf, fi) == BTRFS_FILE_EXTENT_INLINE)
		end = leaf->fs_info->sectorsize;
	else
		end = key.offset + btrfs_file_extent_num_bytes(leaf, fi);

	return end;
}
