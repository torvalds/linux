// SPDX-License-Identifier: GPL-2.0
/*
 * (C) 2001 Clemson University and The University of Chicago
 * Copyright 2018 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

/*
 *  Linux VFS inode operations.
 */

#include <linux/bvec.h>
#include "protocol.h"
#include "orangefs-kernel.h"
#include "orangefs-bufmap.h"

static int orangefs_writepage_locked(struct page *page,
    struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	struct orangefs_write_range *wr = NULL;
	struct iov_iter iter;
	struct bio_vec bv;
	size_t len, wlen;
	ssize_t ret;
	loff_t off;

	set_page_writeback(page);

	len = i_size_read(inode);
	if (PagePrivate(page)) {
		wr = (struct orangefs_write_range *)page_private(page);
		WARN_ON(wr->pos >= len);
		off = wr->pos;
		if (off + wr->len > len)
			wlen = len - off;
		else
			wlen = wr->len;
	} else {
		WARN_ON(1);
		off = page_offset(page);
		if (off + PAGE_SIZE > len)
			wlen = len - off;
		else
			wlen = PAGE_SIZE;
	}
	/* Should've been handled in orangefs_invalidatepage. */
	WARN_ON(off == len || off + wlen > len);

	bv.bv_page = page;
	bv.bv_len = wlen;
	bv.bv_offset = off % PAGE_SIZE;
	WARN_ON(wlen == 0);
	iov_iter_bvec(&iter, WRITE, &bv, 1, wlen);

	ret = wait_for_direct_io(ORANGEFS_IO_WRITE, inode, &off, &iter, wlen,
	    len, wr, NULL);
	if (ret < 0) {
		SetPageError(page);
		mapping_set_error(page->mapping, ret);
	} else {
		ret = 0;
	}
	if (wr) {
		kfree(wr);
		set_page_private(page, 0);
		ClearPagePrivate(page);
		put_page(page);
	}
	return ret;
}

static int orangefs_writepage(struct page *page, struct writeback_control *wbc)
{
	int ret;
	ret = orangefs_writepage_locked(page, wbc);
	unlock_page(page);
	end_page_writeback(page);
	return ret;
}

struct orangefs_writepages {
	loff_t off;
	size_t len;
	kuid_t uid;
	kgid_t gid;
	int maxpages;
	int npages;
	struct page **pages;
	struct bio_vec *bv;
};

static int orangefs_writepages_work(struct orangefs_writepages *ow,
    struct writeback_control *wbc)
{
	struct inode *inode = ow->pages[0]->mapping->host;
	struct orangefs_write_range *wrp, wr;
	struct iov_iter iter;
	ssize_t ret;
	size_t len;
	loff_t off;
	int i;

	len = i_size_read(inode);

	for (i = 0; i < ow->npages; i++) {
		set_page_writeback(ow->pages[i]);
		ow->bv[i].bv_page = ow->pages[i];
		ow->bv[i].bv_len = min(page_offset(ow->pages[i]) + PAGE_SIZE,
		    ow->off + ow->len) -
		    max(ow->off, page_offset(ow->pages[i]));
		if (i == 0)
			ow->bv[i].bv_offset = ow->off -
			    page_offset(ow->pages[i]);
		else
			ow->bv[i].bv_offset = 0;
	}
	iov_iter_bvec(&iter, WRITE, ow->bv, ow->npages, ow->len);

	WARN_ON(ow->off >= len);
	if (ow->off + ow->len > len)
		ow->len = len - ow->off;

	off = ow->off;
	wr.uid = ow->uid;
	wr.gid = ow->gid;
	ret = wait_for_direct_io(ORANGEFS_IO_WRITE, inode, &off, &iter, ow->len,
	    0, &wr, NULL);
	if (ret < 0) {
		for (i = 0; i < ow->npages; i++) {
			SetPageError(ow->pages[i]);
			mapping_set_error(ow->pages[i]->mapping, ret);
			if (PagePrivate(ow->pages[i])) {
				wrp = (struct orangefs_write_range *)
				    page_private(ow->pages[i]);
				ClearPagePrivate(ow->pages[i]);
				put_page(ow->pages[i]);
				kfree(wrp);
			}
			end_page_writeback(ow->pages[i]);
			unlock_page(ow->pages[i]);
		}
	} else {
		ret = 0;
		for (i = 0; i < ow->npages; i++) {
			if (PagePrivate(ow->pages[i])) {
				wrp = (struct orangefs_write_range *)
				    page_private(ow->pages[i]);
				ClearPagePrivate(ow->pages[i]);
				put_page(ow->pages[i]);
				kfree(wrp);
			}
			end_page_writeback(ow->pages[i]);
			unlock_page(ow->pages[i]);
		}
	}
	return ret;
}

