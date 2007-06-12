#include <linux/module.h>
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

static int dirty_and_release_pages(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct file *file,
				   struct page **pages,
				   size_t num_pages,
				   loff_t pos,
				   size_t write_bytes)
{
	int i;
	int offset;
	int err = 0;
	int ret;
	int this_write;
	struct inode *inode = file->f_path.dentry->d_inode;
	struct buffer_head *bh;
	struct btrfs_file_extent_item *ei;

	for (i = 0; i < num_pages; i++) {
		offset = pos & (PAGE_CACHE_SIZE -1);
		this_write = min(PAGE_CACHE_SIZE - offset, write_bytes);
		/* FIXME, one block at a time */

		mutex_lock(&root->fs_info->fs_mutex);
		trans = btrfs_start_transaction(root, 1);
		btrfs_set_trans_block_group(trans, inode);

		bh = page_buffers(pages[i]);

		if (buffer_mapped(bh) && bh->b_blocknr == 0) {
			struct btrfs_key key;
			struct btrfs_path *path;
			char *ptr;
			u32 datasize;

			/* create an inline extent, and copy the data in */
			path = btrfs_alloc_path();
			BUG_ON(!path);
			key.objectid = inode->i_ino;
			key.offset = pages[i]->index << PAGE_CACHE_SHIFT;
			key.flags = 0;
			btrfs_set_key_type(&key, BTRFS_EXTENT_DATA_KEY);
			BUG_ON(write_bytes >= PAGE_CACHE_SIZE);
			datasize = offset +
				btrfs_file_extent_calc_inline_size(write_bytes);

			ret = btrfs_insert_empty_item(trans, root, path, &key,
						      datasize);
			BUG_ON(ret);
			ei = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]),
			       path->slots[0], struct btrfs_file_extent_item);
			btrfs_set_file_extent_generation(ei, trans->transid);
			btrfs_set_file_extent_type(ei,
						   BTRFS_FILE_EXTENT_INLINE);
			ptr = btrfs_file_extent_inline_start(ei);
			btrfs_memcpy(root, path->nodes[0]->b_data,
				     ptr, bh->b_data, offset + write_bytes);
			mark_buffer_dirty(path->nodes[0]);
			btrfs_free_path(path);
		} else if (buffer_mapped(bh)) {
			/* csum the file data */
			btrfs_csum_file_block(trans, root, inode->i_ino,
				      pages[i]->index << PAGE_CACHE_SHIFT,
				      kmap(pages[i]), PAGE_CACHE_SIZE);
			kunmap(pages[i]);
		}
		SetPageChecked(pages[i]);
		ret = btrfs_end_transaction(trans, root);
		BUG_ON(ret);
		mutex_unlock(&root->fs_info->fs_mutex);

		ret = btrfs_commit_write(file, pages[i], offset,
					 offset + this_write);
		pos += this_write;
		if (ret) {
			err = ret;
			goto failed;
		}
		WARN_ON(this_write > write_bytes);
		write_bytes -= this_write;
	}
