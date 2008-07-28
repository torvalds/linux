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

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/backing-dev.h>
#include <linux/mpage.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/statfs.h>
#include <linux/compat.h>
#include <linux/version.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "ioctl.h"
#include "print-tree.h"
#include "compat.h"


static int btrfs_copy_from_user(loff_t pos, int num_pages, int write_bytes,
				struct page **prepared_pages,
				const char __user * buf)
{
	long page_fault = 0;
	int i;
	int offset = pos & (PAGE_CACHE_SIZE - 1);

	for (i = 0; i < num_pages && write_bytes > 0; i++, offset = 0) {
		size_t count = min_t(size_t,
				     PAGE_CACHE_SIZE - offset, write_bytes);
		struct page *page = prepared_pages[i];
		fault_in_pages_readable(buf, count);

		/* Copy data from userspace to the current page */
		kmap(page);
		page_fault = __copy_from_user(page_address(page) + offset,
					      buf, count);
		/* Flush processor's dcache for this page */
		flush_dcache_page(page);
		kunmap(page);
		buf += count;
		write_bytes -= count;

		if (page_fault)
			break;
	}
	return page_fault ? -EFAULT : 0;
}

static void btrfs_drop_pages(struct page **pages, size_t num_pages)
{
	size_t i;
	for (i = 0; i < num_pages; i++) {
		if (!pages[i])
			break;
		ClearPageChecked(pages[i]);
		unlock_page(pages[i]);
		mark_page_accessed(pages[i]);
		page_cache_release(pages[i]);
	}
}

static int noinline insert_inline_extent(struct btrfs_trans_handle *trans,
				struct btrfs_root *root, struct inode *inode,
				u64 offset, size_t size,
				struct page **pages, size_t page_offset,
				int num_pages)
{
	struct btrfs_key key;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	char *kaddr;
	unsigned long ptr;
	struct btrfs_file_extent_item *ei;
	struct page *page;
	u32 datasize;
	int err = 0;
	int ret;
	int i;
	ssize_t cur_size;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	btrfs_set_trans_block_group(trans, inode);

	key.objectid = inode->i_ino;
	key.offset = offset;
	btrfs_set_key_type(&key, BTRFS_EXTENT_DATA_KEY);

	ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
	if (ret < 0) {
		err = ret;
		goto fail;
	}
	if (ret == 1) {
		struct btrfs_key found_key;

		if (path->slots[0] == 0)
			goto insert;

		path->slots[0]--;
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		if (found_key.objectid != inode->i_ino)
			goto insert;

		if (found_key.type != BTRFS_EXTENT_DATA_KEY)
			goto insert;
		ei = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);

		if (btrfs_file_extent_type(leaf, ei) !=
		    BTRFS_FILE_EXTENT_INLINE) {
			goto insert;
		}
		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		ret = 0;
	}
	if (ret == 0) {
		u32 found_size;
		u64 found_end;

		leaf = path->nodes[0];
		ei = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);

		if (btrfs_file_extent_type(leaf, ei) !=
		    BTRFS_FILE_EXTENT_INLINE) {
			err = ret;
			btrfs_print_leaf(root, leaf);
			printk("found wasn't inline offset %Lu inode %lu\n",
			       offset, inode->i_ino);
			goto fail;
		}
		found_size = btrfs_file_extent_inline_len(leaf,
					  btrfs_item_nr(leaf, path->slots[0]));
		found_end = key.offset + found_size;

		if (found_end < offset + size) {
			btrfs_release_path(root, path);
			ret = btrfs_search_slot(trans, root, &key, path,
						offset + size - found_end, 1);
			BUG_ON(ret != 0);

			ret = btrfs_extend_item(trans, root, path,
						offset + size - found_end);
			if (ret) {
				err = ret;
				goto fail;
			}
			leaf = path->nodes[0];
			ei = btrfs_item_ptr(leaf, path->slots[0],
					    struct btrfs_file_extent_item);
			inode->i_blocks += (offset + size - found_end) >> 9;
		}
		if (found_end < offset) {
			ptr = btrfs_file_extent_inline_start(ei) + found_size;
			memset_extent_buffer(leaf, 0, ptr, offset - found_end);
		}
	} else {
insert:
		btrfs_release_path(root, path);
		datasize = offset + size - key.offset;
		inode->i_blocks += datasize >> 9;
		datasize = btrfs_file_extent_calc_inline_size(datasize);
		ret = btrfs_insert_empty_item(trans, root, path, &key,
					      datasize);
		if (ret) {
			err = ret;
			printk("got bad ret %d\n", ret);
			goto fail;
		}
		leaf = path->nodes[0];
		ei = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);
		btrfs_set_file_extent_generation(leaf, ei, trans->transid);
		btrfs_set_file_extent_type(leaf, ei, BTRFS_FILE_EXTENT_INLINE);
	}
	ptr = btrfs_file_extent_inline_start(ei) + offset - key.offset;

	cur_size = size;
	i = 0;
	while (size > 0) {
		page = pages[i];
		kaddr = kmap_atomic(page, KM_USER0);
		cur_size = min_t(size_t, PAGE_CACHE_SIZE - page_offset, size);
		write_extent_buffer(leaf, kaddr + page_offset, ptr, cur_size);
		kunmap_atomic(kaddr, KM_USER0);
		page_offset = 0;
		ptr += cur_size;
		size -= cur_size;
		if (i >= num_pages) {
			printk("i %d num_pages %d\n", i, num_pages);
		}
		i++;
	}
	btrfs_mark_buffer_dirty(leaf);