static int orangefs_writepages_callback(struct page *page,
    struct writeback_control *wbc, void *data)
{
	struct orangefs_writepages *ow = data;
	struct orangefs_write_range *wr;
	int ret;

	if (!PagePrivate(page)) {
		unlock_page(page);
		/* It's not private so there's nothing to write, right? */
		printk("writepages_callback not private!\n");
		BUG();
		return 0;
	}
	wr = (struct orangefs_write_range *)page_private(page);

	ret = -1;
	if (ow->npages == 0) {
		ow->off = wr->pos;
		ow->len = wr->len;
		ow->uid = wr->uid;
		ow->gid = wr->gid;
		ow->pages[ow->npages++] = page;
		ret = 0;
		goto done;
	}
	if (!uid_eq(ow->uid, wr->uid) || !gid_eq(ow->gid, wr->gid)) {
		orangefs_writepages_work(ow, wbc);
		ow->npages = 0;
		ret = -1;
		goto done;
	}
	if (ow->off + ow->len == wr->pos) {
		ow->len += wr->len;
		ow->pages[ow->npages++] = page;
		ret = 0;
		goto done;
	}
done:
	if (ret == -1) {
		if (ow->npages) {
			orangefs_writepages_work(ow, wbc);
			ow->npages = 0;
		}
		ret = orangefs_writepage_locked(page, wbc);
		mapping_set_error(page->mapping, ret);
		unlock_page(page);
		end_page_writeback(page);
	} else {
		if (ow->npages == ow->maxpages) {
			orangefs_writepages_work(ow, wbc);
			ow->npages = 0;
		}
	}
	return ret;
}

static int orangefs_writepages(struct address_space *mapping,
    struct writeback_control *wbc)
{
	struct orangefs_writepages *ow;
	struct blk_plug plug;
	int ret;
	ow = kzalloc(sizeof(struct orangefs_writepages), GFP_KERNEL);
	if (!ow)
		return -ENOMEM;
	ow->maxpages = orangefs_bufmap_size_query()/PAGE_SIZE;
	ow->pages = kcalloc(ow->maxpages, sizeof(struct page *), GFP_KERNEL);
	if (!ow->pages) {
		kfree(ow);
		return -ENOMEM;
	}
	ow->bv = kcalloc(ow->maxpages, sizeof(struct bio_vec), GFP_KERNEL);
	if (!ow->bv) {
		kfree(ow->pages);
		kfree(ow);
		return -ENOMEM;
	}
	blk_start_plug(&plug);
	ret = write_cache_pages(mapping, wbc, orangefs_writepages_callback, ow);
	if (ow->npages)
		ret = orangefs_writepages_work(ow, wbc);
	blk_finish_plug(&plug);
	kfree(ow->pages);
	kfree(ow->bv);
	kfree(ow);
	return ret;
}

static int orangefs_launder_page(struct page *);

