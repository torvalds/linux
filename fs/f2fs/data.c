/*
 * fs/f2fs/data.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/prefetch.h>

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include <trace/events/f2fs.h>

/*
 * Lock ordering for the change of data block address:
 * ->data_page
 *  ->node_page
 *    update block addresses in the node page
 */
static void __set_data_blkaddr(struct dnode_of_data *dn, block_t new_addr)
{
	struct f2fs_node *rn;
	__le32 *addr_array;
	struct page *node_page = dn->node_page;
	unsigned int ofs_in_node = dn->ofs_in_node;

	wait_on_page_writeback(node_page);

	rn = (struct f2fs_node *)page_address(node_page);

	/* Get physical address of data block */
	addr_array = blkaddr_in_node(rn);
	addr_array[ofs_in_node] = cpu_to_le32(new_addr);
	set_page_dirty(node_page);
}

int reserve_new_block(struct dnode_of_data *dn)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dn->inode->i_sb);

	if (is_inode_flag_set(F2FS_I(dn->inode), FI_NO_ALLOC))
		return -EPERM;
	if (!inc_valid_block_count(sbi, dn->inode, 1))
		return -ENOSPC;

	trace_f2fs_reserve_new_block(dn->inode, dn->nid, dn->ofs_in_node);

	__set_data_blkaddr(dn, NEW_ADDR);
	dn->data_blkaddr = NEW_ADDR;
	sync_inode_page(dn);
	return 0;
}

static int check_extent_cache(struct inode *inode, pgoff_t pgofs,
					struct buffer_head *bh_result)
{
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	pgoff_t start_fofs, end_fofs;
	block_t start_blkaddr;

	read_lock(&fi->ext.ext_lock);
	if (fi->ext.len == 0) {
		read_unlock(&fi->ext.ext_lock);
		return 0;
	}

	sbi->total_hit_ext++;
	start_fofs = fi->ext.fofs;
	end_fofs = fi->ext.fofs + fi->ext.len - 1;
	start_blkaddr = fi->ext.blk_addr;

	if (pgofs >= start_fofs && pgofs <= end_fofs) {
		unsigned int blkbits = inode->i_sb->s_blocksize_bits;
		size_t count;

		clear_buffer_new(bh_result);
		map_bh(bh_result, inode->i_sb,
				start_blkaddr + pgofs - start_fofs);
		count = end_fofs - pgofs + 1;
		if (count < (UINT_MAX >> blkbits))
			bh_result->b_size = (count << blkbits);
		else
			bh_result->b_size = UINT_MAX;

		sbi->read_hit_ext++;
		read_unlock(&fi->ext.ext_lock);
		return 1;
	}
	read_unlock(&fi->ext.ext_lock);
	return 0;
}

void update_extent_cache(block_t blk_addr, struct dnode_of_data *dn)
{
	struct f2fs_inode_info *fi = F2FS_I(dn->inode);
	pgoff_t fofs, start_fofs, end_fofs;
	block_t start_blkaddr, end_blkaddr;

	BUG_ON(blk_addr == NEW_ADDR);
	fofs = start_bidx_of_node(ofs_of_node(dn->node_page)) + dn->ofs_in_node;

	/* Update the page address in the parent node */
	__set_data_blkaddr(dn, blk_addr);

	write_lock(&fi->ext.ext_lock);

	start_fofs = fi->ext.fofs;
	end_fofs = fi->ext.fofs + fi->ext.len - 1;
	start_blkaddr = fi->ext.blk_addr;
	end_blkaddr = fi->ext.blk_addr + fi->ext.len - 1;

	/* Drop and initialize the matched extent */
	if (fi->ext.len == 1 && fofs == start_fofs)
		fi->ext.len = 0;

	/* Initial extent */
	if (fi->ext.len == 0) {
		if (blk_addr != NULL_ADDR) {
			fi->ext.fofs = fofs;
			fi->ext.blk_addr = blk_addr;
			fi->ext.len = 1;
		}
		goto end_update;
	}

	/* Front merge */
	if (fofs == start_fofs - 1 && blk_addr == start_blkaddr - 1) {
		fi->ext.fofs--;
		fi->ext.blk_addr--;
		fi->ext.len++;
		goto end_update;
	}

	/* Back merge */
	if (fofs == end_fofs + 1 && blk_addr == end_blkaddr + 1) {
		fi->ext.len++;
		goto end_update;
	}

	/* Split the existing extent */
	if (fi->ext.len > 1 &&
		fofs >= start_fofs && fofs <= end_fofs) {
		if ((end_fofs - fofs) < (fi->ext.len >> 1)) {
			fi->ext.len = fofs - start_fofs;
		} else {
			fi->ext.fofs = fofs + 1;
			fi->ext.blk_addr = start_blkaddr +
					fofs - start_fofs + 1;
			fi->ext.len -= fofs - start_fofs + 1;
		}
		goto end_update;
	}
	write_unlock(&fi->ext.ext_lock);
	return;

end_update:
	write_unlock(&fi->ext.ext_lock);
	sync_inode_page(dn);
	return;
}