fail:
	btrfs_free_path(path);
	return err;
}

static int noinline dirty_and_release_pages(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct file *file,
				   struct page **pages,
				   size_t num_pages,
				   loff_t pos,
				   size_t write_bytes)
{
	int err = 0;
	int i;
	struct inode *inode = fdentry(file)->d_inode;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	u64 hint_byte;
	u64 num_bytes;
	u64 start_pos;
	u64 end_of_last_block;
	u64 end_pos = pos + write_bytes;
	u64 inline_size;
	int did_inline = 0;
	loff_t isize = i_size_read(inode);

	start_pos = pos & ~((u64)root->sectorsize - 1);
	num_bytes = (write_bytes + pos - start_pos +
		    root->sectorsize - 1) & ~((u64)root->sectorsize - 1);

	end_of_last_block = start_pos + num_bytes - 1;

	lock_extent(io_tree, start_pos, end_of_last_block, GFP_NOFS);
	trans = btrfs_start_transaction(root, 1);
	if (!trans) {
		err = -ENOMEM;
		goto out_unlock;
	}
	btrfs_set_trans_block_group(trans, inode);
	hint_byte = 0;

	if ((end_of_last_block & 4095) == 0) {
		printk("strange end of last %Lu %zu %Lu\n", start_pos, write_bytes, end_of_last_block);
	}
	set_extent_uptodate(io_tree, start_pos, end_of_last_block, GFP_NOFS);

	/* FIXME...EIEIO, ENOSPC and more */
	/* insert any holes we need to create */
	if (isize < start_pos) {
		u64 last_pos_in_file;
		u64 hole_size;
		u64 mask = root->sectorsize - 1;
		last_pos_in_file = (isize + mask) & ~mask;
		hole_size = (start_pos - last_pos_in_file + mask) & ~mask;
		if (hole_size > 0) {
			btrfs_wait_ordered_range(inode, last_pos_in_file,
						 last_pos_in_file + hole_size);
			mutex_lock(&BTRFS_I(inode)->extent_mutex);
			err = btrfs_drop_extents(trans, root, inode,
						 last_pos_in_file,
						 last_pos_in_file + hole_size,
						 last_pos_in_file,
						 &hint_byte);
			if (err)
				goto failed;

			err = btrfs_insert_file_extent(trans, root,
						       inode->i_ino,
						       last_pos_in_file,
						       0, 0, hole_size, 0);
			btrfs_drop_extent_cache(inode, last_pos_in_file,
					last_pos_in_file + hole_size -1);
			mutex_unlock(&BTRFS_I(inode)->extent_mutex);
			btrfs_check_file(root, inode);
		}
		if (err)
			goto failed;
	}

	/*
	 * either allocate an extent for the new bytes or setup the key
	 * to show we are doing inline data in the extent
	 */
	inline_size = end_pos;
	if (isize >= BTRFS_MAX_INLINE_DATA_SIZE(root) ||
	    inline_size > root->fs_info->max_inline ||
	    (inline_size & (root->sectorsize -1)) == 0 ||
	    inline_size >= BTRFS_MAX_INLINE_DATA_SIZE(root)) {
		/* check for reserved extents on each page, we don't want
		 * to reset the delalloc bit on things that already have
		 * extents reserved.
		 */
		set_extent_delalloc(io_tree, start_pos,
				    end_of_last_block, GFP_NOFS);
		for (i = 0; i < num_pages; i++) {
			struct page *p = pages[i];
			SetPageUptodate(p);
			ClearPageChecked(p);
			set_page_dirty(p);
		}
	} else {
		u64 aligned_end;
		/* step one, delete the existing extents in this range */
		aligned_end = (pos + write_bytes + root->sectorsize - 1) &
			~((u64)root->sectorsize - 1);
		mutex_lock(&BTRFS_I(inode)->extent_mutex);
		err = btrfs_drop_extents(trans, root, inode, start_pos,
					 aligned_end, aligned_end, &hint_byte);
		if (err)
			goto failed;
		if (isize > inline_size)
			inline_size = min_t(u64, isize, aligned_end);
		inline_size -= start_pos;
		err = insert_inline_extent(trans, root, inode, start_pos,
					   inline_size, pages, 0, num_pages);
		btrfs_drop_extent_cache(inode, start_pos, aligned_end - 1);
		BUG_ON(err);
		mutex_unlock(&BTRFS_I(inode)->extent_mutex);
		did_inline = 1;
	}
	if (end_pos > isize) {
		i_size_write(inode, end_pos);
		if (did_inline)
			BTRFS_I(inode)->disk_i_size = end_pos;
		btrfs_update_inode(trans, root, inode);
	}
failed:
	err = btrfs_end_transaction(trans, root);
out_unlock:
	unlock_extent(io_tree, start_pos, end_of_last_block, GFP_NOFS);
	return err;
}