static int orangefs_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct iov_iter iter;
	struct bio_vec bv;
	ssize_t ret;
	loff_t off; /* offset into this page */
	pgoff_t index; /* which page */
	struct page *next_page;
	char *kaddr;
	struct orangefs_read_options *ro = file->private_data;
	loff_t read_size;
	loff_t roundedup;
	int buffer_index = -1; /* orangefs shared memory slot */
	int slot_index;   /* index into slot */
	int remaining;

	/*
	 * If they set some miniscule size for "count" in read(2)
	 * (for example) then let's try to read a page, or the whole file
	 * if it is smaller than a page. Once "count" goes over a page
	 * then lets round up to the highest page size multiple that is
	 * less than or equal to "count" and do that much orangefs IO and
	 * try to fill as many pages as we can from it.
	 *
	 * "count" should be represented in ro->blksiz.
	 *
	 * inode->i_size = file size.
	 */
	if (ro) {
		if (ro->blksiz < PAGE_SIZE) {
			if (inode->i_size < PAGE_SIZE)
				read_size = inode->i_size;
			else
				read_size = PAGE_SIZE;
		} else {
			roundedup = ((PAGE_SIZE - 1) & ro->blksiz) ?
				((ro->blksiz + PAGE_SIZE) & ~(PAGE_SIZE -1)) :
				ro->blksiz;
			if (roundedup > inode->i_size)
				read_size = inode->i_size;
			else
				read_size = roundedup;

		}
	} else {
		read_size = PAGE_SIZE;
	}
	if (!read_size)
		read_size = PAGE_SIZE;

	if (PageDirty(page))
		orangefs_launder_page(page);

	off = page_offset(page);
	index = off >> PAGE_SHIFT;
	bv.bv_page = page;
	bv.bv_len = PAGE_SIZE;
	bv.bv_offset = 0;
	iov_iter_bvec(&iter, READ, &bv, 1, PAGE_SIZE);

	ret = wait_for_direct_io(ORANGEFS_IO_READ, inode, &off, &iter,
	    read_size, inode->i_size, NULL, &buffer_index);
	remaining = ret;
	/* this will only zero remaining unread portions of the page data */
	iov_iter_zero(~0U, &iter);
	/* takes care of potential aliasing */
	flush_dcache_page(page);
	if (ret < 0) {
		SetPageError(page);
		unlock_page(page);
		goto out;
	} else {
		SetPageUptodate(page);
		if (PageError(page))
			ClearPageError(page);
		ret = 0;
	}
	/* unlock the page after the ->readpage() routine completes */
	unlock_page(page);

	if (remaining > PAGE_SIZE) {
		slot_index = 0;
		while ((remaining - PAGE_SIZE) >= PAGE_SIZE) {
			remaining -= PAGE_SIZE;
			/*
			 * It is an optimization to try and fill more than one
			 * page... by now we've already gotten the single
			 * page we were after, if stuff doesn't seem to
			 * be going our way at this point just return
			 * and hope for the best.
			 *
			 * If we look for pages and they're already there is
			 * one reason to give up, and if they're not there
			 * and we can't create them is another reason.
			 */

			index++;
			slot_index++;
			next_page = find_get_page(inode->i_mapping, index);
			if (next_page) {
				gossip_debug(GOSSIP_FILE_DEBUG,
					"%s: found next page, quitting\n",
					__func__);
				put_page(next_page);
				goto out;
			}
			next_page = find_or_create_page(inode->i_mapping,
							index,
							GFP_KERNEL);
			/*
			 * I've never hit this, leave it as a printk for
			 * now so it will be obvious.
			 */
			if (!next_page) {
				printk("%s: can't create next page, quitting\n",
					__func__);
				goto out;
			}
			kaddr = kmap_atomic(next_page);
			orangefs_bufmap_page_fill(kaddr,
						buffer_index,
						slot_index);
			kunmap_atomic(kaddr);
			SetPageUptodate(next_page);
			unlock_page(next_page);
			put_page(next_page);
		}
	}

out:
	if (buffer_index != -1)
		orangefs_bufmap_put(buffer_index);
	return ret;
}

static int orangefs_write_begin(struct file *file,
    struct address_space *mapping,
    loff_t pos, unsigned len, unsigned flags, struct page **pagep,
    void **fsdata)
{
	struct orangefs_write_range *wr;
	struct page *page;
	pgoff_t index;
	int ret;

	index = pos >> PAGE_SHIFT;

	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page)
		return -ENOMEM;

	*pagep = page;

	if (PageDirty(page) && !PagePrivate(page)) {
		/*
		 * Should be impossible.  If it happens, launder the page
		 * since we don't know what's dirty.  This will WARN in
		 * orangefs_writepage_locked.
		 */
		ret = orangefs_launder_page(page);
		if (ret)
			return ret;
	}
	if (PagePrivate(page)) {
		struct orangefs_write_range *wr;
		wr = (struct orangefs_write_range *)page_private(page);
		if (wr->pos + wr->len == pos &&
		    uid_eq(wr->uid, current_fsuid()) &&
		    gid_eq(wr->gid, current_fsgid())) {
			wr->len += len;
			goto okay;
		} else {
			ret = orangefs_launder_page(page);
			if (ret)
				return ret;
		}
	}

	wr = kmalloc(sizeof *wr, GFP_KERNEL);
	if (!wr)
		return -ENOMEM;

	wr->pos = pos;
	wr->len = len;
	wr->uid = current_fsuid();
	wr->gid = current_fsgid();
	SetPagePrivate(page);
	set_page_private(page, (unsigned long)wr);
	get_page(page);
okay:
	return 0;
}