struct page *find_data_page(struct inode *inode, pgoff_t index)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct address_space *mapping = inode->i_mapping;
	struct dnode_of_data dn;
	struct page *page;
	int err;

	page = find_get_page(mapping, index);
	if (page && PageUptodate(page))
		return page;
	f2fs_put_page(page, 0);

	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = get_dnode_of_data(&dn, index, LOOKUP_NODE);
	if (err)
		return ERR_PTR(err);
	f2fs_put_dnode(&dn);

	if (dn.data_blkaddr == NULL_ADDR)
		return ERR_PTR(-ENOENT);

	/* By fallocate(), there is no cached page, but with NEW_ADDR */
	if (dn.data_blkaddr == NEW_ADDR)
		return ERR_PTR(-EINVAL);

	page = grab_cache_page(mapping, index);
	if (!page)
		return ERR_PTR(-ENOMEM);

	if (PageUptodate(page)) {
		unlock_page(page);
		return page;
	}

	err = f2fs_readpage(sbi, page, dn.data_blkaddr, READ_SYNC);
	wait_on_page_locked(page);
	if (!PageUptodate(page)) {
		f2fs_put_page(page, 0);
		return ERR_PTR(-EIO);
	}
	return page;
}

/*
 * If it tries to access a hole, return an error.
 * Because, the callers, functions in dir.c and GC, should be able to know
 * whether this page exists or not.
 */
struct page *get_lock_data_page(struct inode *inode, pgoff_t index)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct address_space *mapping = inode->i_mapping;
	struct dnode_of_data dn;
	struct page *page;
	int err;

	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = get_dnode_of_data(&dn, index, LOOKUP_NODE);
	if (err)
		return ERR_PTR(err);
	f2fs_put_dnode(&dn);

	if (dn.data_blkaddr == NULL_ADDR)
		return ERR_PTR(-ENOENT);

	page = grab_cache_page(mapping, index);
	if (!page)
		return ERR_PTR(-ENOMEM);

	if (PageUptodate(page))
		return page;

	BUG_ON(dn.data_blkaddr == NEW_ADDR);
	BUG_ON(dn.data_blkaddr == NULL_ADDR);

	err = f2fs_readpage(sbi, page, dn.data_blkaddr, READ_SYNC);
	if (err)
		return ERR_PTR(err);

	lock_page(page);
	if (!PageUptodate(page)) {
		f2fs_put_page(page, 1);
		return ERR_PTR(-EIO);
	}
	return page;
}

/*
 * Caller ensures that this data page is never allocated.
 * A new zero-filled data page is allocated in the page cache.
 *
 * Also, caller should grab and release a mutex by calling mutex_lock_op() and
 * mutex_unlock_op().
 */