failed:
	return err;
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

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	while(1) {
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
		keep = 0;
		bookend = 0;
		found_extent = 0;
		found_inline = 0;
		extent = NULL;
		leaf = btrfs_buffer_leaf(path->nodes[0]);
		slot = path->slots[0];
		btrfs_disk_key_to_cpu(&key, &leaf->items[slot].key);
		if (key.offset >= end || key.objectid != inode->i_ino) {
			ret = 0;
			goto out;
		}
		if (btrfs_key_type(&key) != BTRFS_EXTENT_DATA_KEY) {
			ret = 0;
			goto out;
		}
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
			     btrfs_file_extent_inline_len(leaf->items + slot);
		}

		/* we found nothing we can drop */
		if (!found_extent && !found_inline) {
			ret = 0;
			goto out;
		}

		/* we found nothing inside the range */
		if (search_start >= extent_end) {
			ret = 0;
			goto out;
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
				mark_buffer_dirty(path->nodes[0]);
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
			 size_t write_bytes,
			 u64 alloc_extent_start)
{
	int i;
	unsigned long index = pos >> PAGE_CACHE_SHIFT;
	struct inode *inode = file->f_path.dentry->d_inode;
	int offset;
	int err = 0;
	int this_write;
	struct buffer_head *bh;
	struct buffer_head *head;
	loff_t isize = i_size_read(inode);

	memset(pages, 0, num_pages * sizeof(struct page *));

	for (i = 0; i < num_pages; i++) {
		pages[i] = grab_cache_page(inode->i_mapping, index + i);
		if (!pages[i]) {
			err = -ENOMEM;
			goto failed_release;
		}
		cancel_dirty_page(pages[i], PAGE_CACHE_SIZE);
		wait_on_page_writeback(pages[i]);
		offset = pos & (PAGE_CACHE_SIZE -1);
		this_write = min(PAGE_CACHE_SIZE - offset, write_bytes);
		if (!page_has_buffers(pages[i])) {
			create_empty_buffers(pages[i],
					     root->fs_info->sb->s_blocksize,
					     (1 << BH_Uptodate));
		}
		head = page_buffers(pages[i]);
		bh = head;
		do {
			err = btrfs_map_bh_to_logical(root, bh,
						      alloc_extent_start);
			BUG_ON(err);
			if (err)
				goto failed_truncate;
			bh = bh->b_this_page;
			if (alloc_extent_start)
				alloc_extent_start++;
		} while (bh != head);
		pos += this_write;
		WARN_ON(this_write > write_bytes);
		write_bytes -= this_write;
	}
	return 0;

failed_release:
	btrfs_drop_pages(pages, num_pages);
	return err;

failed_truncate:
	btrfs_drop_pages(pages, num_pages);
	if (pos > isize)
		vmtruncate(inode, isize);
	return err;
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
	struct page *pages[8];
	struct page *pinned[2];
	unsigned long first_index;
	unsigned long last_index;
	u64 start_pos;
	u64 num_blocks;
	u64 alloc_extent_start;
	u64 hint_block;
	struct btrfs_trans_handle *trans;
	struct btrfs_key ins;
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

	start_pos = pos & ~((u64)PAGE_CACHE_SIZE - 1);
	num_blocks = (count + pos - start_pos + root->blocksize - 1) >>
			inode->i_blkbits;

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
			ret = mpage_readpage(pinned[0], btrfs_get_block);
			BUG_ON(ret);
			wait_on_page_locked(pinned[0]);
		} else {
			unlock_page(pinned[0]);
		}
	}
	if ((pos + count) & (PAGE_CACHE_SIZE - 1)) {
		pinned[1] = grab_cache_page(inode->i_mapping, last_index);
		if (!PageUptodate(pinned[1])) {
			ret = mpage_readpage(pinned[1], btrfs_get_block);
			BUG_ON(ret);
			wait_on_page_locked(pinned[1]);
		} else {
			unlock_page(pinned[1]);
		}
	}

	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	if (!trans) {
		err = -ENOMEM;
		mutex_unlock(&root->fs_info->fs_mutex);
		goto out_unlock;
	}
	btrfs_set_trans_block_group(trans, inode);
	/* FIXME blocksize != 4096 */
	inode->i_blocks += num_blocks << 3;
	hint_block = 0;

	/* FIXME...EIEIO, ENOSPC and more */

	/* step one, delete the existing extents in this range */
	if (start_pos < inode->i_size) {
		/* FIXME blocksize != pagesize */
		ret = btrfs_drop_extents(trans, root, inode,
					 start_pos,
					 (pos + count + root->blocksize -1) &
					 ~((u64)root->blocksize - 1),
					 &hint_block);
		BUG_ON(ret);
	}

	/* insert any holes we need to create */
	if (inode->i_size < start_pos) {
		u64 last_pos_in_file;
		u64 hole_size;
		u64 mask = root->blocksize - 1;
		last_pos_in_file = (inode->i_size + mask) & ~mask;
		hole_size = (start_pos - last_pos_in_file + mask) & ~mask;
		hole_size >>= inode->i_blkbits;
		if (last_pos_in_file < start_pos) {
			ret = btrfs_insert_file_extent(trans, root,
						       inode->i_ino,
						       last_pos_in_file,
						       0, 0, hole_size);
		}
		BUG_ON(ret);
	}

	/*
	 * either allocate an extent for the new bytes or setup the key
	 * to show we are doing inline data in the extent
	 */
	if (inode->i_size >= PAGE_CACHE_SIZE || pos + count < inode->i_size ||
	    pos + count - start_pos > BTRFS_MAX_INLINE_DATA_SIZE(root)) {
		ret = btrfs_alloc_extent(trans, root, inode->i_ino,
					 num_blocks, hint_block, (u64)-1,
					 &ins, 1);
		BUG_ON(ret);
		ret = btrfs_insert_file_extent(trans, root, inode->i_ino,
				       start_pos, ins.objectid, ins.offset,
				       ins.offset);
		BUG_ON(ret);
	} else {
		ins.offset = 0;
		ins.objectid = 0;
	}
	BUG_ON(ret);
	alloc_extent_start = ins.objectid;
	ret = btrfs_end_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);

	while(count > 0) {
		size_t offset = pos & (PAGE_CACHE_SIZE - 1);
		size_t write_bytes = min(count, PAGE_CACHE_SIZE - offset);
		size_t num_pages = (write_bytes + PAGE_CACHE_SIZE - 1) >>
					PAGE_CACHE_SHIFT;

		memset(pages, 0, sizeof(pages));
		ret = prepare_pages(root, file, pages, num_pages,
				    pos, first_index, last_index,
				    write_bytes, alloc_extent_start);
		BUG_ON(ret);

		/* FIXME blocks != pagesize */
		if (alloc_extent_start)
			alloc_extent_start += num_pages;
		ret = btrfs_copy_from_user(pos, num_pages,
					   write_bytes, pages, buf);
		BUG_ON(ret);

		ret = dirty_and_release_pages(NULL, root, file, pages,
					      num_pages, pos, write_bytes);
		BUG_ON(ret);
		btrfs_drop_pages(pages, num_pages);

		buf += write_bytes;
		count -= write_bytes;
		pos += write_bytes;
		num_written += write_bytes;

		balance_dirty_pages_ratelimited(inode->i_mapping);
		btrfs_btree_balance_dirty(root);
		cond_resched();
	}