static int orangefs_write_end(struct file *file, struct address_space *mapping,
    loff_t pos, unsigned len, unsigned copied, struct page *page, void *fsdata)
{
	struct inode *inode = page->mapping->host;
	loff_t last_pos = pos + copied;

	/*
	 * No need to use i_size_read() here, the i_size
	 * cannot change under us because we hold the i_mutex.
	 */
	if (last_pos > inode->i_size)
		i_size_write(inode, last_pos);

	/* zero the stale part of the page if we did a short copy */
	if (!PageUptodate(page)) {
		unsigned from = pos & (PAGE_SIZE - 1);
		if (copied < len) {
			zero_user(page, from + copied, len - copied);
		}
		/* Set fully written pages uptodate. */
		if (pos == page_offset(page) &&
		    (len == PAGE_SIZE || pos + len == inode->i_size)) {
			zero_user_segment(page, from + copied, PAGE_SIZE);
			SetPageUptodate(page);
		}
	}

	set_page_dirty(page);
	unlock_page(page);
	put_page(page);

	mark_inode_dirty_sync(file_inode(file));
	return copied;
}

static void orangefs_invalidatepage(struct page *page,
				 unsigned int offset,
				 unsigned int length)
{
	struct orangefs_write_range *wr;
	wr = (struct orangefs_write_range *)page_private(page);

	if (offset == 0 && length == PAGE_SIZE) {
		kfree((struct orangefs_write_range *)page_private(page));
		set_page_private(page, 0);
		ClearPagePrivate(page);
		put_page(page);
		return;
	/* write range entirely within invalidate range (or equal) */
	} else if (page_offset(page) + offset <= wr->pos &&
	    wr->pos + wr->len <= page_offset(page) + offset + length) {
		kfree((struct orangefs_write_range *)page_private(page));
		set_page_private(page, 0);
		ClearPagePrivate(page);
		put_page(page);
		/* XXX is this right? only caller in fs */
		cancel_dirty_page(page);
		return;
	/* invalidate range chops off end of write range */
	} else if (wr->pos < page_offset(page) + offset &&
	    wr->pos + wr->len <= page_offset(page) + offset + length &&
	     page_offset(page) + offset < wr->pos + wr->len) {
		size_t x;
		x = wr->pos + wr->len - (page_offset(page) + offset);
		WARN_ON(x > wr->len);
		wr->len -= x;
		wr->uid = current_fsuid();
		wr->gid = current_fsgid();
	/* invalidate range chops off beginning of write range */
	} else if (page_offset(page) + offset <= wr->pos &&
	    page_offset(page) + offset + length < wr->pos + wr->len &&
	    wr->pos < page_offset(page) + offset + length) {
		size_t x;
		x = page_offset(page) + offset + length - wr->pos;
		WARN_ON(x > wr->len);
		wr->pos += x;
		wr->len -= x;
		wr->uid = current_fsuid();
		wr->gid = current_fsgid();
	/* invalidate range entirely within write range (punch hole) */
	} else if (wr->pos < page_offset(page) + offset &&
	    page_offset(page) + offset + length < wr->pos + wr->len) {
		/* XXX what do we do here... should not WARN_ON */
		WARN_ON(1);
		/* punch hole */
		/*
		 * should we just ignore this and write it out anyway?
		 * it hardly makes sense
		 */
		return;
	/* non-overlapping ranges */
	} else {
		/* WARN if they do overlap */
		if (!((page_offset(page) + offset + length <= wr->pos) ^
		    (wr->pos + wr->len <= page_offset(page) + offset))) {
			WARN_ON(1);
			printk("invalidate range offset %llu length %u\n",
			    page_offset(page) + offset, length);
			printk("write range offset %llu length %zu\n",
			    wr->pos, wr->len);
		}
		return;
	}

	/*
	 * Above there are returns where wr is freed or where we WARN.
	 * Thus the following runs if wr was modified above.
	 */

	orangefs_launder_page(page);
}

static int orangefs_releasepage(struct page *page, gfp_t foo)
{
	return !PagePrivate(page);
}

static void orangefs_freepage(struct page *page)
{
	if (PagePrivate(page)) {
		kfree((struct orangefs_write_range *)page_private(page));
		set_page_private(page, 0);
		ClearPagePrivate(page);
		put_page(page);
	}
}

static int orangefs_launder_page(struct page *page)
{
	int r = 0;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = 0,
	};
	wait_on_page_writeback(page);
	if (clear_page_dirty_for_io(page)) {
		r = orangefs_writepage_locked(page, &wbc);
		end_page_writeback(page);
	}
	return r;
}