int btrfs_drop_extent_cache(struct inode *inode, u64 start, u64 end)
{
	struct extent_map *em;
	struct extent_map *split = NULL;
	struct extent_map *split2 = NULL;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	u64 len = end - start + 1;
	int ret;
	int testend = 1;

	WARN_ON(end < start);
	if (end == (u64)-1) {
		len = (u64)-1;
		testend = 0;
	}
	while(1) {
		if (!split)
			split = alloc_extent_map(GFP_NOFS);
		if (!split2)
			split2 = alloc_extent_map(GFP_NOFS);

		spin_lock(&em_tree->lock);
		em = lookup_extent_mapping(em_tree, start, len);
		if (!em) {
			spin_unlock(&em_tree->lock);
			break;
		}
		if (test_bit(EXTENT_FLAG_PINNED, &em->flags)) {
			printk(KERN_CRIT "inode %lu trying to drop pinned "
			       "extent start %llu end %llu, em [%llu %llu]\n",
			       inode->i_ino,
			       (unsigned long long)start,
			       (unsigned long long)end,
			       (unsigned long long)em->start,
			       (unsigned long long)em->len);
		}
		remove_extent_mapping(em_tree, em);

		if (em->block_start < EXTENT_MAP_LAST_BYTE &&
		    em->start < start) {
			split->start = em->start;
			split->len = start - em->start;
			split->block_start = em->block_start;
			split->bdev = em->bdev;
			split->flags = em->flags;
			ret = add_extent_mapping(em_tree, split);
			BUG_ON(ret);
			free_extent_map(split);
			split = split2;
			split2 = NULL;
		}
		if (em->block_start < EXTENT_MAP_LAST_BYTE &&
		    testend && em->start + em->len > start + len) {
			u64 diff = start + len - em->start;

			split->start = start + len;
			split->len = em->start + em->len - (start + len);
			split->bdev = em->bdev;
			split->flags = em->flags;

			split->block_start = em->block_start + diff;

			ret = add_extent_mapping(em_tree, split);
			BUG_ON(ret);
			free_extent_map(split);
			split = NULL;
		}
		spin_unlock(&em_tree->lock);

		/* once for us */
		free_extent_map(em);
		/* once for the tree*/
		free_extent_map(em);
	}
	if (split)
		free_extent_map(split);
	if (split2)
		free_extent_map(split2);
	return 0;
}