struct page *get_new_data_page(struct inode *inode, pgoff_t index,
						bool new_i_size)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct address_space *mapping = inode->i_mapping;
	struct page *page;
	struct dnode_of_data dn;
	int err;

	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = get_dnode_of_data(&dn, index, ALLOC_NODE);
	if (err)
		return ERR_PTR(err);

	if (dn.data_blkaddr == NULL_ADDR) {
		if (reserve_new_block(&dn)) {
			f2fs_put_dnode(&dn);
			return ERR_PTR(-ENOSPC);
		}
	}
	f2fs_put_dnode(&dn);

	page = grab_cache_page(mapping, index);
	if (!page)
		return ERR_PTR(-ENOMEM);

	if (PageUptodate(page))
		return page;

	if (dn.data_blkaddr == NEW_ADDR) {
		zero_user_segment(page, 0, PAGE_CACHE_SIZE);
		SetPageUptodate(page);
	} else {
		err = f2fs_readpage(sbi, page, dn.data_blkaddr, READ_SYNC);
		if (err)
			return ERR_PTR(err);
		lock_page(page);
		if (!PageUptodate(page)) {
			f2fs_put_page(page, 1);
			return ERR_PTR(-EIO);
		}
	}

	if (new_i_size &&
		i_size_read(inode) < ((index + 1) << PAGE_CACHE_SHIFT)) {
		i_size_write(inode, ((index + 1) << PAGE_CACHE_SHIFT));
		mark_inode_dirty_sync(inode);
	}
	return page;
}

static void read_end_io(struct bio *bio, int err)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;

	do {
		struct page *page = bvec->bv_page;

		if (--bvec >= bio->bi_io_vec)
			prefetchw(&bvec->bv_page->flags);

		if (uptodate) {
			SetPageUptodate(page);
		} else {
			ClearPageUptodate(page);
			SetPageError(page);
		}
		unlock_page(page);
	} while (bvec >= bio->bi_io_vec);
	kfree(bio->bi_private);
	bio_put(bio);
}

/*
 * Fill the locked page with data located in the block address.
 * Return unlocked page.
 */
int f2fs_readpage(struct f2fs_sb_info *sbi, struct page *page,
					block_t blk_addr, int type)
{
	struct block_device *bdev = sbi->sb->s_bdev;
	struct bio *bio;

	trace_f2fs_readpage(page, blk_addr, type);

	down_read(&sbi->bio_sem);

	/* Allocate a new bio */
	bio = f2fs_bio_alloc(bdev, 1);

	/* Initialize the bio */
	bio->bi_sector = SECTOR_FROM_BLOCK(sbi, blk_addr);
	bio->bi_end_io = read_end_io;

	if (bio_add_page(bio, page, PAGE_CACHE_SIZE, 0) < PAGE_CACHE_SIZE) {
		kfree(bio->bi_private);
		bio_put(bio);
		up_read(&sbi->bio_sem);
		f2fs_put_page(page, 1);
		return -EFAULT;
	}

	submit_bio(type, bio);
	up_read(&sbi->bio_sem);
	return 0;
}

/*
 * This function should be used by the data read flow only where it
 * does not check the "create" flag that indicates block allocation.
 * The reason for this special functionality is to exploit VFS readahead
 * mechanism.
 */
static int get_data_block_ro(struct inode *inode, sector_t iblock,
			struct buffer_head *bh_result, int create)
{
	unsigned int blkbits = inode->i_sb->s_blocksize_bits;
	unsigned maxblocks = bh_result->b_size >> blkbits;
	struct dnode_of_data dn;
	pgoff_t pgofs;
	int err;

	/* Get the page offset from the block offset(iblock) */
	pgofs =	(pgoff_t)(iblock >> (PAGE_CACHE_SHIFT - blkbits));

	if (check_extent_cache(inode, pgofs, bh_result)) {
		trace_f2fs_get_data_block(inode, iblock, bh_result, 0);
		return 0;
	}

	/* When reading holes, we need its node page */
	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = get_dnode_of_data(&dn, pgofs, LOOKUP_NODE_RA);
	if (err) {
		trace_f2fs_get_data_block(inode, iblock, bh_result, err);
		return (err == -ENOENT) ? 0 : err;
	}

	/* It does not support data allocation */
	BUG_ON(create);

	if (dn.data_blkaddr != NEW_ADDR && dn.data_blkaddr != NULL_ADDR) {
		int i;
		unsigned int end_offset;

		end_offset = IS_INODE(dn.node_page) ?
				ADDRS_PER_INODE :
				ADDRS_PER_BLOCK;

		clear_buffer_new(bh_result);

		/* Give more consecutive addresses for the read ahead */
		for (i = 0; i < end_offset - dn.ofs_in_node; i++)
			if (((datablock_addr(dn.node_page,
							dn.ofs_in_node + i))
				!= (dn.data_blkaddr + i)) || maxblocks == i)
				break;
		map_bh(bh_result, inode->i_sb, dn.data_blkaddr);
		bh_result->b_size = (i << blkbits);
	}
	f2fs_put_dnode(&dn);
	trace_f2fs_get_data_block(inode, iblock, bh_result, 0);
	return 0;
}

