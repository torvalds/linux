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

#include <linux/buffer_head.h>
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
		unlock_page(pages[i]);
		mark_page_accessed(pages[i]);
		page_cache_release(pages[i]);
	}
}

static int insert_inline_extent(struct btrfs_trans_handle *trans,
				struct btrfs_root *root, struct inode *inode,
				u64 offset, ssize_t size,
				struct page *page, size_t page_offset)
{
	struct btrfs_key key;
	struct btrfs_path *path;
	char *ptr, *kaddr;
	struct btrfs_file_extent_item *ei;
	u32 datasize;
	int err = 0;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	btrfs_set_trans_block_group(trans, inode);

	key.objectid = inode->i_ino;
	key.offset = offset;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_EXTENT_DATA_KEY);
	BUG_ON(size >= PAGE_CACHE_SIZE);
	datasize = btrfs_file_extent_calc_inline_size(size);

	ret = btrfs_insert_empty_item(trans, root, path, &key,
				      datasize);
	if (ret) {
		err = ret;
		goto fail;
	}
	ei = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]),
	       path->slots[0], struct btrfs_file_extent_item);
	btrfs_set_file_extent_generation(ei, trans->transid);
	btrfs_set_file_extent_type(ei,
				   BTRFS_FILE_EXTENT_INLINE);
	ptr = btrfs_file_extent_inline_start(ei);

	kaddr = kmap_atomic(page, KM_USER0);
	btrfs_memcpy(root, path->nodes[0]->b_data,
		     ptr, kaddr + page_offset, size);
	kunmap_atomic(kaddr, KM_USER0);
	btrfs_mark_buffer_dirty(path->nodes[0]);
fail:
	btrfs_free_path(path);
	return err;
}

static int dirty_and_release_pages(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct file *file,
				   struct page **pages,
				   size_t num_pages,
				   loff_t pos,
				   size_t write_bytes)
{
	int err = 0;
	int i;
	struct inode *inode = file->f_path.dentry->d_inode;
	struct extent_map *em;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	u64 hint_block;
	u64 num_blocks;
	u64 start_pos;
	u64 end_of_last_block;
	u64 end_pos = pos + write_bytes;
	loff_t isize = i_size_read(inode);

	em = alloc_extent_map(GFP_NOFS);
	if (!em)
		return -ENOMEM;

	em->bdev = inode->i_sb->s_bdev;

	start_pos = pos & ~((u64)root->blocksize - 1);
	num_blocks = (write_bytes + pos - start_pos + root->blocksize - 1) >>
			inode->i_blkbits;

	down_read(&BTRFS_I(inode)->root->snap_sem);
	end_of_last_block = start_pos + (num_blocks << inode->i_blkbits) - 1;
	lock_extent(em_tree, start_pos, end_of_last_block, GFP_NOFS);
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	if (!trans) {
		err = -ENOMEM;
		goto out_unlock;
	}
	btrfs_set_trans_block_group(trans, inode);
	inode->i_blocks += num_blocks << 3;
	hint_block = 0;

	if ((end_of_last_block & 4095) == 0) {
		printk("strange end of last %Lu %zu %Lu\n", start_pos, write_bytes, end_of_last_block);
	}
	set_extent_uptodate(em_tree, start_pos, end_of_last_block, GFP_NOFS);

	/* FIXME...EIEIO, ENOSPC and more */

	/* insert any holes we need to create */
	if (inode->i_size < start_pos) {
		u64 last_pos_in_file;
		u64 hole_size;
		u64 mask = root->blocksize - 1;
		last_pos_in_file = (isize + mask) & ~mask;
		hole_size = (start_pos - last_pos_in_file + mask) & ~mask;

		if (last_pos_in_file < start_pos) {
			err = btrfs_drop_extents(trans, root, inode,
						 last_pos_in_file,
						 last_pos_in_file + hole_size,
						 &hint_block);
			if (err)
				goto failed;

			hole_size >>= inode->i_blkbits;
			err = btrfs_insert_file_extent(trans, root,
						       inode->i_ino,
						       last_pos_in_file,
						       0, 0, hole_size);
		}
		if (err)
			goto failed;
	}

	/*
	 * either allocate an extent for the new bytes or setup the key
	 * to show we are doing inline data in the extent
	 */
	if (isize >= PAGE_CACHE_SIZE || pos + write_bytes < inode->i_size ||
	    pos + write_bytes - start_pos > BTRFS_MAX_INLINE_DATA_SIZE(root)) {
		u64 last_end;
		for (i = 0; i < num_pages; i++) {
			struct page *p = pages[i];
			SetPageUptodate(p);
			set_page_dirty(p);
		}
		last_end = pages[num_pages -1]->index << PAGE_CACHE_SHIFT;
		last_end += PAGE_CACHE_SIZE - 1;
		set_extent_delalloc(em_tree, start_pos, end_of_last_block,
				 GFP_NOFS);
	} else {
		struct page *p = pages[0];
		/* step one, delete the existing extents in this range */
		/* FIXME blocksize != pagesize */
		err = btrfs_drop_extents(trans, root, inode, start_pos,
			 (pos + write_bytes + root->blocksize -1) &
			 ~((u64)root->blocksize - 1), &hint_block);
		if (err)
			goto failed;

		err = insert_inline_extent(trans, root, inode, start_pos,
					   end_pos - start_pos, p, 0);
		BUG_ON(err);
		em->start = start_pos;
		em->end = end_pos - 1;
		em->block_start = EXTENT_MAP_INLINE;
		em->block_end = EXTENT_MAP_INLINE;
		add_extent_mapping(em_tree, em);
	}
	if (end_pos > isize) {
		i_size_write(inode, end_pos);
		btrfs_update_inode(trans, root, inode);
	}
failed:
	err = btrfs_end_transaction(trans, root);
out_unlock:
	mutex_unlock(&root->fs_info->fs_mutex);
	unlock_extent(em_tree, start_pos, end_of_last_block, GFP_NOFS);
	free_extent_map(em);
	up_read(&BTRFS_I(inode)->root->snap_sem);
	return err;
}

