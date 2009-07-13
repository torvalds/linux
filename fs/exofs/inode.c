/*
 * Copyright (C) 2005, 2006
 * Avishay Traeger (avishay@gmail.com)
 * Copyright (C) 2008, 2009
 * Boaz Harrosh <bharrosh@panasas.com>
 *
 * Copyrights for code taken from ext2:
 *     Copyright (C) 1992, 1993, 1994, 1995
 *     Remy Card (card@masi.ibp.fr)
 *     Laboratoire MASI - Institut Blaise Pascal
 *     Universite Pierre et Marie Curie (Paris VI)
 *     from
 *     linux/fs/minix/inode.c
 *     Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is part of exofs.
 *
 * exofs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.  Since it is based on ext2, and the only
 * valid version of GPL for the Linux kernel is version 2, the only valid
 * version of GPL for exofs is version 2.
 *
 * exofs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with exofs; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/writeback.h>
#include <linux/buffer_head.h>
#include <scsi/scsi_device.h>

#include "exofs.h"

#ifdef CONFIG_EXOFS_DEBUG
#  define EXOFS_DEBUG_OBJ_ISIZE 1
#endif

struct page_collect {
	struct exofs_sb_info *sbi;
	struct request_queue *req_q;
	struct inode *inode;
	unsigned expected_pages;

	struct bio *bio;
	unsigned nr_pages;
	unsigned long length;
	loff_t pg_first; /* keep 64bit also in 32-arches */
};

static void _pcol_init(struct page_collect *pcol, unsigned expected_pages,
		struct inode *inode)
{
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;

	pcol->sbi = sbi;
	pcol->req_q = osd_request_queue(sbi->s_dev);
	pcol->inode = inode;
	pcol->expected_pages = expected_pages;

	pcol->bio = NULL;
	pcol->nr_pages = 0;
	pcol->length = 0;
	pcol->pg_first = -1;

	EXOFS_DBGMSG("_pcol_init ino=0x%lx expected_pages=%u\n", inode->i_ino,
		     expected_pages);
}

static void _pcol_reset(struct page_collect *pcol)
{
	pcol->expected_pages -= min(pcol->nr_pages, pcol->expected_pages);

	pcol->bio = NULL;
	pcol->nr_pages = 0;
	pcol->length = 0;
	pcol->pg_first = -1;
	EXOFS_DBGMSG("_pcol_reset ino=0x%lx expected_pages=%u\n",
		     pcol->inode->i_ino, pcol->expected_pages);

	/* this is probably the end of the loop but in writes
	 * it might not end here. don't be left with nothing
	 */
	if (!pcol->expected_pages)
		pcol->expected_pages = 128;
}

static int pcol_try_alloc(struct page_collect *pcol)
{
	int pages = min_t(unsigned, pcol->expected_pages, BIO_MAX_PAGES);

	for (; pages; pages >>= 1) {
		pcol->bio = bio_alloc(GFP_KERNEL, pages);
		if (likely(pcol->bio))
			return 0;
	}

	EXOFS_ERR("Failed to kcalloc expected_pages=%u\n",
		  pcol->expected_pages);
	return -ENOMEM;
}

static void pcol_free(struct page_collect *pcol)
{
	bio_put(pcol->bio);
	pcol->bio = NULL;
}

static int pcol_add_page(struct page_collect *pcol, struct page *page,
			 unsigned len)
{
	int added_len = bio_add_pc_page(pcol->req_q, pcol->bio, page, len, 0);
	if (unlikely(len != added_len))
		return -ENOMEM;

	++pcol->nr_pages;
	pcol->length += len;
	return 0;
}

static int update_read_page(struct page *page, int ret)
{
	if (ret == 0) {
		/* Everything is OK */
		SetPageUptodate(page);
		if (PageError(page))
			ClearPageError(page);
	} else if (ret == -EFAULT) {
		/* In this case we were trying to read something that wasn't on
		 * disk yet - return a page full of zeroes.  This should be OK,
		 * because the object should be empty (if there was a write
		 * before this read, the read would be waiting with the page
		 * locked */
		clear_highpage(page);

		SetPageUptodate(page);
		if (PageError(page))
			ClearPageError(page);
		ret = 0; /* recovered error */
		EXOFS_DBGMSG("recovered read error\n");
	} else /* Error */
		SetPageError(page);

	return ret;
}

static void update_write_page(struct page *page, int ret)
{
	if (ret) {
		mapping_set_error(page->mapping, ret);
		SetPageError(page);
	}
	end_page_writeback(page);
}

/* Called at the end of reads, to optionally unlock pages and update their
 * status.
 */