static ssize_t orangefs_direct_IO(struct kiocb *iocb,
				  struct iov_iter *iter)
{
	/*
	 * Comment from original do_readv_writev:
	 * Common entry point for read/write/readv/writev
	 * This function will dispatch it to either the direct I/O
	 * or buffered I/O path depending on the mount options and/or
	 * augmented/extended metadata attached to the file.
	 * Note: File extended attributes override any mount options.
	 */
	struct file *file = iocb->ki_filp;
	loff_t pos = iocb->ki_pos;
	enum ORANGEFS_io_type type = iov_iter_rw(iter) == WRITE ?
            ORANGEFS_IO_WRITE : ORANGEFS_IO_READ;
	loff_t *offset = &pos;
	struct inode *inode = file->f_mapping->host;
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct orangefs_khandle *handle = &orangefs_inode->refn.khandle;
	size_t count = iov_iter_count(iter);
	ssize_t total_count = 0;
	ssize_t ret = -EINVAL;
	int i = 0;

	gossip_debug(GOSSIP_FILE_DEBUG,
		"%s-BEGIN(%pU): count(%d) after estimate_max_iovecs.\n",
		__func__,
		handle,
		(int)count);

	if (type == ORANGEFS_IO_WRITE) {
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s(%pU): proceeding with offset : %llu, "
			     "size %d\n",
			     __func__,
			     handle,
			     llu(*offset),
			     (int)count);
	}

	if (count == 0) {
		ret = 0;
		goto out;
	}

	while (iov_iter_count(iter)) {
		size_t each_count = iov_iter_count(iter);
		size_t amt_complete;
		i++;

		/* how much to transfer in this loop iteration */
		if (each_count > orangefs_bufmap_size_query())
			each_count = orangefs_bufmap_size_query();

		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s(%pU): size of each_count(%d)\n",
			     __func__,
			     handle,
			     (int)each_count);
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s(%pU): BEFORE wait_for_io: offset is %d\n",
			     __func__,
			     handle,
			     (int)*offset);

		ret = wait_for_direct_io(type, inode, offset, iter,
				each_count, 0, NULL, NULL);
		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s(%pU): return from wait_for_io:%d\n",
			     __func__,
			     handle,
			     (int)ret);

		if (ret < 0)
			goto out;

		*offset += ret;
		total_count += ret;
		amt_complete = ret;

		gossip_debug(GOSSIP_FILE_DEBUG,
			     "%s(%pU): AFTER wait_for_io: offset is %d\n",
			     __func__,
			     handle,
			     (int)*offset);

		/*
		 * if we got a short I/O operations,
		 * fall out and return what we got so far
		 */
		if (amt_complete < each_count)
			break;
	} /*end while */

out:
	if (total_count > 0)
		ret = total_count;
	if (ret > 0) {
		if (type == ORANGEFS_IO_READ) {
			file_accessed(file);
		} else {
			file_update_time(file);
			if (*offset > i_size_read(inode))
				i_size_write(inode, *offset);
		}
	}

	gossip_debug(GOSSIP_FILE_DEBUG,
		     "%s(%pU): Value(%d) returned.\n",
		     __func__,
		     handle,
		     (int)ret);

	return ret;
}

/** ORANGEFS2 implementation of address space operations */
static const struct address_space_operations orangefs_address_operations = {
	.writepage = orangefs_writepage,
	.readpage = orangefs_readpage,
	.writepages = orangefs_writepages,
	.set_page_dirty = __set_page_dirty_nobuffers,
	.write_begin = orangefs_write_begin,
	.write_end = orangefs_write_end,
	.invalidatepage = orangefs_invalidatepage,
	.releasepage = orangefs_releasepage,
	.freepage = orangefs_freepage,
	.launder_page = orangefs_launder_page,
	.direct_IO = orangefs_direct_IO,
};