int btrfs_check_file(struct btrfs_root *root, struct inode *inode)
{
	return 0;
#if 0
	struct btrfs_path *path;
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	struct btrfs_file_extent_item *extent;
	u64 last_offset = 0;
	int nritems;
	int slot;
	int found_type;
	int ret;
	int err = 0;
	u64 extent_end = 0;

	path = btrfs_alloc_path();
	ret = btrfs_lookup_file_extent(NULL, root, path, inode->i_ino,
				       last_offset, 0);
	while(1) {
		nritems = btrfs_header_nritems(path->nodes[0]);
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(root, path);
			if (ret)
				goto out;
			nritems = btrfs_header_nritems(path->nodes[0]);
		}
		slot = path->slots[0];
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, slot);
		if (found_key.objectid != inode->i_ino)
			break;
		if (found_key.type != BTRFS_EXTENT_DATA_KEY)
			goto out;

		if (found_key.offset < last_offset) {
			WARN_ON(1);
			btrfs_print_leaf(root, leaf);
			printk("inode %lu found offset %Lu expected %Lu\n",
			       inode->i_ino, found_key.offset, last_offset);
			err = 1;
			goto out;
		}
		extent = btrfs_item_ptr(leaf, slot,
					struct btrfs_file_extent_item);
		found_type = btrfs_file_extent_type(leaf, extent);
		if (found_type == BTRFS_FILE_EXTENT_REG) {
			extent_end = found_key.offset +
			     btrfs_file_extent_num_bytes(leaf, extent);
		} else if (found_type == BTRFS_FILE_EXTENT_INLINE) {
			struct btrfs_item *item;
			item = btrfs_item_nr(leaf, slot);
			extent_end = found_key.offset +
			     btrfs_file_extent_inline_len(leaf, item);
			extent_end = (extent_end + root->sectorsize - 1) &
				~((u64)root->sectorsize -1 );
		}
		last_offset = extent_end;
		path->slots[0]++;
	}
	if (0 && last_offset < inode->i_size) {
		WARN_ON(1);
		btrfs_print_leaf(root, leaf);
		printk("inode %lu found offset %Lu size %Lu\n", inode->i_ino,
		       last_offset, inode->i_size);
		err = 1;

	}
out:
	btrfs_free_path(path);
	return err;
#endif
}

/*
 * this is very complex, but the basic idea is to drop all extents
 * in the range start - end.  hint_block is filled in with a block number
 * that would be a good hint to the block allocator for this file.
 *
 * If an extent intersects the range but is not entirely inside the range
 * it is either truncated or split.  Anything entirely inside the range
 * is deleted from the tree.
 */