static int f2fs_read_data_page(struct file *file, struct page *page)
{
	return mpage_readpage(page, get_data_block_ro);
}

static int f2fs_read_data_pages(struct file *file,
			struct address_space *mapping,
			struct list_head *pages, unsigned nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, get_data_block_ro);
}

int do_write_data_page(struct page *page)
{
	struct inode *inode = page->mapping->host;
	block_t old_blk_addr, new_blk_addr;
	struct dnode_of_data dn;
	int err = 0;

	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = get_dnode_of_data(&dn, page->index, LOOKUP_NODE);
	if (err)
		return err;

	old_blk_addr = dn.data_blkaddr;

	/* This page is already truncated */
	if (old_blk_addr == NULL_ADDR)
		goto out_writepage;

	set_page_writeback(page);

	/*
	 * If current allocation needs SSR,
	 * it had better in-place writes for updated data.
	 */
	if (old_blk_addr != NEW_ADDR && !is_cold_data(page) &&
				need_inplace_update(inode)) {
		rewrite_data_page(F2FS_SB(inode->i_sb), page,
						old_blk_addr);
	} else {
		write_data_page(inode, page, &dn,
				old_blk_addr, &new_blk_addr);
		update_extent_cache(new_blk_addr, &dn);
	}
out_writepage:
	f2fs_put_dnode(&dn);
	return err;
}

static int f2fs_write_data_page(struct page *page,
					struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	loff_t i_size = i_size_read(inode);
	const pgoff_t end_index = ((unsigned long long) i_size)
							>> PAGE_CACHE_SHIFT;
	unsigned offset;
	bool need_balance_fs = false;
	int err = 0;

	if (page->index < end_index)
		goto write;

	/*
	 * If the offset is out-of-range of file size,
	 * this page does not have to be written to disk.
	 */
	offset = i_size & (PAGE_CACHE_SIZE - 1);
	if ((page->index >= end_index + 1) || !offset) {
		if (S_ISDIR(inode->i_mode)) {
			dec_page_count(sbi, F2FS_DIRTY_DENTS);
			inode_dec_dirty_dents(inode);
		}
		goto out;
	}

	zero_user_segment(page, offset, PAGE_CACHE_SIZE);
write:
	if (sbi->por_doing) {
		err = AOP_WRITEPAGE_ACTIVATE;
		goto redirty_out;
	}

	/* Dentry blocks are controlled by checkpoint */
	if (S_ISDIR(inode->i_mode)) {
		dec_page_count(sbi, F2FS_DIRTY_DENTS);
		inode_dec_dirty_dents(inode);
		err = do_write_data_page(page);
	} else {
		int ilock = mutex_lock_op(sbi);
		err = do_write_data_page(page);
		mutex_unlock_op(sbi, ilock);
		need_balance_fs = true;
	}
	if (err == -ENOENT)
		goto out;
	else if (err)
		goto redirty_out;

	if (wbc->for_reclaim)
		f2fs_submit_bio(sbi, DATA, true);

	clear_cold_data(page);
out:
	unlock_page(page);
	if (need_balance_fs)
		f2fs_balance_fs(sbi);
	return 0;

redirty_out:
	wbc->pages_skipped++;
	set_page_dirty(page);
	return err;
}

#define MAX_DESIRED_PAGES_WP	4096

static int __f2fs_writepage(struct page *page, struct writeback_control *wbc,
			void *data)
{
	struct address_space *mapping = data;
	int ret = mapping->a_ops->writepage(page, wbc);
	mapping_set_error(mapping, ret);
	return ret;
}

static int f2fs_write_data_pages(struct address_space *mapping,
			    struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	int ret;
	long excess_nrtw = 0, desired_nrtw;

	/* deal with chardevs and other special file */
	if (!mapping->a_ops->writepage)
		return 0;

	if (wbc->nr_to_write < MAX_DESIRED_PAGES_WP) {
		desired_nrtw = MAX_DESIRED_PAGES_WP;
		excess_nrtw = desired_nrtw - wbc->nr_to_write;
		wbc->nr_to_write = desired_nrtw;
	}

	if (!S_ISDIR(inode->i_mode))
		mutex_lock(&sbi->writepages);
	ret = write_cache_pages(mapping, wbc, __f2fs_writepage, mapping);
	if (!S_ISDIR(inode->i_mode))
		mutex_unlock(&sbi->writepages);
	f2fs_submit_bio(sbi, DATA, (wbc->sync_mode == WB_SYNC_ALL));

	remove_dirty_dir_inode(inode);

	wbc->nr_to_write -= excess_nrtw;
	return ret;
}