vm_fault_t orangefs_page_mkwrite(struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct inode *inode = file_inode(vmf->vma->vm_file);
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	unsigned long *bitlock = &orangefs_inode->bitlock;
	vm_fault_t ret;
	struct orangefs_write_range *wr;

	sb_start_pagefault(inode->i_sb);

	if (wait_on_bit(bitlock, 1, TASK_KILLABLE)) {
		ret = VM_FAULT_RETRY;
		goto out;
	}

	lock_page(page);
	if (PageDirty(page) && !PagePrivate(page)) {
		/*
		 * Should be impossible.  If it happens, launder the page
		 * since we don't know what's dirty.  This will WARN in
		 * orangefs_writepage_locked.
		 */
		if (orangefs_launder_page(page)) {
			ret = VM_FAULT_LOCKED|VM_FAULT_RETRY;
			goto out;
		}
	}
	if (PagePrivate(page)) {
		wr = (struct orangefs_write_range *)page_private(page);
		if (uid_eq(wr->uid, current_fsuid()) &&
		    gid_eq(wr->gid, current_fsgid())) {
			wr->pos = page_offset(page);
			wr->len = PAGE_SIZE;
			goto okay;
		} else {
			if (orangefs_launder_page(page)) {
				ret = VM_FAULT_LOCKED|VM_FAULT_RETRY;
				goto out;
			}
		}
	}
	wr = kmalloc(sizeof *wr, GFP_KERNEL);
	if (!wr) {
		ret = VM_FAULT_LOCKED|VM_FAULT_RETRY;
		goto out;
	}
	wr->pos = page_offset(page);
	wr->len = PAGE_SIZE;
	wr->uid = current_fsuid();
	wr->gid = current_fsgid();
	SetPagePrivate(page);
	set_page_private(page, (unsigned long)wr);
	get_page(page);
okay:

	file_update_time(vmf->vma->vm_file);
	if (page->mapping != inode->i_mapping) {
		unlock_page(page);
		ret = VM_FAULT_LOCKED|VM_FAULT_NOPAGE;
		goto out;
	}

	/*
	 * We mark the page dirty already here so that when freeze is in
	 * progress, we are guaranteed that writeback during freezing will
	 * see the dirty page and writeprotect it again.
	 */
	set_page_dirty(page);
	wait_for_stable_page(page);
	ret = VM_FAULT_LOCKED;
out:
	sb_end_pagefault(inode->i_sb);
	return ret;
}

static int orangefs_setattr_size(struct inode *inode, struct iattr *iattr)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct orangefs_kernel_op_s *new_op;
	loff_t orig_size;
	int ret = -EINVAL;

	gossip_debug(GOSSIP_INODE_DEBUG,
		     "%s: %pU: Handle is %pU | fs_id %d | size is %llu\n",
		     __func__,
		     get_khandle_from_ino(inode),
		     &orangefs_inode->refn.khandle,
		     orangefs_inode->refn.fs_id,
		     iattr->ia_size);

	/* Ensure that we have a up to date size, so we know if it changed. */
	ret = orangefs_inode_getattr(inode, ORANGEFS_GETATTR_SIZE);
	if (ret == -ESTALE)
		ret = -EIO;
	if (ret) {
		gossip_err("%s: orangefs_inode_getattr failed, ret:%d:.\n",
		    __func__, ret);
		return ret;
	}
	orig_size = i_size_read(inode);

	/* This is truncate_setsize in a different order. */
	truncate_pagecache(inode, iattr->ia_size);
	i_size_write(inode, iattr->ia_size);
	if (iattr->ia_size > orig_size)
		pagecache_isize_extended(inode, orig_size, iattr->ia_size);

	new_op = op_alloc(ORANGEFS_VFS_OP_TRUNCATE);
	if (!new_op)
		return -ENOMEM;

	new_op->upcall.req.truncate.refn = orangefs_inode->refn;
	new_op->upcall.req.truncate.size = (__s64) iattr->ia_size;

	ret = service_operation(new_op,
		__func__,
		get_interruptible_flag(inode));

	/*
	 * the truncate has no downcall members to retrieve, but
	 * the status value tells us if it went through ok or not
	 */
	gossip_debug(GOSSIP_INODE_DEBUG, "%s: ret:%d:\n", __func__, ret);

	op_release(new_op);

	if (ret != 0)
		return ret;

	if (orig_size != i_size_read(inode))
		iattr->ia_valid |= ATTR_CTIME | ATTR_MTIME;

	return ret;
}

int __orangefs_setattr(struct inode *inode, struct iattr *iattr)
{
	int ret;

	if (iattr->ia_valid & ATTR_MODE) {
		if (iattr->ia_mode & (S_ISVTX)) {
			if (is_root_handle(inode)) {
				/*
				 * allow sticky bit to be set on root (since
				 * it shows up that way by default anyhow),
				 * but don't show it to the server
				 */
				iattr->ia_mode -= S_ISVTX;
			} else {
				gossip_debug(GOSSIP_UTILS_DEBUG,
					     "User attempted to set sticky bit on non-root directory; returning EINVAL.\n");
				ret = -EINVAL;
				goto out;
			}
		}
		if (iattr->ia_mode & (S_ISUID)) {
			gossip_debug(GOSSIP_UTILS_DEBUG,
				     "Attempting to set setuid bit (not supported); returning EINVAL.\n");
			ret = -EINVAL;
			goto out;
		}
	}

	if (iattr->ia_valid & ATTR_SIZE) {
		ret = orangefs_setattr_size(inode, iattr);
		if (ret)
			goto out;
	}

again:
	spin_lock(&inode->i_lock);
	if (ORANGEFS_I(inode)->attr_valid) {
		if (uid_eq(ORANGEFS_I(inode)->attr_uid, current_fsuid()) &&
		    gid_eq(ORANGEFS_I(inode)->attr_gid, current_fsgid())) {
			ORANGEFS_I(inode)->attr_valid = iattr->ia_valid;
		} else {
			spin_unlock(&inode->i_lock);
			write_inode_now(inode, 1);
			goto again;
		}
	} else {
		ORANGEFS_I(inode)->attr_valid = iattr->ia_valid;
		ORANGEFS_I(inode)->attr_uid = current_fsuid();
		ORANGEFS_I(inode)->attr_gid = current_fsgid();
	}
	setattr_copy(inode, iattr);
	spin_unlock(&inode->i_lock);
	mark_inode_dirty(inode);

	if (iattr->ia_valid & ATTR_MODE)
		/* change mod on a file that has ACLs */
		ret = posix_acl_chmod(inode, inode->i_mode);

	ret = 0;
out:
	return ret;
}