int btrfs_drop_extent_cache(struct inode *inode, u64 start, u64 end)
{
	struct extent_map *em;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;

	while(1) {
		em = lookup_extent_mapping(em_tree, start, end);
		if (!em)
			break;
		remove_extent_mapping(em_tree, em);
		/* once for us */
		free_extent_map(em);
		/* once for the tree*/
		free_extent_map(em);
	}
	return 0;
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
		       u64 start, u64 end, u64 *hint_block)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_leaf *leaf;
	int slot;
	struct btrfs_file_extent_item *extent;
	u64 extent_end = 0;
	int keep;
	struct btrfs_file_extent_item old;
	struct btrfs_path *path;
	u64 search_start = start;
	int bookend;
	int found_type;
	int found_extent;
	int found_inline;
	int recow;

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
		leaf = btrfs_buffer_leaf(path->nodes[0]);
		slot = path->slots[0];
		ret = 0;
		btrfs_disk_key_to_cpu(&key, &leaf->items[slot].key);
		if (key.offset >= end || key.objectid != inode->i_ino) {
			goto out;
		}
		if (btrfs_key_type(&key) > BTRFS_EXTENT_DATA_KEY) {
			goto out;
		}
		if (recow) {
			search_start = key.offset;
			continue;
		}
		if (btrfs_key_type(&key) == BTRFS_EXTENT_DATA_KEY) {
			extent = btrfs_item_ptr(leaf, slot,
						struct btrfs_file_extent_item);
			found_type = btrfs_file_extent_type(extent);
			if (found_type == BTRFS_FILE_EXTENT_REG) {
				extent_end = key.offset +
					(btrfs_file_extent_num_blocks(extent) <<
					 inode->i_blkbits);
				found_extent = 1;
			} else if (found_type == BTRFS_FILE_EXTENT_INLINE) {
				found_inline = 1;
				extent_end = key.offset +
				     btrfs_file_extent_inline_len(leaf->items +
								  slot);
			}
		} else {
			extent_end = search_start;
		}

		/* we found nothing we can drop */
		if ((!found_extent && !found_inline) ||
		    search_start >= extent_end) {
			int nextret;
			u32 nritems;
			nritems = btrfs_header_nritems(
					btrfs_buffer_header(path->nodes[0]));
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

		/* FIXME, there's only one inline extent allowed right now */
		if (found_inline) {
			u64 mask = root->blocksize - 1;
			search_start = (extent_end + mask) & ~mask;
		} else
			search_start = extent_end;

		if (end < extent_end && end >= key.offset) {
			if (found_extent) {
				u64 disk_blocknr =
					btrfs_file_extent_disk_blocknr(extent);
				u64 disk_num_blocks =
				      btrfs_file_extent_disk_num_blocks(extent);
				memcpy(&old, extent, sizeof(old));
				if (disk_blocknr != 0) {
					ret = btrfs_inc_extent_ref(trans, root,
					         disk_blocknr, disk_num_blocks);
					BUG_ON(ret);
				}
			}
			WARN_ON(found_inline);
			bookend = 1;
		}
		/* truncate existing extent */
		if (start > key.offset) {
			u64 new_num;
			u64 old_num;
			keep = 1;
			WARN_ON(start & (root->blocksize - 1));
			if (found_extent) {
				new_num = (start - key.offset) >>
					inode->i_blkbits;
				old_num = btrfs_file_extent_num_blocks(extent);
				*hint_block =
					btrfs_file_extent_disk_blocknr(extent);
				if (btrfs_file_extent_disk_blocknr(extent)) {
					inode->i_blocks -=
						(old_num - new_num) << 3;
				}
				btrfs_set_file_extent_num_blocks(extent,
								 new_num);
				btrfs_mark_buffer_dirty(path->nodes[0]);
			} else {
				WARN_ON(1);
			}
		}
		/* delete the entire extent */
		if (!keep) {
			u64 disk_blocknr = 0;
			u64 disk_num_blocks = 0;
			u64 extent_num_blocks = 0;
			if (found_extent) {
				disk_blocknr =
				      btrfs_file_extent_disk_blocknr(extent);
				disk_num_blocks =
				      btrfs_file_extent_disk_num_blocks(extent);
				extent_num_blocks =
				      btrfs_file_extent_num_blocks(extent);
				*hint_block =
					btrfs_file_extent_disk_blocknr(extent);
			}
			ret = btrfs_del_item(trans, root, path);
			/* TODO update progress marker and return */
			BUG_ON(ret);
			btrfs_release_path(root, path);
			extent = NULL;
			if (found_extent && disk_blocknr != 0) {
				inode->i_blocks -= extent_num_blocks << 3;
				ret = btrfs_free_extent(trans, root,
							disk_blocknr,
							disk_num_blocks, 0);
			}

			BUG_ON(ret);
			if (!bookend && search_start >= end) {
				ret = 0;
				goto out;
			}
			if (!bookend)
				continue;
		}
		/* create bookend, splitting the extent in two */
		if (bookend && found_extent) {
			struct btrfs_key ins;
			ins.objectid = inode->i_ino;
			ins.offset = end;
			ins.flags = 0;
			btrfs_set_key_type(&ins, BTRFS_EXTENT_DATA_KEY);
			btrfs_release_path(root, path);
			ret = btrfs_insert_empty_item(trans, root, path, &ins,
						      sizeof(*extent));

			if (ret) {
				btrfs_print_leaf(root, btrfs_buffer_leaf(path->nodes[0]));
				printk("got %d on inserting %Lu %u %Lu start %Lu end %Lu found %Lu %Lu keep was %d\n", ret , ins.objectid, ins.flags, ins.offset, start, end, key.offset, extent_end, keep);
			}
			BUG_ON(ret);
			extent = btrfs_item_ptr(
				    btrfs_buffer_leaf(path->nodes[0]),
				    path->slots[0],
				    struct btrfs_file_extent_item);
			btrfs_set_file_extent_disk_blocknr(extent,
				    btrfs_file_extent_disk_blocknr(&old));
			btrfs_set_file_extent_disk_num_blocks(extent,
				    btrfs_file_extent_disk_num_blocks(&old));

			btrfs_set_file_extent_offset(extent,
				    btrfs_file_extent_offset(&old) +
				    ((end - key.offset) >> inode->i_blkbits));
			WARN_ON(btrfs_file_extent_num_blocks(&old) <
				(extent_end - end) >> inode->i_blkbits);
			btrfs_set_file_extent_num_blocks(extent,
				    (extent_end - end) >> inode->i_blkbits);

			btrfs_set_file_extent_type(extent,
						   BTRFS_FILE_EXTENT_REG);
			btrfs_set_file_extent_generation(extent,
				    btrfs_file_extent_generation(&old));
			btrfs_mark_buffer_dirty(path->nodes[0]);
			if (btrfs_file_extent_disk_blocknr(&old) != 0) {
				inode->i_blocks +=
				      btrfs_file_extent_num_blocks(extent) << 3;
			}
			ret = 0;
			goto out;
		}
	}
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * this gets pages into the page cache and locks them down
 */