static int __readpages_done(struct osd_request *or, struct page_collect *pcol,
			    bool do_unlock)
{
	struct bio_vec *bvec;
	int i;
	u64 resid;
	u64 good_bytes;
	u64 length = 0;
	int ret = exofs_check_ok_resid(or, &resid, NULL);

	osd_end_request(or);

	if (likely(!ret))
		good_bytes = pcol->length;
	else if (!resid)
		good_bytes = 0;
	else
		good_bytes = pcol->length - resid;

	EXOFS_DBGMSG("readpages_done(0x%lx) good_bytes=0x%llx"
		     " length=0x%lx nr_pages=%u\n",
		     pcol->inode->i_ino, _LLU(good_bytes), pcol->length,
		     pcol->nr_pages);

	__bio_for_each_segment(bvec, pcol->bio, i, 0) {
		struct page *page = bvec->bv_page;
		struct inode *inode = page->mapping->host;
		int page_stat;

		if (inode != pcol->inode)
			continue; /* osd might add more pages at end */

		if (likely(length < good_bytes))
			page_stat = 0;
		else
			page_stat = ret;

		EXOFS_DBGMSG("    readpages_done(0x%lx, 0x%lx) %s\n",
			  inode->i_ino, page->index,
			  page_stat ? "bad_bytes" : "good_bytes");

		ret = update_read_page(page, page_stat);
		if (do_unlock)
			unlock_page(page);
		length += bvec->bv_len;
	}

	pcol_free(pcol);
	EXOFS_DBGMSG("readpages_done END\n");
	return ret;
}

/* callback of async reads */
static void readpages_done(struct osd_request *or, void *p)
{
	struct page_collect *pcol = p;

	__readpages_done(or, pcol, true);
	atomic_dec(&pcol->sbi->s_curr_pending);
	kfree(p);
}

static void _unlock_pcol_pages(struct page_collect *pcol, int ret, int rw)
{
	struct bio_vec *bvec;
	int i;

	__bio_for_each_segment(bvec, pcol->bio, i, 0) {
		struct page *page = bvec->bv_page;

		if (rw == READ)
			update_read_page(page, ret);
		else
			update_write_page(page, ret);

		unlock_page(page);
	}
	pcol_free(pcol);
}

static int read_exec(struct page_collect *pcol, bool is_sync)
{
	struct exofs_i_info *oi = exofs_i(pcol->inode);
	struct osd_obj_id obj = {pcol->sbi->s_pid,
					pcol->inode->i_ino + EXOFS_OBJ_OFF};
	struct osd_request *or = NULL;
	struct page_collect *pcol_copy = NULL;
	loff_t i_start = pcol->pg_first << PAGE_CACHE_SHIFT;
	int ret;

	if (!pcol->bio)
		return 0;

	/* see comment in _readpage() about sync reads */
	WARN_ON(is_sync && (pcol->nr_pages != 1));

	or = osd_start_request(pcol->sbi->s_dev, GFP_KERNEL);
	if (unlikely(!or)) {
		ret = -ENOMEM;
		goto err;
	}

	osd_req_read(or, &obj, i_start, pcol->bio, pcol->length);

	if (is_sync) {
		exofs_sync_op(or, pcol->sbi->s_timeout, oi->i_cred);
		return __readpages_done(or, pcol, false);
	}

	pcol_copy = kmalloc(sizeof(*pcol_copy), GFP_KERNEL);
	if (!pcol_copy) {
		ret = -ENOMEM;
		goto err;
	}

	*pcol_copy = *pcol;
	ret = exofs_async_op(or, readpages_done, pcol_copy, oi->i_cred);
	if (unlikely(ret))
		goto err;

	atomic_inc(&pcol->sbi->s_curr_pending);

	EXOFS_DBGMSG("read_exec obj=0x%llx start=0x%llx length=0x%lx\n",
		  obj.id, _LLU(i_start), pcol->length);

	/* pages ownership was passed to pcol_copy */
	_pcol_reset(pcol);
	return 0;

err:
	if (!is_sync)
		_unlock_pcol_pages(pcol, ret, READ);
	else /* Pages unlocked by caller in sync mode only free bio */
		pcol_free(pcol);

	kfree(pcol_copy);
	if (or)
		osd_end_request(or);
	return ret;
}

/* readpage_strip is called either directly from readpage() or by the VFS from
 * within read_cache_pages(), to add one more page to be read. It will try to
 * collect as many contiguous pages as posible. If a discontinuity is
 * encountered, or it runs out of resources, it will submit the previous segment
 * and will start a new collection. Eventually caller must submit the last
 * segment if present.
 */