/*
 * Change attributes of an object referenced by dentry.
 */
int orangefs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	int ret;
	gossip_debug(GOSSIP_INODE_DEBUG, "__orangefs_setattr: called on %pd\n",
	    dentry);
	ret = setattr_prepare(dentry, iattr);
	if (ret)
	        goto out;
	ret = __orangefs_setattr(d_inode(dentry), iattr);
	sync_inode_metadata(d_inode(dentry), 1);
out:
	gossip_debug(GOSSIP_INODE_DEBUG, "orangefs_setattr: returning %d\n",
	    ret);
	return ret;
}

/*
 * Obtain attributes of an object given a dentry
 */
int orangefs_getattr(const struct path *path, struct kstat *stat,
		     u32 request_mask, unsigned int flags)
{
	int ret = -ENOENT;
	struct inode *inode = path->dentry->d_inode;

	gossip_debug(GOSSIP_INODE_DEBUG,
		     "orangefs_getattr: called on %pd mask %u\n",
		     path->dentry, request_mask);

	ret = orangefs_inode_getattr(inode,
	    request_mask & STATX_SIZE ? ORANGEFS_GETATTR_SIZE : 0);
	if (ret == 0) {
		generic_fillattr(inode, stat);

		/* override block size reported to stat */
		if (!(request_mask & STATX_SIZE))
			stat->result_mask &= ~STATX_SIZE;

		stat->attributes_mask = STATX_ATTR_IMMUTABLE |
		    STATX_ATTR_APPEND;
		if (inode->i_flags & S_IMMUTABLE)
			stat->attributes |= STATX_ATTR_IMMUTABLE;
		if (inode->i_flags & S_APPEND)
			stat->attributes |= STATX_ATTR_APPEND;
	}
	return ret;
}

int orangefs_permission(struct inode *inode, int mask)
{
	int ret;

	if (mask & MAY_NOT_BLOCK)
		return -ECHILD;

	gossip_debug(GOSSIP_INODE_DEBUG, "%s: refreshing\n", __func__);

	/* Make sure the permission (and other common attrs) are up to date. */
	ret = orangefs_inode_getattr(inode, 0);
	if (ret < 0)
		return ret;

	return generic_permission(inode, mask);
}

int orangefs_update_time(struct inode *inode, struct timespec64 *time, int flags)
{
	struct iattr iattr;
	gossip_debug(GOSSIP_INODE_DEBUG, "orangefs_update_time: %pU\n",
	    get_khandle_from_ino(inode));
	generic_update_time(inode, time, flags);
	memset(&iattr, 0, sizeof iattr);
        if (flags & S_ATIME)
		iattr.ia_valid |= ATTR_ATIME;
	if (flags & S_CTIME)
		iattr.ia_valid |= ATTR_CTIME;
	if (flags & S_MTIME)
		iattr.ia_valid |= ATTR_MTIME;
	return __orangefs_setattr(inode, &iattr);
}

/* ORANGEFS2 implementation of VFS inode operations for files */
static const struct inode_operations orangefs_file_inode_operations = {
	.get_acl = orangefs_get_acl,
	.set_acl = orangefs_set_acl,
	.setattr = orangefs_setattr,
	.getattr = orangefs_getattr,
	.listxattr = orangefs_listxattr,
	.permission = orangefs_permission,
	.update_time = orangefs_update_time,
};