int btrfs_drop_extents(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct inode *inode,
		       u64 start, u64 end, u64 inline_limit, u64 *hint_byte)
{
	u64 extent_end = 0;
	u64 search_start = start;
	struct extent_buffer *leaf;
	struct btrfs_file_extent_item *extent;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_file_extent_item old;
	int keep;
	int slot;
	int bookend;
	int found_type;
	int found_extent;
	int found_inline;
	int recow;
	int ret;

	btrfs_drop_extent_cache(inode, start, end - 1);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	while(1) {
		recow = 0;
		btrfs_release_path(root, path);
		ret = btrfs_lookup_file_extent(trans, root, path, inode->i_ino,
					       search_start, -1);
		if (ret < 0)
			goto out;
		if (ret > 0) {
			if (path->slots[0] == 0) {
				ret = 0;
				goto out;
			}
			path->slots[0]--;
		}
next_slot:
		keep = 0;
		bookend = 0;
		found_extent = 0;
		found_inline = 0;
		extent = NULL;
		leaf = path->nodes[0];
		slot = path->slots[0];
		ret = 0;
		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (btrfs_key_type(&key) == BTRFS_EXTENT_DATA_KEY &&
		    key.offset >= end) {
			goto out;
		}
		if (btrfs_key_type(&key) > BTRFS_EXTENT_DATA_KEY ||
		    key.objectid != inode->i_ino) {
			goto out;
		}
		if (recow) {
			search_start = key.offset;
			continue;
		}
		if (btrfs_key_type(&key) == BTRFS_EXTENT_DATA_KEY) {
			extent = btrfs_item_ptr(leaf, slot,
						struct btrfs_file_extent_item);
			found_type = btrfs_file_extent_type(leaf, extent);
			if (found_type == BTRFS_FILE_EXTENT_REG) {
				extent_end =
				     btrfs_file_extent_disk_bytenr(leaf,
								   extent);
				if (extent_end)
					*hint_byte = extent_end;

				extent_end = key.offset +
				     btrfs_file_extent_num_bytes(leaf, extent);
				found_extent = 1;
			} else if (found_type == BTRFS_FILE_EXTENT_INLINE) {
				struct btrfs_item *item;
				item = btrfs_item_nr(leaf, slot);
				found_inline = 1;
				extent_end = key.offset +
				     btrfs_file_extent_inline_len(leaf, item);
			}
		} else {
			extent_end = search_start;
		}

		/* we found nothing we can drop */
		if ((!found_extent && !found_inline) ||
		    search_start >= extent_end) {
			int nextret;
			u32 nritems;
			nritems = btrfs_header_nritems(leaf);
			if (slot >= nritems - 1) {
				nextret = btrfs_next_leaf(root, path);
				if (nextret)
					goto out;
				recow = 1;
			} else {
				path->slots[0]++;
			}
			goto next_slot;
		}

		if (found_inline) {
			u64 mask = root->sectorsize - 1;
			search_start = (extent_end + mask) & ~mask;
		} else
			search_start = extent_end;
		if (end <= extent_end && start >= key.offset && found_inline) {
			*hint_byte = EXTENT_MAP_INLINE;
			continue;
		}
		if (end < extent_end && end >= key.offset) {
			if (found_extent) {
				u64 disk_bytenr =
				    btrfs_file_extent_disk_bytenr(leaf, extent);
				u64 disk_num_bytes =
				    btrfs_file_extent_disk_num_bytes(leaf,
								      extent);
				read_extent_buffer(leaf, &old,
						   (unsigned long)extent,
						   sizeof(old));
				if (disk_bytenr != 0) {
					ret = btrfs_inc_extent_ref(trans, root,
					         disk_bytenr, disk_num_bytes,
						 root->root_key.objectid,
						 trans->transid,
						 key.objectid, end);
					BUG_ON(ret);
				}
			}
			bookend = 1;
			if (found_inline && start <= key.offset)
				keep = 1;
		}
		/* truncate existing extent */
		if (start > key.offset) {
			u64 new_num;
			u64 old_num;
			keep = 1;
			WARN_ON(start & (root->sectorsize - 1));
			if (found_extent) {
				new_num = start - key.offset;
				old_num = btrfs_file_extent_num_bytes(leaf,
								      extent);
				*hint_byte =
					btrfs_file_extent_disk_bytenr(leaf,
								      extent);
				if (btrfs_file_extent_disk_bytenr(leaf,
								  extent)) {
					dec_i_blocks(inode, old_num - new_num);
				}
				btrfs_set_file_extent_num_bytes(leaf, extent,
								new_num);
				btrfs_mark_buffer_dirty(leaf);
			} else if (key.offset < inline_limit &&
				   (end > extent_end) &&
				   (inline_limit < extent_end)) {
				u32 new_size;
				new_size = btrfs_file_extent_calc_inline_size(
						   inline_limit - key.offset);
				dec_i_blocks(inode, (extent_end - key.offset) -
					(inline_limit - key.offset));
				btrfs_truncate_item(trans, root, path,
						    new_size, 1);
			}
		}
		/* delete the entire extent */
		if (!keep) {
			u64 disk_bytenr = 0;
			u64 disk_num_bytes = 0;
			u64 extent_num_bytes = 0;
			u64 root_gen;
			u64 root_owner;

			root_gen = btrfs_header_generation(leaf);
			root_owner = btrfs_header_owner(leaf);
			if (found_extent) {
				disk_bytenr =
				      btrfs_file_extent_disk_bytenr(leaf,
								     extent);
				disk_num_bytes =
				      btrfs_file_extent_disk_num_bytes(leaf,
								       extent);
				extent_num_bytes =
				      btrfs_file_extent_num_bytes(leaf, extent);
				*hint_byte =
					btrfs_file_extent_disk_bytenr(leaf,
								      extent);
			}
			ret = btrfs_del_item(trans, root, path);
			/* TODO update progress marker and return */
			BUG_ON(ret);
			btrfs_release_path(root, path);
			extent = NULL;
			if (found_extent && disk_bytenr != 0) {
				dec_i_blocks(inode, extent_num_bytes);
				ret = btrfs_free_extent(trans, root,
						disk_bytenr,
						disk_num_bytes,
						root_owner,
						root_gen, inode->i_ino,
						key.offset, 0);
			}

			BUG_ON(ret);
			if (!bookend && search_start >= end) {
				ret = 0;
				goto out;
			}
			if (!bookend)
				continue;
		}
		if (bookend && found_inline && start <= key.offset) {
			u32 new_size;
			new_size = btrfs_file_extent_calc_inline_size(
						   extent_end - end);
			dec_i_blocks(inode, (extent_end - key.offset) -
					(extent_end - end));
			btrfs_truncate_item(trans, root, path, new_size, 0);
		}
		/* create bookend, splitting the extent in two */
		if (bookend && found_extent) {
			struct btrfs_key ins;
			ins.objectid = inode->i_ino;
			ins.offset = end;
			btrfs_set_key_type(&ins, BTRFS_EXTENT_DATA_KEY);
			btrfs_release_path(root, path);
			ret = btrfs_insert_empty_item(trans, root, path, &ins,
						      sizeof(*extent));

			leaf = path->nodes[0];
			if (ret) {
				btrfs_print_leaf(root, leaf);
				printk("got %d on inserting %Lu %u %Lu start %Lu end %Lu found %Lu %Lu keep was %d\n", ret , ins.objectid, ins.type, ins.offset, start, end, key.offset, extent_end, keep);
			}
			BUG_ON(ret);
			extent = btrfs_item_ptr(leaf, path->slots[0],
						struct btrfs_file_extent_item);
			write_extent_buffer(leaf, &old,
					    (unsigned long)extent, sizeof(old));

			btrfs_set_file_extent_offset(leaf, extent,
				    le64_to_cpu(old.offset) + end - key.offset);
			WARN_ON(le64_to_cpu(old.num_bytes) <
				(extent_end - end));
			btrfs_set_file_extent_num_bytes(leaf, extent,
							extent_end - end);
			btrfs_set_file_extent_type(leaf, extent,
						   BTRFS_FILE_EXTENT_REG);

			btrfs_mark_buffer_dirty(path->nodes[0]);
			if (le64_to_cpu(old.disk_bytenr) != 0) {
				inode->i_blocks +=
				      btrfs_file_extent_num_bytes(leaf,
								  extent) >> 9;
			}
			ret = 0;
			goto out;
		}
	}
out:
	btrfs_free_path(path);
	btrfs_check_file(root, inode);
	return ret;
}