static int readpage_strip(void *data, struct page *page)
{
	struct page_collect *pcol = data;
	struct inode *inode = pcol->inode;
	struct exofs_i_info *oi = exofs_i(inode);
	loff_t i_size = i_size_read(inode);
	pgoff_t end_index = i_size >> PAGE_CACHE_SHIFT;
	size_t len;
	int ret;

	/* FIXME: Just for debugging, will be removed */
	if (PageUptodate(page))
		EXOFS_ERR("PageUptodate(0x%lx, 0x%lx)\n", pcol->inode->i_ino,
			  page->index);

	if (page->index < end_index)
		len = PAGE_CACHE_SIZE;
	else if (page->index == end_index)
		len = i_size & ~PAGE_CACHE_MASK;
	else
		len = 0;

	if (!len || !obj_created(oi)) {
		/* this will be out of bounds, or doesn't exist yet.
		 * Current page is cleared and the request is split
		 */
		clear_highpage(page);

		SetPageUptodate(page);
		if (PageError(page))
			ClearPageError(page);

		unlock_page(page);
		EXOFS_DBGMSG("readpage_strip(0x%lx, 0x%lx) empty page,"
			     " splitting\n", inode->i_ino, page->index);

		return read_exec(pcol, false);
	}

try_again:

	if (unlikely(pcol->pg_first == -1)) {
		pcol->pg_first = page->index;
	} else if (unlikely((pcol->pg_first + pcol->nr_pages) !=
		   page->index)) {
		/* Discontinuity detected, split the request */
		ret = read_exec(pcol, false);
		if (unlikely(ret))
			goto fail;
		goto try_again;
	}

	if (!pcol->bio) {
		ret = pcol_try_alloc(pcol);
		if (unlikely(ret))
			goto fail;
	}

	if (len != PAGE_CACHE_SIZE)
		zero_user(page, len, PAGE_CACHE_SIZE - len);

	EXOFS_DBGMSG("    readpage_strip(0x%lx, 0x%lx) len=0x%zx\n",
		     inode->i_ino, page->index, len);

	ret = pcol_add_page(pcol, page, len);
	if (ret) {
		EXOFS_DBGMSG("Failed pcol_add_page pages[i]=%p "
			  "this_len=0x%zx nr_pages=%u length=0x%lx\n",
			  page, len, pcol->nr_pages, pcol->length);

		/* split the request, and start again with current page */
		ret = read_exec(pcol, false);
		if (unlikely(ret))
			goto fail;

		goto try_again;
	}

	return 0;

fail:
	/* SetPageError(page); ??? */
	unlock_page(page);
	return ret;
}

static int exofs_readpages(struct file *file, struct address_space *mapping,
			   struct list_head *pages, unsigned nr_pages)
{
	struct page_collect pcol;
	int ret;

	_pcol_init(&pcol, nr_pages, mapping->host);

	ret = read_cache_pages(mapping, pages, readpage_strip, &pcol);
	if (ret) {
		EXOFS_ERR("read_cache_pages => %d\n", ret);
		return ret;
	}

	return read_exec(&pcol, false);
}

static int _readpage(struct page *page, bool is_sync)
{
	struct page_collect pcol;
	int ret;

	_pcol_init(&pcol, 1, page->mapping->host);

	/* readpage_strip might call read_exec(,async) inside at several places
	 * but this is safe for is_async=0 since read_exec will not do anything
	 * when we have a single page.
	 */
	ret = readpage_strip(&pcol, page);
	if (ret) {
		EXOFS_ERR("_readpage => %d\n", ret);
		return ret;
	}

	return read_exec(&pcol, is_sync);
}

/*
 * We don't need the file
 */
static int exofs_readpage(struct file *file, struct page *page)
{
	return _readpage(page, false);
}

/* Callback for osd_write. All writes are asynchronouse */
static void writepages_done(struct osd_request *or, void *p)
{
	struct page_collect *pcol = p;
	struct bio_vec *bvec;
	int i;
	u64 resid;
	u64  good_bytes;
	u64  length = 0;

	int ret = exofs_check_ok_resid(or, NULL, &resid);

	osd_end_request(or);
	atomic_dec(&pcol->sbi->s_curr_pending);

	if (likely(!ret))
		good_bytes = pcol->length;
	else if (!resid)
		good_bytes = 0;
	else
		good_bytes = pcol->length - resid;

	EXOFS_DBGMSG("writepages_done(0x%lx) good_bytes=0x%llx"
		     " length=0x%lx nr_pages=%u\n",
		     pcol->inode->i_ino, _LLU(good_bytes), pcol->length,
		     pcol->nr_pages);

	__bio_for_each_segment(bvec, pcol->bio, i, 0) {
		struct page *page = bvec->bv_page;
		struct inode *inode = page->mapping->host;
		int page_stat;

		if (inode != pcol->inode)
			continue; /* osd might add more pages to a bio */

		if (likely(length < good_bytes))
			page_stat = 0;
		else
			page_stat = ret;

		update_write_page(page, page_stat);
		unlock_page(page);
		EXOFS_DBGMSG("    writepages_done(0x%lx, 0x%lx) status=%d\n",
			     inode->i_ino, page->index, page_stat);

		length += bvec->bv_len;
	}

	pcol_free(pcol);
	kfree(pcol);
	EXOFS_DBGMSG("writepages_done END\n");
}

