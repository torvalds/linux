/*
 * Copyright (C) 2010 Red Hat, Inc.
 * Copyright (c) 2016 Christoph Hellwig.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/module.h>
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/iomap.h>
#include <linux/uaccess.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/backing-dev.h>
#include <linux/buffer_head.h>
#include <linux/dax.h>
#include "internal.h"

typedef loff_t (*iomap_actor_t)(struct inode *inode, loff_t pos, loff_t len,
		void *data, struct iomap *iomap);

/*
 * Execute a iomap write on a segment of the mapping that spans a
 * contiguous range of pages that have identical block mapping state.
 *
 * This avoids the need to map pages individually, do individual allocations
 * for each page and most importantly avoid the need for filesystem specific
 * locking per page. Instead, all the operations are amortised over the entire
 * range of pages. It is assumed that the filesystems will lock whatever
 * resources they require in the iomap_begin call, and release them in the
 * iomap_end call.
 */
static loff_t
iomap_apply(struct inode *inode, loff_t pos, loff_t length, unsigned flags,
		struct iomap_ops *ops, void *data, iomap_actor_t actor)
{
	struct iomap iomap = { 0 };
	loff_t written = 0, ret;

	/*
	 * Need to map a range from start position for length bytes. This can
	 * span multiple pages - it is only guaranteed to return a range of a
	 * single type of pages (e.g. all into a hole, all mapped or all
	 * unwritten). Failure at this point has nothing to undo.
	 *
	 * If allocation is required for this range, reserve the space now so
	 * that the allocation is guaranteed to succeed later on. Once we copy
	 * the data into the page cache pages, then we cannot fail otherwise we
	 * expose transient stale data. If the reserve fails, we can safely
	 * back out at this point as there is nothing to undo.
	 */
	ret = ops->iomap_begin(inode, pos, length, flags, &iomap);
	if (ret)
		return ret;
	if (WARN_ON(iomap.offset > pos))
		return -EIO;

	/*
	 * Cut down the length to the one actually provided by the filesystem,
	 * as it might not be able to give us the whole size that we requested.
	 */
	if (iomap.offset + iomap.length < pos + length)
		length = iomap.offset + iomap.length - pos;

	/*
	 * Now that we have guaranteed that the space allocation will succeed.
	 * we can do the copy-in page by page without having to worry about
	 * failures exposing transient data.
	 */
	written = actor(inode, pos, length, data, &iomap);

	/*
	 * Now the data has been copied, commit the range we've copied.  This
	 * should not fail unless the filesystem has had a fatal error.
	 */
	ret = ops->iomap_end(inode, pos, length, written > 0 ? written : 0,
			flags, &iomap);

	return written ? written : ret;
}

static void
iomap_write_failed(struct inode *inode, loff_t pos, unsigned len)
{
	loff_t i_size = i_size_read(inode);

	/*
	 * Only truncate newly allocated pages beyoned EOF, even if the
	 * write started inside the existing inode size.
	 */
	if (pos + len > i_size)
		truncate_pagecache_range(inode, max(pos, i_size), pos + len);
}

static int
iomap_write_begin(struct inode *inode, loff_t pos, unsigned len, unsigned flags,
		struct page **pagep, struct iomap *iomap)
{
	pgoff_t index = pos >> PAGE_SHIFT;
	struct page *page;
	int status = 0;

	BUG_ON(pos + len > iomap->offset + iomap->length);

	page = grab_cache_page_write_begin(inode->i_mapping, index, flags);
	if (!page)
		return -ENOMEM;

	status = __block_write_begin_int(page, pos, len, NULL, iomap);
	if (unlikely(status)) {
		unlock_page(page);
		put_page(page);
		page = NULL;

		iomap_write_failed(inode, pos, len);
	}

	*pagep = page;
	return status;
}

static int
iomap_write_end(struct inode *inode, loff_t pos, unsigned len,
		unsigned copied, struct page *page)
{
	int ret;

	ret = generic_write_end(NULL, inode->i_mapping, pos, len,
			copied, page, NULL);
	if (ret < len)
		iomap_write_failed(inode, pos, len);
	return ret;
}

static loff_t
iomap_write_actor(struct inode *inode, loff_t pos, loff_t length, void *data,
		struct iomap *iomap)
{
	struct iov_iter *i = data;
	long status = 0;
	ssize_t written = 0;
	unsigned int flags = AOP_FLAG_NOFS;

	/*
	 * Copies from kernel address space cannot fail (NFSD is a big user).
	 */
	if (!iter_is_iovec(i))
		flags |= AOP_FLAG_UNINTERRUPTIBLE;