/*
 * this gets pages into the page cache and locks them down
 */
static int prepare_pages(struct btrfs_root *root, struct file *file,
			 struct page **pages, size_t num_pages,
			 loff_t pos, unsigned long first_index,
			 unsigned long last_index, size_t write_bytes)
{
	int i;
	unsigned long index = pos >> PAGE_CACHE_SHIFT;
	struct inode *inode = fdentry(file)->d_inode;
	int err = 0;
	u64 start_pos;
	u64 last_pos;

	start_pos = pos & ~((u64)root->sectorsize - 1);
	last_pos = ((u64)index + num_pages) << PAGE_CACHE_SHIFT;

	memset(pages, 0, num_pages * sizeof(struct page *));
again:
	for (i = 0; i < num_pages; i++) {
		pages[i] = grab_cache_page(inode->i_mapping, index + i);
		if (!pages[i]) {
			err = -ENOMEM;
			BUG_ON(1);
		}
		wait_on_page_writeback(pages[i]);
	}
	if (start_pos < inode->i_size) {
		struct btrfs_ordered_extent *ordered;
		lock_extent(&BTRFS_I(inode)->io_tree,
			    start_pos, last_pos - 1, GFP_NOFS);
		ordered = btrfs_lookup_first_ordered_extent(inode, last_pos -1);
		if (ordered &&
		    ordered->file_offset + ordered->len > start_pos &&
		    ordered->file_offset < last_pos) {
			btrfs_put_ordered_extent(ordered);
			unlock_extent(&BTRFS_I(inode)->io_tree,
				      start_pos, last_pos - 1, GFP_NOFS);
			for (i = 0; i < num_pages; i++) {
				unlock_page(pages[i]);
				page_cache_release(pages[i]);
			}
			btrfs_wait_ordered_range(inode, start_pos,
						 last_pos - start_pos);
			goto again;
		}
		if (ordered)
			btrfs_put_ordered_extent(ordered);

		clear_extent_bits(&BTRFS_I(inode)->io_tree, start_pos,
				  last_pos - 1, EXTENT_DIRTY | EXTENT_DELALLOC,
				  GFP_NOFS);
		unlock_extent(&BTRFS_I(inode)->io_tree,
			      start_pos, last_pos - 1, GFP_NOFS);
	}
	for (i = 0; i < num_pages; i++) {
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)
		ClearPageDirty(pages[i]);
#else
		cancel_dirty_page(pages[i], PAGE_CACHE_SIZE);
#endif
		set_page_extent_mapped(pages[i]);
		WARN_ON(!PageLocked(pages[i]));
	}
	return 0;
}