static int write_exec(struct page_collect *pcol)
{
	struct exofs_i_info *oi = exofs_i(pcol->inode);
	struct osd_obj_id obj = {pcol->sbi->s_pid,
					pcol->inode->i_ino + EXOFS_OBJ_OFF};
	struct osd_request *or = NULL;
	struct page_collect *pcol_copy = NULL;
	loff_t i_start = pcol->pg_first << PAGE_CACHE_SHIFT;
	int ret;

	if (!pcol->bio)
		return 0;

	or = osd_start_request(pcol->sbi->s_dev, GFP_KERNEL);
	if (unlikely(!or)) {
		EXOFS_ERR("write_exec: Faild to osd_start_request()\n");
		ret = -ENOMEM;
		goto err;
	}

	pcol_copy = kmalloc(sizeof(*pcol_copy), GFP_KERNEL);
	if (!pcol_copy) {
		EXOFS_ERR("write_exec: Faild to kmalloc(pcol)\n");
		ret = -ENOMEM;
		goto err;
	}

	*pcol_copy = *pcol;

	pcol_copy->bio->bi_rw |= (1 << BIO_RW); /* FIXME: bio_set_dir() */
	osd_req_write(or, &obj, i_start, pcol_copy->bio, pcol_copy->length);
	ret = exofs_async_op(or, writepages_done, pcol_copy, oi->i_cred);
	if (unlikely(ret)) {
		EXOFS_ERR("write_exec: exofs_async_op() Faild\n");
		goto err;
	}

	atomic_inc(&pcol->sbi->s_curr_pending);
	EXOFS_DBGMSG("write_exec(0x%lx, 0x%llx) start=0x%llx length=0x%lx\n",
		  pcol->inode->i_ino, pcol->pg_first, _LLU(i_start),
		  pcol->length);
	/* pages ownership was passed to pcol_copy */
	_pcol_reset(pcol);
	return 0;

err:
	_unlock_pcol_pages(pcol, ret, WRITE);
	kfree(pcol_copy);
	if (or)
		osd_end_request(or);
	return ret;
}

/* writepage_strip is called either directly from writepage() or by the VFS from
 * within write_cache_pages(), to add one more page to be written to storage.
 * It will try to collect as many contiguous pages as possible. If a
 * discontinuity is encountered or it runs out of resources it will submit the
 * previous segment and will start a new collection.
 * Eventually caller must submit the last segment if present.
 */
static int writepage_strip(struct page *page,
			   struct writeback_control *wbc_unused, void *data)
{
	struct page_collect *pcol = data;
	struct inode *inode = pcol->inode;
	struct exofs_i_info *oi = exofs_i(inode);
	loff_t i_size = i_size_read(inode);
	pgoff_t end_index = i_size >> PAGE_CACHE_SHIFT;
	size_t len;
	int ret;

	BUG_ON(!PageLocked(page));

	ret = wait_obj_created(oi);
	if (unlikely(ret))
		goto fail;

	if (page->index < end_index)
		/* in this case, the page is within the limits of the file */
		len = PAGE_CACHE_SIZE;
	else {
		len = i_size & ~PAGE_CACHE_MASK;

		if (page->index > end_index || !len) {
			/* in this case, the page is outside the limits
			 * (truncate in progress)
			 */
			ret = write_exec(pcol);
			if (unlikely(ret))
				goto fail;
			if (PageError(page))
				ClearPageError(page);
			unlock_page(page);
			return 0;
		}
	}

try_again:

	if (unlikely(pcol->pg_first == -1)) {
		pcol->pg_first = page->index;
	} else if (unlikely((pcol->pg_first + pcol->nr_pages) !=
		   page->index)) {
		/* Discontinuity detected, split the request */
		ret = write_exec(pcol);
		if (unlikely(ret))
			goto fail;
		goto try_again;
	}

	if (!pcol->bio) {
		ret = pcol_try_alloc(pcol);
		if (unlikely(ret))
			goto fail;
	}

	EXOFS_DBGMSG("    writepage_strip(0x%lx, 0x%lx) len=0x%zx\n",
		     inode->i_ino, page->index, len);

	ret = pcol_add_page(pcol, page, len);
	if (unlikely(ret)) {
		EXOFS_DBGMSG("Failed pcol_add_page "
			     "nr_pages=%u total_length=0x%lx\n",
			     pcol->nr_pages, pcol->length);

		/* split the request, next loop will start again */
		ret = write_exec(pcol);
		if (unlikely(ret)) {
			EXOFS_DBGMSG("write_exec faild => %d", ret);
			goto fail;
		}

		goto try_again;
	}

	BUG_ON(PageWriteback(page));
	set_page_writeback(page);

	return 0;

fail:
	set_bit(AS_EIO, &page->mapping->flags);
	unlock_page(page);
	return ret;
}

static int exofs_writepages(struct address_space *mapping,
		       struct writeback_control *wbc)
{
	struct page_collect pcol;
	long start, end, expected_pages;
	int ret;

	start = wbc->range_start >> PAGE_CACHE_SHIFT;
	end = (wbc->range_end == LLONG_MAX) ?
			start + mapping->nrpages :
			wbc->range_end >> PAGE_CACHE_SHIFT;

	if (start || end)
		expected_pages = min(end - start + 1, 32L);
	else
		expected_pages = mapping->nrpages;