out_unlock:
	mutex_unlock(&inode->i_mutex);
out:
	if (pinned[0])
		page_cache_release(pinned[0]);
	if (pinned[1])
		page_cache_release(pinned[1]);
	*ppos = pos;
	current->backing_dev_info = NULL;
	mark_inode_dirty(inode);
	return num_written ? num_written : err;
}

/*
 * FIXME, do this by stuffing the csum we want in the info hanging off
 * page->private.  For now, verify file csums on read
 */
static int btrfs_read_actor(read_descriptor_t *desc, struct page *page,
			unsigned long offset, unsigned long size)
{
	char *kaddr;
	unsigned long left, count = desc->count;
	struct inode *inode = page->mapping->host;

	if (size > count)
		size = count;

	if (!PageChecked(page)) {
		/* FIXME, do it per block */
		struct btrfs_root *root = BTRFS_I(inode)->root;
		int ret;
		struct buffer_head *bh;

		if (page_has_buffers(page)) {
			bh = page_buffers(page);
			if (!buffer_mapped(bh)) {
				SetPageChecked(page);
				goto checked;
			}
		}

		ret = btrfs_csum_verify_file_block(root,
				  page->mapping->host->i_ino,
				  page->index << PAGE_CACHE_SHIFT,
				  kmap(page), PAGE_CACHE_SIZE);
		if (ret) {
			if (ret != -ENOENT) {
				printk("failed to verify ino %lu page %lu ret %d\n",
				       page->mapping->host->i_ino,
				       page->index, ret);
				memset(page_address(page), 1, PAGE_CACHE_SIZE);
				flush_dcache_page(page);
			}
		}
		SetPageChecked(page);
		kunmap(page);
	}
checked:
	/*
	 * Faults on the destination of a read are common, so do it before
	 * taking the kmap.
	 */
	if (!fault_in_pages_writeable(desc->arg.buf, size)) {
		kaddr = kmap_atomic(page, KM_USER0);
		left = __copy_to_user_inatomic(desc->arg.buf,
						kaddr + offset, size);
		kunmap_atomic(kaddr, KM_USER0);
		if (left == 0)
			goto success;
	}

	/* Do it the slow way */
	kaddr = kmap(page);
	left = __copy_to_user(desc->arg.buf, kaddr + offset, size);
	kunmap(page);

	if (left) {
		size -= left;
		desc->error = -EFAULT;
	}
success:
	desc->count = count - size;
	desc->written += size;
	desc->arg.buf += size;
	return size;
}