static int orangefs_init_iops(struct inode *inode)
{
	inode->i_mapping->a_ops = &orangefs_address_operations;

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		inode->i_op = &orangefs_file_inode_operations;
		inode->i_fop = &orangefs_file_operations;
		break;
	case S_IFLNK:
		inode->i_op = &orangefs_symlink_inode_operations;
		break;
	case S_IFDIR:
		inode->i_op = &orangefs_dir_inode_operations;
		inode->i_fop = &orangefs_dir_operations;
		break;
	default:
		gossip_debug(GOSSIP_INODE_DEBUG,
			     "%s: unsupported mode\n",
			     __func__);
		return -EINVAL;
	}

	return 0;
}

/*
 * Given an ORANGEFS object identifier (fsid, handle), convert it into
 * a ino_t type that will be used as a hash-index from where the handle will
 * be searched for in the VFS hash table of inodes.
 */
static inline ino_t orangefs_handle_hash(struct orangefs_object_kref *ref)
{
	if (!ref)
		return 0;
	return orangefs_khandle_to_ino(&(ref->khandle));
}

/*
 * Called to set up an inode from iget5_locked.
 */
static int orangefs_set_inode(struct inode *inode, void *data)
{
	struct orangefs_object_kref *ref = (struct orangefs_object_kref *) data;
	ORANGEFS_I(inode)->refn.fs_id = ref->fs_id;
	ORANGEFS_I(inode)->refn.khandle = ref->khandle;
	ORANGEFS_I(inode)->attr_valid = 0;
	hash_init(ORANGEFS_I(inode)->xattr_cache);
	ORANGEFS_I(inode)->mapping_time = jiffies - 1;
	ORANGEFS_I(inode)->bitlock = 0;
	return 0;
}

/*
 * Called to determine if handles match.
 */
static int orangefs_test_inode(struct inode *inode, void *data)
{
	struct orangefs_object_kref *ref = (struct orangefs_object_kref *) data;
	struct orangefs_inode_s *orangefs_inode = NULL;

	orangefs_inode = ORANGEFS_I(inode);
	/* test handles and fs_ids... */
	return (!ORANGEFS_khandle_cmp(&(orangefs_inode->refn.khandle),
				&(ref->khandle)) &&
			orangefs_inode->refn.fs_id == ref->fs_id);
}

/*
 * Front-end to lookup the inode-cache maintained by the VFS using the ORANGEFS
 * file handle.
 *
 * @sb: the file system super block instance.
 * @ref: The ORANGEFS object for which we are trying to locate an inode.
 */
struct inode *orangefs_iget(struct super_block *sb,
		struct orangefs_object_kref *ref)
{
	struct inode *inode = NULL;
	unsigned long hash;
	int error;

	hash = orangefs_handle_hash(ref);
	inode = iget5_locked(sb,
			hash,
			orangefs_test_inode,
			orangefs_set_inode,
			ref);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (!(inode->i_state & I_NEW))
		return inode;

	error = orangefs_inode_getattr(inode, ORANGEFS_GETATTR_NEW);
	if (error) {
		iget_failed(inode);
		return ERR_PTR(error);
	}

	inode->i_ino = hash;	/* needed for stat etc */
	orangefs_init_iops(inode);
	unlock_new_inode(inode);

	gossip_debug(GOSSIP_INODE_DEBUG,
		     "iget handle %pU, fsid %d hash %ld i_ino %lu\n",
		     &ref->khandle,
		     ref->fs_id,
		     hash,
		     inode->i_ino);

	return inode;
}

/*
 * Allocate an inode for a newly created file and insert it into the inode hash.
 */
struct inode *orangefs_new_inode(struct super_block *sb, struct inode *dir,
		int mode, dev_t dev, struct orangefs_object_kref *ref)
{
	unsigned long hash = orangefs_handle_hash(ref);
	struct inode *inode;
	int error;

	gossip_debug(GOSSIP_INODE_DEBUG,
		     "%s:(sb is %p | MAJOR(dev)=%u | MINOR(dev)=%u mode=%o)\n",
		     __func__,
		     sb,
		     MAJOR(dev),
		     MINOR(dev),
		     mode);

	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	orangefs_set_inode(inode, ref);
	inode->i_ino = hash;	/* needed for stat etc */

	error = orangefs_inode_getattr(inode, ORANGEFS_GETATTR_NEW);
	if (error)
		goto out_iput;

	orangefs_init_iops(inode);
	inode->i_rdev = dev;

	error = insert_inode_locked4(inode, hash, orangefs_test_inode, ref);
	if (error < 0)
		goto out_iput;

	gossip_debug(GOSSIP_INODE_DEBUG,
		     "Initializing ACL's for inode %pU\n",
		     get_khandle_from_ino(inode));
	orangefs_init_acl(inode, dir);
	return inode;

out_iput:
	iput(inode);
	return ERR_PTR(error);
}