	EXOFS_DBGMSG("inode(0x%lx) wbc->start=0x%llx wbc->end=0x%llx"
		     " m->nrpages=%lu start=0x%lx end=0x%lx\n",
		     mapping->host->i_ino, wbc->range_start, wbc->range_end,
		     mapping->nrpages, start, end);

	_pcol_init(&pcol, expected_pages, mapping->host);

	ret = write_cache_pages(mapping, wbc, writepage_strip, &pcol);
	if (ret) {
		EXOFS_ERR("write_cache_pages => %d\n", ret);
		return ret;
	}

	return write_exec(&pcol);
}

static int exofs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct page_collect pcol;
	int ret;

	_pcol_init(&pcol, 1, page->mapping->host);

	ret = writepage_strip(page, NULL, &pcol);
	if (ret) {
		EXOFS_ERR("exofs_writepage => %d\n", ret);
		return ret;
	}

	return write_exec(&pcol);
}

int exofs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags,
		struct page **pagep, void **fsdata)
{
	int ret = 0;
	struct page *page;

	page = *pagep;
	if (page == NULL) {
		ret = simple_write_begin(file, mapping, pos, len, flags, pagep,
					 fsdata);
		if (ret) {
			EXOFS_DBGMSG("simple_write_begin faild\n");
			return ret;
		}

		page = *pagep;
	}

	 /* read modify write */
	if (!PageUptodate(page) && (len != PAGE_CACHE_SIZE)) {
		ret = _readpage(page, true);
		if (ret) {
			/*SetPageError was done by _readpage. Is it ok?*/
			unlock_page(page);
			EXOFS_DBGMSG("__readpage_filler faild\n");
		}
	}

	return ret;
}

static int exofs_write_begin_export(struct file *file,
		struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags,
		struct page **pagep, void **fsdata)
{
	*pagep = NULL;

	return exofs_write_begin(file, mapping, pos, len, flags, pagep,
					fsdata);
}

const struct address_space_operations exofs_aops = {
	.readpage	= exofs_readpage,
	.readpages	= exofs_readpages,
	.writepage	= exofs_writepage,
	.writepages	= exofs_writepages,
	.write_begin	= exofs_write_begin_export,
	.write_end	= simple_write_end,
};

/******************************************************************************
 * INODE OPERATIONS
 *****************************************************************************/

/*
 * Test whether an inode is a fast symlink.
 */
static inline int exofs_inode_is_fast_symlink(struct inode *inode)
{
	struct exofs_i_info *oi = exofs_i(inode);

	return S_ISLNK(inode->i_mode) && (oi->i_data[0] != 0);
}

/*
 * get_block_t - Fill in a buffer_head
 * An OSD takes care of block allocation so we just fake an allocation by
 * putting in the inode's sector_t in the buffer_head.
 * TODO: What about the case of create==0 and @iblock does not exist in the
 * object?
 */
static int exofs_get_block(struct inode *inode, sector_t iblock,
		    struct buffer_head *bh_result, int create)
{
	map_bh(bh_result, inode->i_sb, iblock);
	return 0;
}

const struct osd_attr g_attr_logical_length = ATTR_DEF(
	OSD_APAGE_OBJECT_INFORMATION, OSD_ATTR_OI_LOGICAL_LENGTH, 8);

/*
 * Truncate a file to the specified size - all we have to do is set the size
 * attribute.  We make sure the object exists first.
 */
void exofs_truncate(struct inode *inode)
{
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;
	struct exofs_i_info *oi = exofs_i(inode);
	struct osd_obj_id obj = {sbi->s_pid, inode->i_ino + EXOFS_OBJ_OFF};
	struct osd_request *or;
	struct osd_attr attr;
	loff_t isize = i_size_read(inode);
	__be64 newsize;
	int ret;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)
	     || S_ISLNK(inode->i_mode)))
		return;
	if (exofs_inode_is_fast_symlink(inode))
		return;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return;
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;

	nobh_truncate_page(inode->i_mapping, isize, exofs_get_block);

	or = osd_start_request(sbi->s_dev, GFP_KERNEL);
	if (unlikely(!or)) {
		EXOFS_ERR("ERROR: exofs_truncate: osd_start_request failed\n");
		goto fail;
	}

	osd_req_set_attributes(or, &obj);

	newsize = cpu_to_be64((u64)isize);
	attr = g_attr_logical_length;
	attr.val_ptr = &newsize;
	osd_req_add_set_attr_list(or, &attr, 1);

	/* if we are about to truncate an object, and it hasn't been
	 * created yet, wait
	 */
	if (unlikely(wait_obj_created(oi)))
		goto fail;

	ret = exofs_sync_op(or, sbi->s_timeout, oi->i_cred);
	osd_end_request(or);
	if (ret)
		goto fail;

out:
	mark_inode_dirty(inode);
	return;
fail:
	make_bad_inode(inode);
	goto out;
}

/*
 * Set inode attributes - just call generic functions.
 */
int exofs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	int error;

	error = inode_change_ok(inode, iattr);
	if (error)
		return error;

	error = inode_setattr(inode, iattr);
	return error;
}