	do {
		struct page *page;
		unsigned long offset;	/* Offset into pagecache page */
		unsigned long bytes;	/* Bytes to write to page */
		size_t copied;		/* Bytes copied from user */

		offset = (pos & (PAGE_SIZE - 1));
		bytes = min_t(unsigned long, PAGE_SIZE - offset,
						iov_iter_count(i));
again:
		if (bytes > length)
			bytes = length;

		/*
		 * Bring in the user page that we will copy from _first_.
		 * Otherwise there's a nasty deadlock on copying from the
		 * same page as we're writing to, without it being marked
		 * up-to-date.
		 *
		 * Not only is this an optimisation, but it is also required
		 * to check that the address is actually valid, when atomic
		 * usercopies are used, below.
		 */
		if (unlikely(iov_iter_fault_in_readable(i, bytes))) {
			status = -EFAULT;
			break;
		}

		status = iomap_write_begin(inode, pos, bytes, flags, &page,
				iomap);
		if (unlikely(status))
			break;

		if (mapping_writably_mapped(inode->i_mapping))
			flush_dcache_page(page);

		pagefault_disable();
		copied = iov_iter_copy_from_user_atomic(page, i, offset, bytes);
		pagefault_enable();

		flush_dcache_page(page);
		mark_page_accessed(page);

		status = iomap_write_end(inode, pos, bytes, copied, page);
		if (unlikely(status < 0))
			break;
		copied = status;

		cond_resched();

		iov_iter_advance(i, copied);
		if (unlikely(copied == 0)) {
			/*
			 * If we were unable to copy any data at all, we must
			 * fall back to a single segment length write.
			 *
			 * If we didn't fallback here, we could livelock
			 * because not all segments in the iov can be copied at
			 * once without a pagefault.
			 */
			bytes = min_t(unsigned long, PAGE_SIZE - offset,
						iov_iter_single_seg_count(i));
			goto again;
		}
		pos += copied;
		written += copied;
		length -= copied;

		balance_dirty_pages_ratelimited(inode->i_mapping);
	} while (iov_iter_count(i) && length);

	return written ? written : status;
}

ssize_t
iomap_file_buffered_write(struct kiocb *iocb, struct iov_iter *iter,
		struct iomap_ops *ops)
{
	struct inode *inode = iocb->ki_filp->f_mapping->host;
	loff_t pos = iocb->ki_pos, ret = 0, written = 0;

	while (iov_iter_count(iter)) {
		ret = iomap_apply(inode, pos, iov_iter_count(iter),
				IOMAP_WRITE, ops, iter, iomap_write_actor);
		if (ret <= 0)
			break;
		pos += ret;
		written += ret;
	}

	return written ? written : ret;
}
EXPORT_SYMBOL_GPL(iomap_file_buffered_write);

static int iomap_zero(struct inode *inode, loff_t pos, unsigned offset,
		unsigned bytes, struct iomap *iomap)
{
	struct page *page;
	int status;

	status = iomap_write_begin(inode, pos, bytes,
			AOP_FLAG_UNINTERRUPTIBLE | AOP_FLAG_NOFS, &page, iomap);
	if (status)
		return status;

	zero_user(page, offset, bytes);
	mark_page_accessed(page);

	return iomap_write_end(inode, pos, bytes, bytes, page);
}

static int iomap_dax_zero(loff_t pos, unsigned offset, unsigned bytes,
		struct iomap *iomap)
{
	sector_t sector = iomap->blkno +
		(((pos & ~(PAGE_SIZE - 1)) - iomap->offset) >> 9);

	return __dax_zero_page_range(iomap->bdev, sector, offset, bytes);
}

static loff_t
iomap_zero_range_actor(struct inode *inode, loff_t pos, loff_t count,
		void *data, struct iomap *iomap)
{
	bool *did_zero = data;
	loff_t written = 0;
	int status;

	/* already zeroed?  we're done. */
	if (iomap->type == IOMAP_HOLE || iomap->type == IOMAP_UNWRITTEN)
	    	return count;

	do {
		unsigned offset, bytes;

		offset = pos & (PAGE_SIZE - 1); /* Within page */
		bytes = min_t(unsigned, PAGE_SIZE - offset, count);

		if (IS_DAX(inode))
			status = iomap_dax_zero(pos, offset, bytes, iomap);
		else
			status = iomap_zero(inode, pos, offset, bytes, iomap);
		if (status < 0)
			return status;

		pos += bytes;
		count -= bytes;
		written += bytes;
		if (did_zero)
			*did_zero = true;
	} while (count > 0);

	return written;
}

int
iomap_zero_range(struct inode *inode, loff_t pos, loff_t len, bool *did_zero,
		struct iomap_ops *ops)
{
	loff_t ret;