static int prepare_pages(struct btrfs_root *root,
			 struct file *file,
			 struct page **pages,
			 size_t num_pages,
			 loff_t pos,
			 unsigned long first_index,
			 unsigned long last_index,
			 size_t write_bytes)
{
	int i;
	unsigned long index = pos >> PAGE_CACHE_SHIFT;
	struct inode *inode = file->f_path.dentry->d_inode;
	int err = 0;
	u64 num_blocks;
	u64 start_pos;

	start_pos = pos & ~((u64)root->blocksize - 1);
	num_blocks = (write_bytes + pos - start_pos + root->blocksize - 1) >>
			inode->i_blkbits;

	memset(pages, 0, num_pages * sizeof(struct page *));

	for (i = 0; i < num_pages; i++) {
		pages[i] = grab_cache_page(inode->i_mapping, index + i);
		if (!pages[i]) {
			err = -ENOMEM;
			BUG_ON(1);
		}
		cancel_dirty_page(pages[i], PAGE_CACHE_SIZE);
		wait_on_page_writeback(pages[i]);
		set_page_extent_mapped(pages[i]);
		WARN_ON(!PageLocked(pages[i]));
	}
	return 0;
}

static ssize_t btrfs_file_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	loff_t pos;
	size_t num_written = 0;
	int err = 0;
	int ret = 0;
	struct inode *inode = file->f_path.dentry->d_inode;
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
	if (file->f_flags & O_DIRECT)
		return -EINVAL;
	pos = *ppos;
	vfs_check_frozen(inode->i_sb, SB_FREEZE_WRITE);
	current->backing_dev_info = inode->i_mapping->backing_dev_info;
	err = generic_write_checks(file, &pos, &count, S_ISBLK(inode->i_mode));
	if (err)
		goto out;
	if (count == 0)
		goto out;
	err = remove_suid(file->f_path.dentry);
	if (err)
		goto out;
	file_update_time(file);

	pages = kmalloc(nrptrs * sizeof(struct page *), GFP_KERNEL);

	mutex_lock(&inode->i_mutex);
	first_index = pos >> PAGE_CACHE_SHIFT;
	last_index = (pos + count) >> PAGE_CACHE_SHIFT;

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
		btrfs_btree_balance_dirty(root, 1);
		cond_resched();
	}
	mutex_unlock(&inode->i_mutex);
out:
	kfree(pages);
	if (pinned[0])
		page_cache_release(pinned[0]);
	if (pinned[1])
		page_cache_release(pinned[1]);
	*ppos = pos;
	current->backing_dev_info = NULL;
	return num_written ? num_written : err;
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
	mutex_lock(&root->fs_info->fs_mutex);
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
	trans = btrfs_start_transaction(root, 1);
	if (!trans) {
		ret = -ENOMEM;
		goto out;
	}
	ret = btrfs_commit_transaction(trans, root);
out:
	mutex_unlock(&root->fs_info->fs_mutex);
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
	.write		= btrfs_file_write,
	.mmap		= btrfs_file_mmap,
	.open		= generic_file_open,
	.fsync		= btrfs_sync_file,
	.unlocked_ioctl	= btrfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= btrfs_ioctl,
#endif
};