/*
 * Read an inode from the OSD, and return it as is.  We also return the size
 * attribute in the 'sanity' argument if we got compiled with debugging turned
 * on.
 */
static int exofs_get_inode(struct super_block *sb, struct exofs_i_info *oi,
		    struct exofs_fcb *inode, uint64_t *sanity)
{
	struct exofs_sb_info *sbi = sb->s_fs_info;
	struct osd_request *or;
	struct osd_attr attr;
	struct osd_obj_id obj = {sbi->s_pid,
				 oi->vfs_inode.i_ino + EXOFS_OBJ_OFF};
	int ret;

	exofs_make_credential(oi->i_cred, &obj);

	or = osd_start_request(sbi->s_dev, GFP_KERNEL);
	if (unlikely(!or)) {
		EXOFS_ERR("exofs_get_inode: osd_start_request failed.\n");
		return -ENOMEM;
	}
	osd_req_get_attributes(or, &obj);

	/* we need the inode attribute */
	osd_req_add_get_attr_list(or, &g_attr_inode_data, 1);

#ifdef EXOFS_DEBUG_OBJ_ISIZE
	/* we get the size attributes to do a sanity check */
	osd_req_add_get_attr_list(or, &g_attr_logical_length, 1);
#endif

	ret = exofs_sync_op(or, sbi->s_timeout, oi->i_cred);
	if (ret)
		goto out;

	attr = g_attr_inode_data;
	ret = extract_attr_from_req(or, &attr);
	if (ret) {
		EXOFS_ERR("exofs_get_inode: extract_attr_from_req failed\n");
		goto out;
	}

	WARN_ON(attr.len != EXOFS_INO_ATTR_SIZE);
	memcpy(inode, attr.val_ptr, EXOFS_INO_ATTR_SIZE);

#ifdef EXOFS_DEBUG_OBJ_ISIZE
	attr = g_attr_logical_length;
	ret = extract_attr_from_req(or, &attr);
	if (ret) {
		EXOFS_ERR("ERROR: extract attr from or failed\n");
		goto out;
	}
	*sanity = get_unaligned_be64(attr.val_ptr);
#endif

out:
	osd_end_request(or);
	return ret;
}

/*
 * Fill in an inode read from the OSD and set it up for use
 */
struct inode *exofs_iget(struct super_block *sb, unsigned long ino)
{
	struct exofs_i_info *oi;
	struct exofs_fcb fcb;
	struct inode *inode;
	uint64_t uninitialized_var(sanity);
	int ret;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;
	oi = exofs_i(inode);

	/* read the inode from the osd */
	ret = exofs_get_inode(sb, oi, &fcb, &sanity);
	if (ret)
		goto bad_inode;

	init_waitqueue_head(&oi->i_wq);
	set_obj_created(oi);

	/* copy stuff from on-disk struct to in-memory struct */
	inode->i_mode = le16_to_cpu(fcb.i_mode);
	inode->i_uid = le32_to_cpu(fcb.i_uid);
	inode->i_gid = le32_to_cpu(fcb.i_gid);
	inode->i_nlink = le16_to_cpu(fcb.i_links_count);
	inode->i_ctime.tv_sec = (signed)le32_to_cpu(fcb.i_ctime);
	inode->i_atime.tv_sec = (signed)le32_to_cpu(fcb.i_atime);
	inode->i_mtime.tv_sec = (signed)le32_to_cpu(fcb.i_mtime);
	inode->i_ctime.tv_nsec =
		inode->i_atime.tv_nsec = inode->i_mtime.tv_nsec = 0;
	oi->i_commit_size = le64_to_cpu(fcb.i_size);
	i_size_write(inode, oi->i_commit_size);
	inode->i_blkbits = EXOFS_BLKSHIFT;
	inode->i_generation = le32_to_cpu(fcb.i_generation);

#ifdef EXOFS_DEBUG_OBJ_ISIZE
	if ((inode->i_size != sanity) &&
		(!exofs_inode_is_fast_symlink(inode))) {
		EXOFS_ERR("WARNING: Size of object from inode and "
			  "attributes differ (%lld != %llu)\n",
			  inode->i_size, _LLU(sanity));
	}
#endif

	oi->i_dir_start_lookup = 0;