static int f2fs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags,
		struct page **pagep, void **fsdata)
{
	struct inode *inode = mapping->host;
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct page *page;
	pgoff_t index = ((unsigned long long) pos) >> PAGE_CACHE_SHIFT;
	struct dnode_of_data dn;
	int err = 0;
	int ilock;

	/* for nobh_write_end */
	*fsdata = NULL;

	f2fs_balance_fs(sbi);

	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page)
		return -ENOMEM;
	*pagep = page;

	ilock = mutex_lock_op(sbi);

	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = get_dnode_of_data(&dn, index, ALLOC_NODE);
	if (err)
		goto err;

	if (dn.data_blkaddr == NULL_ADDR)
		err = reserve_new_block(&dn);

	f2fs_put_dnode(&dn);
	if (err)
		goto err;

	mutex_unlock_op(sbi, ilock);

	if ((len == PAGE_CACHE_SIZE) || PageUptodate(page))
		return 0;

	if ((pos & PAGE_CACHE_MASK) >= i_size_read(inode)) {
		unsigned start = pos & (PAGE_CACHE_SIZE - 1);
		unsigned end = start + len;

		/* Reading beyond i_size is simple: memset to zero */
		zero_user_segments(page, 0, start, end, PAGE_CACHE_SIZE);
		goto out;
	}

	if (dn.data_blkaddr == NEW_ADDR) {
		zero_user_segment(page, 0, PAGE_CACHE_SIZE);
	} else {
		err = f2fs_readpage(sbi, page, dn.data_blkaddr, READ_SYNC);
		if (err)
			return err;
		lock_page(page);
		if (!PageUptodate(page)) {
			f2fs_put_page(page, 1);
			return -EIO;
		}
	}
out:
	SetPageUptodate(page);
	clear_cold_data(page);
	return 0;

err:
	mutex_unlock_op(sbi, ilock);
	f2fs_put_page(page, 1);
	return err;
}

static ssize_t f2fs_direct_IO(int rw, struct kiocb *iocb,
		const struct iovec *iov, loff_t offset, unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;

	if (rw == WRITE)
		return 0;

	/* Needs synchronization with the cleaner */
	return blockdev_direct_IO(rw, iocb, inode, iov, offset, nr_segs,
						  get_data_block_ro);
}

static void f2fs_invalidate_data_page(struct page *page, unsigned long offset)
{
	struct inode *inode = page->mapping->host;
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	if (S_ISDIR(inode->i_mode) && PageDirty(page)) {
		dec_page_count(sbi, F2FS_DIRTY_DENTS);
		inode_dec_dirty_dents(inode);
	}
	ClearPagePrivate(page);
}

static int f2fs_release_data_page(struct page *page, gfp_t wait)
{
	ClearPagePrivate(page);
	return 1;
}

static int f2fs_set_data_page_dirty(struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct inode *inode = mapping->host;

	SetPageUptodate(page);
	if (!PageDirty(page)) {
		__set_page_dirty_nobuffers(page);
		set_dirty_dir_page(inode, page);
		return 1;
	}
	return 0;
}

static sector_t f2fs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, get_data_block_ro);
}

const struct address_space_operations f2fs_dblock_aops = {
	.readpage	= f2fs_read_data_page,
	.readpages	= f2fs_read_data_pages,
	.writepage	= f2fs_write_data_page,
	.writepages	= f2fs_write_data_pages,
	.write_begin	= f2fs_write_begin,
	.write_end	= nobh_write_end,
	.set_page_dirty	= f2fs_set_data_page_dirty,
	.invalidatepage	= f2fs_invalidate_data_page,
	.releasepage	= f2fs_release_data_page,
	.direct_IO	= f2fs_direct_IO,
	.bmap		= f2fs_bmap,
};