static ssize_t btrfs_file_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	loff_t pos;
	loff_t start_pos;
	ssize_t num_written = 0;
	ssize_t err = 0;
	int ret = 0;
	struct inode *inode = fdentry(file)->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct page **pages = NULL;
	int nrptrs;
	struct page *pinned[2];
	unsigned long first_index;
	unsigned long last_index;

	nrptrs = min((count + PAGE_CACHE_SIZE - 1) / PAGE_CACHE_SIZE,
		     PAGE_CACHE_SIZE / (sizeof(struct page *)));
	pinned[0] = NULL;
	pinned[1] = NULL;

	pos = *ppos;
	start_pos = pos;

	vfs_check_frozen(inode->i_sb, SB_FREEZE_WRITE);
	current->backing_dev_info = inode->i_mapping->backing_dev_info;
	err = generic_write_checks(file, &pos, &count, S_ISBLK(inode->i_mode));
	if (err)
		goto out_nolock;
	if (count == 0)
		goto out_nolock;
#ifdef REMOVE_SUID_PATH
	err = remove_suid(&file->f_path);
#else
	err = remove_suid(fdentry(file));
#endif
	if (err)
		goto out_nolock;
	file_update_time(file);

	pages = kmalloc(nrptrs * sizeof(struct page *), GFP_KERNEL);

	mutex_lock(&inode->i_mutex);
	first_index = pos >> PAGE_CACHE_SHIFT;
	last_index = (pos + count) >> PAGE_CACHE_SHIFT;

	/*
	 * if this is a nodatasum mount, force summing off for the inode
	 * all the time.  That way a later mount with summing on won't
	 * get confused
	 */
	if (btrfs_test_opt(root, NODATASUM))
		btrfs_set_flag(inode, NODATASUM);

	/*
	 * there are lots of better ways to do this, but this code
	 * makes sure the first and last page in the file range are
	 * up to date and ready for cow
	 */
	if ((pos & (PAGE_CACHE_SIZE - 1))) {
		pinned[0] = grab_cache_page(inode->i_mapping, first_index);
		if (!PageUptodate(pinned[0])) {
			ret = btrfs_readpage(NULL, pinned[0]);
			BUG_ON(ret);
			wait_on_page_locked(pinned[0]);
		} else {
			unlock_page(pinned[0]);
		}
	}
	if ((pos + count) & (PAGE_CACHE_SIZE - 1)) {
		pinned[1] = grab_cache_page(inode->i_mapping, last_index);
		if (!PageUptodate(pinned[1])) {
			ret = btrfs_readpage(NULL, pinned[1]);
			BUG_ON(ret);
			wait_on_page_locked(pinned[1]);
		} else {
			unlock_page(pinned[1]);
		}
	}

	while(count > 0) {
		size_t offset = pos & (PAGE_CACHE_SIZE - 1);
		size_t write_bytes = min(count, nrptrs *
					(size_t)PAGE_CACHE_SIZE -
					 offset);
		size_t num_pages = (write_bytes + PAGE_CACHE_SIZE - 1) >>
					PAGE_CACHE_SHIFT;

		WARN_ON(num_pages > nrptrs);
		memset(pages, 0, sizeof(pages));

		ret = btrfs_check_free_space(root, write_bytes, 0);
		if (ret)
			goto out;

		ret = prepare_pages(root, file, pages, num_pages,
				    pos, first_index, last_index,
				    write_bytes);
		if (ret)
			goto out;

		ret = btrfs_copy_from_user(pos, num_pages,
					   write_bytes, pages, buf);
		if (ret) {
			btrfs_drop_pages(pages, num_pages);
			goto out;
		}

		ret = dirty_and_release_pages(NULL, root, file, pages,
					      num_pages, pos, write_bytes);
		btrfs_drop_pages(pages, num_pages);
		if (ret)
			goto out;

		buf += write_bytes;
		count -= write_bytes;
		pos += write_bytes;
		num_written += write_bytes;

		balance_dirty_pages_ratelimited_nr(inode->i_mapping, num_pages);
		if (num_pages < (root->leafsize >> PAGE_CACHE_SHIFT) + 1)
			btrfs_btree_balance_dirty(root, 1);
		cond_resched();
	}