	if ((inode->i_nlink == 0) && (inode->i_mode == 0)) {
		ret = -ESTALE;
		goto bad_inode;
	}

	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		if (fcb.i_data[0])
			inode->i_rdev =
				old_decode_dev(le32_to_cpu(fcb.i_data[0]));
		else
			inode->i_rdev =
				new_decode_dev(le32_to_cpu(fcb.i_data[1]));
	} else {
		memcpy(oi->i_data, fcb.i_data, sizeof(fcb.i_data));
	}

	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &exofs_file_inode_operations;
		inode->i_fop = &exofs_file_operations;
		inode->i_mapping->a_ops = &exofs_aops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &exofs_dir_inode_operations;
		inode->i_fop = &exofs_dir_operations;
		inode->i_mapping->a_ops = &exofs_aops;
	} else if (S_ISLNK(inode->i_mode)) {
		if (exofs_inode_is_fast_symlink(inode))
			inode->i_op = &exofs_fast_symlink_inode_operations;
		else {
			inode->i_op = &exofs_symlink_inode_operations;
			inode->i_mapping->a_ops = &exofs_aops;
		}
	} else {
		inode->i_op = &exofs_special_inode_operations;
		if (fcb.i_data[0])
			init_special_inode(inode, inode->i_mode,
			   old_decode_dev(le32_to_cpu(fcb.i_data[0])));
		else
			init_special_inode(inode, inode->i_mode,
			   new_decode_dev(le32_to_cpu(fcb.i_data[1])));
	}

	unlock_new_inode(inode);
	return inode;

bad_inode:
	iget_failed(inode);
	return ERR_PTR(ret);
}

int __exofs_wait_obj_created(struct exofs_i_info *oi)
{
	if (!obj_created(oi)) {
		BUG_ON(!obj_2bcreated(oi));
		wait_event(oi->i_wq, obj_created(oi));
	}
	return unlikely(is_bad_inode(&oi->vfs_inode)) ? -EIO : 0;
}
/*
 * Callback function from exofs_new_inode().  The important thing is that we
 * set the obj_created flag so that other methods know that the object exists on
 * the OSD.
 */
static void create_done(struct osd_request *or, void *p)
{
	struct inode *inode = p;
	struct exofs_i_info *oi = exofs_i(inode);
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;
	int ret;

	ret = exofs_check_ok(or);
	osd_end_request(or);
	atomic_dec(&sbi->s_curr_pending);

	if (unlikely(ret)) {
		EXOFS_ERR("object=0x%llx creation faild in pid=0x%llx",
			  _LLU(sbi->s_pid), _LLU(inode->i_ino + EXOFS_OBJ_OFF));
		make_bad_inode(inode);
	} else
		set_obj_created(oi);

	atomic_dec(&inode->i_count);
	wake_up(&oi->i_wq);
}

/*
 * Set up a new inode and create an object for it on the OSD
 */
struct inode *exofs_new_inode(struct inode *dir, int mode)
{
	struct super_block *sb;
	struct inode *inode;
	struct exofs_i_info *oi;
	struct exofs_sb_info *sbi;
	struct osd_request *or;
	struct osd_obj_id obj;
	int ret;

	sb = dir->i_sb;
	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	oi = exofs_i(inode);

	init_waitqueue_head(&oi->i_wq);
	set_obj_2bcreated(oi);

	sbi = sb->s_fs_info;