/**
 * btrfs_file_aio_read - filesystem read routine, with a mod to csum verify
 * @iocb:	kernel I/O control block
 * @iov:	io vector request
 * @nr_segs:	number of segments in the iovec
 * @pos:	current file position
 */
static ssize_t btrfs_file_aio_read(struct kiocb *iocb, const struct iovec *iov,
				   unsigned long nr_segs, loff_t pos)
{
	struct file *filp = iocb->ki_filp;
	ssize_t retval;
	unsigned long seg;
	size_t count;
	loff_t *ppos = &iocb->ki_pos;

	count = 0;
	for (seg = 0; seg < nr_segs; seg++) {
		const struct iovec *iv = &iov[seg];

		/*
		 * If any segment has a negative length, or the cumulative
		 * length ever wraps negative then return -EINVAL.
		 */
		count += iv->iov_len;
		if (unlikely((ssize_t)(count|iv->iov_len) < 0))
			return -EINVAL;
		if (access_ok(VERIFY_WRITE, iv->iov_base, iv->iov_len))
			continue;
		if (seg == 0)
			return -EFAULT;
		nr_segs = seg;
		count -= iv->iov_len;	/* This segment is no good */
		break;
	}
	retval = 0;
	if (count) {
		for (seg = 0; seg < nr_segs; seg++) {
			read_descriptor_t desc;

			desc.written = 0;
			desc.arg.buf = iov[seg].iov_base;
			desc.count = iov[seg].iov_len;
			if (desc.count == 0)
				continue;
			desc.error = 0;
			do_generic_file_read(filp, ppos, &desc,
					     btrfs_read_actor);
			retval += desc.written;
			if (desc.error) {
				retval = retval ?: desc.error;
				break;
			}
		}
	}
	return retval;
}

static int btrfs_sync_file(struct file *file,
			   struct dentry *dentry, int datasync)
{
	struct inode *inode = dentry->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret;
	struct btrfs_trans_handle *trans;

	/*
	 * FIXME, use inode generation number to check if we can skip the
	 * commit
	 */
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	if (!trans) {
		ret = -ENOMEM;
		goto out;
	}
	ret = btrfs_commit_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);
out:
	return ret > 0 ? EIO : ret;
}

struct file_operations btrfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.aio_read       = btrfs_file_aio_read,
	.write		= btrfs_file_write,
	.mmap		= generic_file_mmap,
	.open		= generic_file_open,
	.ioctl		= btrfs_ioctl,
	.fsync		= btrfs_sync_file,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= btrfs_compat_ioctl,
#endif
};