	while (len > 0) {
		ret = iomap_apply(inode, pos, len, IOMAP_ZERO,
				ops, did_zero, iomap_zero_range_actor);
		if (ret <= 0)
			return ret;

		pos += ret;
		len -= ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(iomap_zero_range);

int
iomap_truncate_page(struct inode *inode, loff_t pos, bool *did_zero,
		struct iomap_ops *ops)
{
	unsigned blocksize = (1 << inode->i_blkbits);
	unsigned off = pos & (blocksize - 1);

	/* Block boundary? Nothing to do */
	if (!off)
		return 0;
	return iomap_zero_range(inode, pos, blocksize - off, did_zero, ops);
}
EXPORT_SYMBOL_GPL(iomap_truncate_page);

static loff_t
iomap_page_mkwrite_actor(struct inode *inode, loff_t pos, loff_t length,
		void *data, struct iomap *iomap)
{
	struct page *page = data;
	int ret;

	ret = __block_write_begin_int(page, pos & ~PAGE_MASK, length,
			NULL, iomap);
	if (ret)
		return ret;

	block_commit_write(page, 0, length);
	return length;
}

int iomap_page_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf,
		struct iomap_ops *ops)
{
	struct page *page = vmf->page;
	struct inode *inode = file_inode(vma->vm_file);
	unsigned long length;
	loff_t offset, size;
	ssize_t ret;

	lock_page(page);
	size = i_size_read(inode);
	if ((page->mapping != inode->i_mapping) ||
	    (page_offset(page) > size)) {
		/* We overload EFAULT to mean page got truncated */
		ret = -EFAULT;
		goto out_unlock;
	}

	/* page is wholly or partially inside EOF */
	if (((page->index + 1) << PAGE_SHIFT) > size)
		length = size & ~PAGE_MASK;
	else
		length = PAGE_SIZE;

	offset = page_offset(page);
	while (length > 0) {
		ret = iomap_apply(inode, offset, length, IOMAP_WRITE,
				ops, page, iomap_page_mkwrite_actor);
		if (unlikely(ret <= 0))
			goto out_unlock;
		offset += ret;
		length -= ret;
	}

	set_page_dirty(page);
	wait_for_stable_page(page);
	return 0;
out_unlock:
	unlock_page(page);
	return ret;
}
EXPORT_SYMBOL_GPL(iomap_page_mkwrite);

struct fiemap_ctx {
	struct fiemap_extent_info *fi;
	struct iomap prev;
};

static int iomap_to_fiemap(struct fiemap_extent_info *fi,
		struct iomap *iomap, u32 flags)
{
	switch (iomap->type) {
	case IOMAP_HOLE:
		/* skip holes */
		return 0;
	case IOMAP_DELALLOC:
		flags |= FIEMAP_EXTENT_DELALLOC | FIEMAP_EXTENT_UNKNOWN;
		break;
	case IOMAP_UNWRITTEN:
		flags |= FIEMAP_EXTENT_UNWRITTEN;
		break;
	case IOMAP_MAPPED:
		break;
	}

	return fiemap_fill_next_extent(fi, iomap->offset,
			iomap->blkno != IOMAP_NULL_BLOCK ? iomap->blkno << 9: 0,
			iomap->length, flags | FIEMAP_EXTENT_MERGED);

}

static loff_t
iomap_fiemap_actor(struct inode *inode, loff_t pos, loff_t length, void *data,
		struct iomap *iomap)
{
	struct fiemap_ctx *ctx = data;
	loff_t ret = length;

	if (iomap->type == IOMAP_HOLE)
		return length;

	ret = iomap_to_fiemap(ctx->fi, &ctx->prev, 0);
	ctx->prev = *iomap;
	switch (ret) {
	case 0:		/* success */
		return length;
	case 1:		/* extent array full */
		return 0;
	default:
		return ret;
	}
}

int iomap_fiemap(struct inode *inode, struct fiemap_extent_info *fi,
		loff_t start, loff_t len, struct iomap_ops *ops)
{
	struct fiemap_ctx ctx;
	loff_t ret;

	memset(&ctx, 0, sizeof(ctx));
	ctx.fi = fi;
	ctx.prev.type = IOMAP_HOLE;

	ret = fiemap_check_flags(fi, FIEMAP_FLAG_SYNC);
	if (ret)
		return ret;

	ret = filemap_write_and_wait(inode->i_mapping);
	if (ret)
		return ret;

	while (len > 0) {
		ret = iomap_apply(inode, start, len, 0, ops, &ctx,
				iomap_fiemap_actor);
		if (ret < 0)
			return ret;
		if (ret == 0)
			break;

		start += ret;
		len -= ret;
	}

	if (ctx.prev.type != IOMAP_HOLE) {
		ret = iomap_to_fiemap(fi, &ctx.prev, FIEMAP_EXTENT_LAST);
		if (ret < 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(iomap_fiemap);