	sb->s_dirt = 1;
	inode->i_uid = current->cred->fsuid;
	if (dir->i_mode & S_ISGID) {
		inode->i_gid = dir->i_gid;
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else {
		inode->i_gid = current->cred->fsgid;
	}
	inode->i_mode = mode;

	inode->i_ino = sbi->s_nextid++;
	inode->i_blkbits = EXOFS_BLKSHIFT;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	oi->i_commit_size = inode->i_size = 0;
	spin_lock(&sbi->s_next_gen_lock);
	inode->i_generation = sbi->s_next_generation++;
	spin_unlock(&sbi->s_next_gen_lock);
	insert_inode_hash(inode);

	mark_inode_dirty(inode);

	obj.partition = sbi->s_pid;
	obj.id = inode->i_ino + EXOFS_OBJ_OFF;
	exofs_make_credential(oi->i_cred, &obj);

	or = osd_start_request(sbi->s_dev, GFP_KERNEL);
	if (unlikely(!or)) {
		EXOFS_ERR("exofs_new_inode: osd_start_request failed\n");
		return ERR_PTR(-ENOMEM);
	}

	osd_req_create_object(or, &obj);

	/* increment the refcount so that the inode will still be around when we
	 * reach the callback
	 */
	atomic_inc(&inode->i_count);

	ret = exofs_async_op(or, create_done, inode, oi->i_cred);
	if (ret) {
		atomic_dec(&inode->i_count);
		osd_end_request(or);
		return ERR_PTR(-EIO);
	}
	atomic_inc(&sbi->s_curr_pending);

	return inode;
}

/*
 * struct to pass two arguments to update_inode's callback
 */
struct updatei_args {
	struct exofs_sb_info	*sbi;
	struct exofs_fcb	fcb;
};

/*
 * Callback function from exofs_update_inode().
 */
static void updatei_done(struct osd_request *or, void *p)
{
	struct updatei_args *args = p;

	osd_end_request(or);

	atomic_dec(&args->sbi->s_curr_pending);

	kfree(args);
}

/*
 * Write the inode to the OSD.  Just fill up the struct, and set the attribute
 * synchronously or asynchronously depending on the do_sync flag.
 */
static int exofs_update_inode(struct inode *inode, int do_sync)
{
	struct exofs_i_info *oi = exofs_i(inode);
	struct super_block *sb = inode->i_sb;
	struct exofs_sb_info *sbi = sb->s_fs_info;
	struct osd_obj_id obj = {sbi->s_pid, inode->i_ino + EXOFS_OBJ_OFF};
	struct osd_request *or;
	struct osd_attr attr;
	struct exofs_fcb *fcb;
	struct updatei_args *args;
	int ret;

	args = kzalloc(sizeof(*args), GFP_KERNEL);
	if (!args)
		return -ENOMEM;

	fcb = &args->fcb;

	fcb->i_mode = cpu_to_le16(inode->i_mode);
	fcb->i_uid = cpu_to_le32(inode->i_uid);
	fcb->i_gid = cpu_to_le32(inode->i_gid);
	fcb->i_links_count = cpu_to_le16(inode->i_nlink);
	fcb->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
	fcb->i_atime = cpu_to_le32(inode->i_atime.tv_sec);
	fcb->i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);
	oi->i_commit_size = i_size_read(inode);
	fcb->i_size = cpu_to_le64(oi->i_commit_size);
	fcb->i_generation = cpu_to_le32(inode->i_generation);

	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		if (old_valid_dev(inode->i_rdev)) {
			fcb->i_data[0] =
				cpu_to_le32(old_encode_dev(inode->i_rdev));
			fcb->i_data[1] = 0;
		} else {
			fcb->i_data[0] = 0;
			fcb->i_data[1] =
				cpu_to_le32(new_encode_dev(inode->i_rdev));
			fcb->i_data[2] = 0;
		}
	} else
		memcpy(fcb->i_data, oi->i_data, sizeof(fcb->i_data));

	or = osd_start_request(sbi->s_dev, GFP_KERNEL);
	if (unlikely(!or)) {
		EXOFS_ERR("exofs_update_inode: osd_start_request failed.\n");
		ret = -ENOMEM;
		goto free_args;
	}

	osd_req_set_attributes(or, &obj);

	attr = g_attr_inode_data;
	attr.val_ptr = fcb;
	osd_req_add_set_attr_list(or, &attr, 1);

	if (!obj_created(oi)) {
		EXOFS_DBGMSG("!obj_created\n");
		BUG_ON(!obj_2bcreated(oi));
		wait_event(oi->i_wq, obj_created(oi));
		EXOFS_DBGMSG("wait_event done\n");
	}

	if (do_sync) {
		ret = exofs_sync_op(or, sbi->s_timeout, oi->i_cred);
		osd_end_request(or);
		goto free_args;
	} else {
		args->sbi = sbi;

		ret = exofs_async_op(or, updatei_done, args, oi->i_cred);
		if (ret) {
			osd_end_request(or);
			goto free_args;
		}
		atomic_inc(&sbi->s_curr_pending);
		goto out; /* deallocation in updatei_done */
	}

free_args:
	kfree(args);
out:
	EXOFS_DBGMSG("ret=>%d\n", ret);
	return ret;
}

int exofs_write_inode(struct inode *inode, int wait)
{
	return exofs_update_inode(inode, wait);
}

/*
 * Callback function from exofs_delete_inode() - don't have much cleaning up to
 * do.
 */
static void delete_done(struct osd_request *or, void *p)
{
	struct exofs_sb_info *sbi;
	osd_end_request(or);
	sbi = p;
	atomic_dec(&sbi->s_curr_pending);
}

/*
 * Called when the refcount of an inode reaches zero.  We remove the object
 * from the OSD here.  We make sure the object was created before we try and
 * delete it.
 */
void exofs_delete_inode(struct inode *inode)
{
	struct exofs_i_info *oi = exofs_i(inode);
	struct super_block *sb = inode->i_sb;
	struct exofs_sb_info *sbi = sb->s_fs_info;
	struct osd_obj_id obj = {sbi->s_pid, inode->i_ino + EXOFS_OBJ_OFF};
	struct osd_request *or;
	int ret;

	truncate_inode_pages(&inode->i_data, 0);

	if (is_bad_inode(inode))
		goto no_delete;

	mark_inode_dirty(inode);
	exofs_update_inode(inode, inode_needs_sync(inode));

	inode->i_size = 0;
	if (inode->i_blocks)
		exofs_truncate(inode);

	clear_inode(inode);

	or = osd_start_request(sbi->s_dev, GFP_KERNEL);
	if (unlikely(!or)) {
		EXOFS_ERR("exofs_delete_inode: osd_start_request failed\n");
		return;
	}

	osd_req_remove_object(or, &obj);

	/* if we are deleting an obj that hasn't been created yet, wait */
	if (!obj_created(oi)) {
		BUG_ON(!obj_2bcreated(oi));
		wait_event(oi->i_wq, obj_created(oi));
	}

	ret = exofs_async_op(or, delete_done, sbi, oi->i_cred);
	if (ret) {
		EXOFS_ERR(
		       "ERROR: @exofs_delete_inode exofs_async_op failed\n");
		osd_end_request(or);
		return;
	}
	atomic_inc(&sbi->s_curr_pending);

	return;

no_delete:
	clear_inode(inode);
}