out:
	mutex_unlock(&inode->i_mutex);

out_nolock:
	kfree(pages);
	if (pinned[0])
		page_cache_release(pinned[0]);
	if (pinned[1])
		page_cache_release(pinned[1]);
	*ppos = pos;

	if (num_written > 0 && ((file->f_flags & O_SYNC) || IS_SYNC(inode))) {
		err = sync_page_range(inode, inode->i_mapping,
				      start_pos, num_written);
		if (err < 0)
			num_written = err;
	} else if (num_written > 0 && (file->f_flags & O_DIRECT)) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
		do_sync_file_range(file, start_pos,
				      start_pos + num_written - 1,
				      SYNC_FILE_RANGE_WRITE |
				      SYNC_FILE_RANGE_WAIT_AFTER);
#else
		do_sync_mapping_range(inode->i_mapping, start_pos,
				      start_pos + num_written - 1,
				      SYNC_FILE_RANGE_WRITE |
				      SYNC_FILE_RANGE_WAIT_AFTER);
#endif
		invalidate_mapping_pages(inode->i_mapping,
		      start_pos >> PAGE_CACHE_SHIFT,
		     (start_pos + num_written - 1) >> PAGE_CACHE_SHIFT);
	}
	current->backing_dev_info = NULL;
	return num_written ? num_written : err;
}

int btrfs_release_file(struct inode * inode, struct file * filp)
{
	if (filp->private_data)
		btrfs_ioctl_trans_end(filp);
	return 0;
}

static int btrfs_sync_file(struct file *file,
			   struct dentry *dentry, int datasync)
{
	struct inode *inode = dentry->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret = 0;
	struct btrfs_trans_handle *trans;

	/*
	 * check the transaction that last modified this inode
	 * and see if its already been committed
	 */
	if (!BTRFS_I(inode)->last_trans)
		goto out;

	mutex_lock(&root->fs_info->trans_mutex);
	if (BTRFS_I(inode)->last_trans <=
	    root->fs_info->last_trans_committed) {
		BTRFS_I(inode)->last_trans = 0;
		mutex_unlock(&root->fs_info->trans_mutex);
		goto out;
	}
	mutex_unlock(&root->fs_info->trans_mutex);

	/*
	 * ok we haven't committed the transaction yet, lets do a commit
	 */
	if (file->private_data)
		btrfs_ioctl_trans_end(file);

	trans = btrfs_start_transaction(root, 1);
	if (!trans) {
		ret = -ENOMEM;
		goto out;
	}
	ret = btrfs_commit_transaction(trans, root);
out:
	return ret > 0 ? EIO : ret;
}

static struct vm_operations_struct btrfs_file_vm_ops = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
	.nopage         = filemap_nopage,
	.populate       = filemap_populate,
#else
	.fault		= filemap_fault,
#endif
	.page_mkwrite	= btrfs_page_mkwrite,
};

static int btrfs_file_mmap(struct file	*filp, struct vm_area_struct *vma)
{
	vma->vm_ops = &btrfs_file_vm_ops;
	file_accessed(filp);
	return 0;
}

struct file_operations btrfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.aio_read       = generic_file_aio_read,
	.splice_read	= generic_file_splice_read,
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)
	.sendfile	= generic_file_sendfile,
#endif
	.write		= btrfs_file_write,
	.mmap		= btrfs_file_mmap,
	.open		= generic_file_open,
	.release	= btrfs_release_file,
	.fsync		= btrfs_sync_file,
	.unlocked_ioctl	= btrfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= btrfs_ioctl,
#endif
};

