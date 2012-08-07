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

#include <linux/slab.h>

#include "exofs.h"

#define EXOFS_DBGMSG2(M...) do {} while (0)

unsigned exofs_max_io_pages(struct ore_layout *layout,
			    unsigned expected_pages)
{
	unsigned pages = min_t(unsigned, expected_pages,
			       layout->max_io_length / PAGE_SIZE);

	return pages;
}

struct page_collect {
	struct exofs_sb_info *sbi;
	struct inode *inode;
	unsigned expected_pages;
	struct ore_io_state *ios;

	struct page **pages;
	unsigned alloc_pages;
	unsigned nr_pages;
	unsigned long length;
	loff_t pg_first; /* keep 64bit also in 32-arches */
	bool read_4_write; /* This means two things: that the read is sync
			    * And the pages should not be unlocked.
			    */
	struct page *that_locked_page;
};

static void _pcol_init(struct page_collect *pcol, unsigned expected_pages,
		       struct inode *inode)
{
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;

	pcol->sbi = sbi;
	pcol->inode = inode;
	pcol->expected_pages = expected_pages;

	pcol->ios = NULL;
	pcol->pages = NULL;
	pcol->alloc_pages = 0;
	pcol->nr_pages = 0;
	pcol->length = 0;
	pcol->pg_first = -1;
	pcol->read_4_write = false;
	pcol->that_locked_page = NULL;
}

static void _pcol_reset(struct page_collect *pcol)
{
	pcol->expected_pages -= min(pcol->nr_pages, pcol->expected_pages);

	pcol->pages = NULL;
	pcol->alloc_pages = 0;
	pcol->nr_pages = 0;
	pcol->length = 0;
	pcol->pg_first = -1;
	pcol->ios = NULL;
	pcol->that_locked_page = NULL;

	/* this is probably the end of the loop but in writes
	 * it might not end here. don't be left with nothing
	 */
	if (!pcol->expected_pages)
		pcol->expected_pages =
				exofs_max_io_pages(&pcol->sbi->layout, ~0);
}

static int pcol_try_alloc(struct page_collect *pcol)
{
	unsigned pages;

	/* TODO: easily support bio chaining */
	pages =  exofs_max_io_pages(&pcol->sbi->layout, pcol->expected_pages);

	for (; pages; pages >>= 1) {
		pcol->pages = kmalloc(pages * sizeof(struct page *),
				      GFP_KERNEL);
		if (likely(pcol->pages)) {
			pcol->alloc_pages = pages;
			return 0;
		}
	}

	EXOFS_ERR("Failed to kmalloc expected_pages=%u\n",
		  pcol->expected_pages);
	return -ENOMEM;
}

static void pcol_free(struct page_collect *pcol)
{
	kfree(pcol->pages);
	pcol->pages = NULL;

	if (pcol->ios) {
		ore_put_io_state(pcol->ios);
		pcol->ios = NULL;
	}
}

static int pcol_add_page(struct page_collect *pcol, struct page *page,
			 unsigned len)
{
	if (unlikely(pcol->nr_pages >= pcol->alloc_pages))
		return -ENOMEM;

	pcol->pages[pcol->nr_pages++] = page;
	pcol->length += len;
	return 0;
}

enum {PAGE_WAS_NOT_IN_IO = 17};
static int update_read_page(struct page *page, int ret)
{
	switch (ret) {
	case 0:
		/* Everything is OK */
		SetPageUptodate(page);
		if (PageError(page))
			ClearPageError(page);
		break;
	case -EFAULT:
		/* In this case we were trying to read something that wasn't on
		 * disk yet - return a page full of zeroes.  This should be OK,
		 * because the object should be empty (if there was a write
		 * before this read, the read would be waiting with the page
		 * locked */
		clear_highpage(page);

		SetPageUptodate(page);
		if (PageError(page))
			ClearPageError(page);
		EXOFS_DBGMSG("recovered read error\n");
		/* fall through */
	case PAGE_WAS_NOT_IN_IO:
		ret = 0; /* recovered error */
		break;
	default:
		SetPageError(page);
	}
	return ret;
}

static void update_write_page(struct page *page, int ret)
{
	if (unlikely(ret == PAGE_WAS_NOT_IN_IO))
		return; /* don't pass start don't collect $200 */

	if (ret) {
		mapping_set_error(page->mapping, ret);
		SetPageError(page);
	}
	end_page_writeback(page);
}

/* Called at the end of reads, to optionally unlock pages and update their
 * status.
 */
static int __readpages_done(struct page_collect *pcol)
{
	int i;
	u64 good_bytes;
	u64 length = 0;
	int ret = ore_check_io(pcol->ios, NULL);

	if (likely(!ret)) {
		good_bytes = pcol->length;
		ret = PAGE_WAS_NOT_IN_IO;
	} else {
		good_bytes = 0;
	}

	EXOFS_DBGMSG2("readpages_done(0x%lx) good_bytes=0x%llx"
		     " length=0x%lx nr_pages=%u\n",
		     pcol->inode->i_ino, _LLU(good_bytes), pcol->length,
		     pcol->nr_pages);

	for (i = 0; i < pcol->nr_pages; i++) {
		struct page *page = pcol->pages[i];
		struct inode *inode = page->mapping->host;
		int page_stat;

		if (inode != pcol->inode)
			continue; /* osd might add more pages at end */

		if (likely(length < good_bytes))
			page_stat = 0;
		else
			page_stat = ret;

		EXOFS_DBGMSG2("    readpages_done(0x%lx, 0x%lx) %s\n",
			  inode->i_ino, page->index,
			  page_stat ? "bad_bytes" : "good_bytes");

		ret = update_read_page(page, page_stat);
		if (!pcol->read_4_write)
			unlock_page(page);
		length += PAGE_SIZE;
	}

	pcol_free(pcol);
	EXOFS_DBGMSG2("readpages_done END\n");
	return ret;
}

/* callback of async reads */
static void readpages_done(struct ore_io_state *ios, void *p)
{
	struct page_collect *pcol = p;

	__readpages_done(pcol);
	atomic_dec(&pcol->sbi->s_curr_pending);
	kfree(pcol);
}

static void _unlock_pcol_pages(struct page_collect *pcol, int ret, int rw)
{
	int i;

	for (i = 0; i < pcol->nr_pages; i++) {
		struct page *page = pcol->pages[i];

		if (rw == READ)
			update_read_page(page, ret);
		else
			update_write_page(page, ret);

		unlock_page(page);
	}
}

static int _maybe_not_all_in_one_io(struct ore_io_state *ios,
	struct page_collect *pcol_src, struct page_collect *pcol)
{
	/* length was wrong or offset was not page aligned */
	BUG_ON(pcol_src->nr_pages < ios->nr_pages);

	if (pcol_src->nr_pages > ios->nr_pages) {
		struct page **src_page;
		unsigned pages_less = pcol_src->nr_pages - ios->nr_pages;
		unsigned long len_less = pcol_src->length - ios->length;
		unsigned i;
		int ret;

		/* This IO was trimmed */
		pcol_src->nr_pages = ios->nr_pages;
		pcol_src->length = ios->length;

		/* Left over pages are passed to the next io */
		pcol->expected_pages += pages_less;
		pcol->nr_pages = pages_less;
		pcol->length = len_less;
		src_page = pcol_src->pages + pcol_src->nr_pages;
		pcol->pg_first = (*src_page)->index;

		ret = pcol_try_alloc(pcol);
		if (unlikely(ret))
			return ret;

		for (i = 0; i < pages_less; ++i)
			pcol->pages[i] = *src_page++;

		EXOFS_DBGMSG("Length was adjusted nr_pages=0x%x "
			"pages_less=0x%x expected_pages=0x%x "
			"next_offset=0x%llx next_len=0x%lx\n",
			pcol_src->nr_pages, pages_less, pcol->expected_pages,
			pcol->pg_first * PAGE_SIZE, pcol->length);
	}
	return 0;
}

static int read_exec(struct page_collect *pcol)
{
	struct exofs_i_info *oi = exofs_i(pcol->inode);
	struct ore_io_state *ios;
	struct page_collect *pcol_copy = NULL;
	int ret;

	if (!pcol->pages)
		return 0;

	if (!pcol->ios) {
		int ret = ore_get_rw_state(&pcol->sbi->layout, &oi->oc, true,
					     pcol->pg_first << PAGE_CACHE_SHIFT,
					     pcol->length, &pcol->ios);

		if (ret)
			return ret;
	}

	ios = pcol->ios;
	ios->pages = pcol->pages;

	if (pcol->read_4_write) {
		ore_read(pcol->ios);
		return __readpages_done(pcol);
	}

	pcol_copy = kmalloc(sizeof(*pcol_copy), GFP_KERNEL);
	if (!pcol_copy) {
		ret = -ENOMEM;
		goto err;
	}

	*pcol_copy = *pcol;
	ios->done = readpages_done;
	ios->private = pcol_copy;

	/* pages ownership was passed to pcol_copy */
	_pcol_reset(pcol);

	ret = _maybe_not_all_in_one_io(ios, pcol_copy, pcol);
	if (unlikely(ret))
		goto err;

	EXOFS_DBGMSG2("read_exec(0x%lx) offset=0x%llx length=0x%llx\n",
		pcol->inode->i_ino, _LLU(ios->offset), _LLU(ios->length));

	ret = ore_read(ios);
	if (unlikely(ret))
		goto err;

	atomic_inc(&pcol->sbi->s_curr_pending);

	return 0;

err:
	if (!pcol->read_4_write)
		_unlock_pcol_pages(pcol, ret, READ);

	pcol_free(pcol);

	kfree(pcol_copy);
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

	BUG_ON(!PageLocked(page));

	/* FIXME: Just for debugging, will be removed */
	if (PageUptodate(page))
		EXOFS_ERR("PageUptodate(0x%lx, 0x%lx)\n", pcol->inode->i_ino,
			  page->index);

	pcol->that_locked_page = page;

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

		if (!pcol->read_4_write)
			unlock_page(page);
		EXOFS_DBGMSG("readpage_strip(0x%lx) empty page len=%zx "
			     "read_4_write=%d index=0x%lx end_index=0x%lx "
			     "splitting\n", inode->i_ino, len,
			     pcol->read_4_write, page->index, end_index);

		return read_exec(pcol);
	}

try_again:

	if (unlikely(pcol->pg_first == -1)) {
		pcol->pg_first = page->index;
	} else if (unlikely((pcol->pg_first + pcol->nr_pages) !=
		   page->index)) {
		/* Discontinuity detected, split the request */
		ret = read_exec(pcol);
		if (unlikely(ret))
			goto fail;
		goto try_again;
	}

	if (!pcol->pages) {
		ret = pcol_try_alloc(pcol);
		if (unlikely(ret))
			goto fail;
	}

	if (len != PAGE_CACHE_SIZE)
		zero_user(page, len, PAGE_CACHE_SIZE - len);

	EXOFS_DBGMSG2("    readpage_strip(0x%lx, 0x%lx) len=0x%zx\n",
		     inode->i_ino, page->index, len);

	ret = pcol_add_page(pcol, page, len);
	if (ret) {
		EXOFS_DBGMSG2("Failed pcol_add_page pages[i]=%p "
			  "this_len=0x%zx nr_pages=%u length=0x%lx\n",
			  page, len, pcol->nr_pages, pcol->length);

		/* split the request, and start again with current page */
		ret = read_exec(pcol);
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

	ret = read_exec(&pcol);
	if (unlikely(ret))
		return ret;

	return read_exec(&pcol);
}

static int _readpage(struct page *page, bool read_4_write)
{
	struct page_collect pcol;
	int ret;

	_pcol_init(&pcol, 1, page->mapping->host);

	pcol.read_4_write = read_4_write;
	ret = readpage_strip(&pcol, page);
	if (ret) {
		EXOFS_ERR("_readpage => %d\n", ret);
		return ret;
	}

	return read_exec(&pcol);
}

/*
 * We don't need the file
 */
static int exofs_readpage(struct file *file, struct page *page)
{
	return _readpage(page, false);
}

/* Callback for osd_write. All writes are asynchronous */
static void writepages_done(struct ore_io_state *ios, void *p)
{
	struct page_collect *pcol = p;
	int i;
	u64  good_bytes;
	u64  length = 0;
	int ret = ore_check_io(ios, NULL);

	atomic_dec(&pcol->sbi->s_curr_pending);

	if (likely(!ret)) {
		good_bytes = pcol->length;
		ret = PAGE_WAS_NOT_IN_IO;
	} else {
		good_bytes = 0;
	}

	EXOFS_DBGMSG2("writepages_done(0x%lx) good_bytes=0x%llx"
		     " length=0x%lx nr_pages=%u\n",
		     pcol->inode->i_ino, _LLU(good_bytes), pcol->length,
		     pcol->nr_pages);

	for (i = 0; i < pcol->nr_pages; i++) {
		struct page *page = pcol->pages[i];
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
		EXOFS_DBGMSG2("    writepages_done(0x%lx, 0x%lx) status=%d\n",
			     inode->i_ino, page->index, page_stat);

		length += PAGE_SIZE;
	}

	pcol_free(pcol);
	kfree(pcol);
	EXOFS_DBGMSG2("writepages_done END\n");
}

static struct page *__r4w_get_page(void *priv, u64 offset, bool *uptodate)
{
	struct page_collect *pcol = priv;
	pgoff_t index = offset / PAGE_SIZE;

	if (!pcol->that_locked_page ||
	    (pcol->that_locked_page->index != index)) {
		struct page *page;
		loff_t i_size = i_size_read(pcol->inode);

		if (offset >= i_size) {
			*uptodate = true;
			EXOFS_DBGMSG("offset >= i_size index=0x%lx\n", index);
			return ZERO_PAGE(0);
		}

		page =  find_get_page(pcol->inode->i_mapping, index);
		if (!page) {
			page = find_or_create_page(pcol->inode->i_mapping,
						   index, GFP_NOFS);
			if (unlikely(!page)) {
				EXOFS_DBGMSG("grab_cache_page Failed "
					"index=0x%llx\n", _LLU(index));
				return NULL;
			}
			unlock_page(page);
		}
		if (PageDirty(page) || PageWriteback(page))
			*uptodate = true;
		else
			*uptodate = PageUptodate(page);
		EXOFS_DBGMSG("index=0x%lx uptodate=%d\n", index, *uptodate);
		return page;
	} else {
		EXOFS_DBGMSG("YES that_locked_page index=0x%lx\n",
			     pcol->that_locked_page->index);
		*uptodate = true;
		return pcol->that_locked_page;
	}
}

static void __r4w_put_page(void *priv, struct page *page)
{
	struct page_collect *pcol = priv;

	if ((pcol->that_locked_page != page) && (ZERO_PAGE(0) != page)) {
		EXOFS_DBGMSG("index=0x%lx\n", page->index);
		page_cache_release(page);
		return;
	}
	EXOFS_DBGMSG("that_locked_page index=0x%lx\n",
		     ZERO_PAGE(0) == page ? -1 : page->index);
}

static const struct _ore_r4w_op _r4w_op = {
	.get_page = &__r4w_get_page,
	.put_page = &__r4w_put_page,
};

static int write_exec(struct page_collect *pcol)
{
	struct exofs_i_info *oi = exofs_i(pcol->inode);
	struct ore_io_state *ios;
	struct page_collect *pcol_copy = NULL;
	int ret;

	if (!pcol->pages)
		return 0;

	BUG_ON(pcol->ios);
	ret = ore_get_rw_state(&pcol->sbi->layout, &oi->oc, false,
				 pcol->pg_first << PAGE_CACHE_SHIFT,
				 pcol->length, &pcol->ios);
	if (unlikely(ret))
		goto err;

	pcol_copy = kmalloc(sizeof(*pcol_copy), GFP_KERNEL);
	if (!pcol_copy) {
		EXOFS_ERR("write_exec: Failed to kmalloc(pcol)\n");
		ret = -ENOMEM;
		goto err;
	}

	*pcol_copy = *pcol;

	ios = pcol->ios;
	ios->pages = pcol_copy->pages;
	ios->done = writepages_done;
	ios->r4w = &_r4w_op;
	ios->private = pcol_copy;

	/* pages ownership was passed to pcol_copy */
	_pcol_reset(pcol);

	ret = _maybe_not_all_in_one_io(ios, pcol_copy, pcol);
	if (unlikely(ret))
		goto err;

	EXOFS_DBGMSG2("write_exec(0x%lx) offset=0x%llx length=0x%llx\n",
		pcol->inode->i_ino, _LLU(ios->offset), _LLU(ios->length));

	ret = ore_write(ios);
	if (unlikely(ret)) {
		EXOFS_ERR("write_exec: ore_write() Failed\n");
		goto err;
	}

	atomic_inc(&pcol->sbi->s_curr_pending);
	return 0;

err:
	_unlock_pcol_pages(pcol, ret, WRITE);
	pcol_free(pcol);
	kfree(pcol_copy);

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
			EXOFS_DBGMSG("writepage_strip(0x%lx, 0x%lx) "
				     "outside the limits\n",
				     inode->i_ino, page->index);
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

		EXOFS_DBGMSG("writepage_strip(0x%lx, 0x%lx) Discontinuity\n",
			     inode->i_ino, page->index);
		goto try_again;
	}

	if (!pcol->pages) {
		ret = pcol_try_alloc(pcol);
		if (unlikely(ret))
			goto fail;
	}

	EXOFS_DBGMSG2("    writepage_strip(0x%lx, 0x%lx) len=0x%zx\n",
		     inode->i_ino, page->index, len);

	ret = pcol_add_page(pcol, page, len);
	if (unlikely(ret)) {
		EXOFS_DBGMSG2("Failed pcol_add_page "
			     "nr_pages=%u total_length=0x%lx\n",
			     pcol->nr_pages, pcol->length);

		/* split the request, next loop will start again */
		ret = write_exec(pcol);
		if (unlikely(ret)) {
			EXOFS_DBGMSG("write_exec failed => %d", ret);
			goto fail;
		}

		goto try_again;
	}

	BUG_ON(PageWriteback(page));
	set_page_writeback(page);

	return 0;

fail:
	EXOFS_DBGMSG("Error: writepage_strip(0x%lx, 0x%lx)=>%d\n",
		     inode->i_ino, page->index, ret);
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
		expected_pages = end - start + 1;
	else
		expected_pages = mapping->nrpages;

	if (expected_pages < 32L)
		expected_pages = 32L;

	EXOFS_DBGMSG2("inode(0x%lx) wbc->start=0x%llx wbc->end=0x%llx "
		     "nrpages=%lu start=0x%lx end=0x%lx expected_pages=%ld\n",
		     mapping->host->i_ino, wbc->range_start, wbc->range_end,
		     mapping->nrpages, start, end, expected_pages);

	_pcol_init(&pcol, expected_pages, mapping->host);

	ret = write_cache_pages(mapping, wbc, writepage_strip, &pcol);
	if (unlikely(ret)) {
		EXOFS_ERR("write_cache_pages => %d\n", ret);
		return ret;
	}

	ret = write_exec(&pcol);
	if (unlikely(ret))
		return ret;

	if (wbc->sync_mode == WB_SYNC_ALL) {
		return write_exec(&pcol); /* pump the last reminder */
	} else if (pcol.nr_pages) {
		/* not SYNC let the reminder join the next writeout */
		unsigned i;

		for (i = 0; i < pcol.nr_pages; i++) {
			struct page *page = pcol.pages[i];

			end_page_writeback(page);
			set_page_dirty(page);
			unlock_page(page);
		}
	}
	return 0;
}

/*
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
*/
/* i_mutex held using inode->i_size directly */
static void _write_failed(struct inode *inode, loff_t to)
{
	if (to > inode->i_size)
		truncate_pagecache(inode, to, inode->i_size);
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
			EXOFS_DBGMSG("simple_write_begin failed\n");
			goto out;
		}

		page = *pagep;
	}

	 /* read modify write */
	if (!PageUptodate(page) && (len != PAGE_CACHE_SIZE)) {
		loff_t i_size = i_size_read(mapping->host);
		pgoff_t end_index = i_size >> PAGE_CACHE_SHIFT;
		size_t rlen;

		if (page->index < end_index)
			rlen = PAGE_CACHE_SIZE;
		else if (page->index == end_index)
			rlen = i_size & ~PAGE_CACHE_MASK;
		else
			rlen = 0;

		if (!rlen) {
			clear_highpage(page);
			SetPageUptodate(page);
			goto out;
		}

		ret = _readpage(page, true);
		if (ret) {
			/*SetPageError was done by _readpage. Is it ok?*/
			unlock_page(page);
			EXOFS_DBGMSG("__readpage failed\n");
		}
	}
out:
	if (unlikely(ret))
		_write_failed(mapping->host, pos + len);

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

static int exofs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;
	/* According to comment in simple_write_end i_mutex is held */
	loff_t i_size = inode->i_size;
	int ret;

	ret = simple_write_end(file, mapping,pos, len, copied, page, fsdata);
	if (unlikely(ret))
		_write_failed(inode, pos + len);

	/* TODO: once simple_write_end marks inode dirty remove */
	if (i_size != inode->i_size)
		mark_inode_dirty(inode);
	return ret;
}

static int exofs_releasepage(struct page *page, gfp_t gfp)
{
	EXOFS_DBGMSG("page 0x%lx\n", page->index);
	WARN_ON(1);
	return 0;
}

static void exofs_invalidatepage(struct page *page, unsigned long offset)
{
	EXOFS_DBGMSG("page 0x%lx offset 0x%lx\n", page->index, offset);
	WARN_ON(1);
}

const struct address_space_operations exofs_aops = {
	.readpage	= exofs_readpage,
	.readpages	= exofs_readpages,
	.writepage	= NULL,
	.writepages	= exofs_writepages,
	.write_begin	= exofs_write_begin_export,
	.write_end	= exofs_write_end,
	.releasepage	= exofs_releasepage,
	.set_page_dirty	= __set_page_dirty_nobuffers,
	.invalidatepage = exofs_invalidatepage,

	/* Not implemented Yet */
	.bmap		= NULL, /* TODO: use osd's OSD_ACT_READ_MAP */
	.direct_IO	= NULL, /* TODO: Should be trivial to do */

	/* With these NULL has special meaning or default is not exported */
	.get_xip_mem	= NULL,
	.migratepage	= NULL,
	.launder_page	= NULL,
	.is_partially_uptodate = NULL,
	.error_remove_page = NULL,
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

static int _do_truncate(struct inode *inode, loff_t newsize)
{
	struct exofs_i_info *oi = exofs_i(inode);
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;
	int ret;

	inode->i_mtime = inode->i_ctime = CURRENT_TIME;

	ret = ore_truncate(&sbi->layout, &oi->oc, (u64)newsize);
	if (likely(!ret))
		truncate_setsize(inode, newsize);

	EXOFS_DBGMSG("(0x%lx) size=0x%llx ret=>%d\n",
		     inode->i_ino, newsize, ret);
	return ret;
}

/*
 * Set inode attributes - update size attribute on OSD if needed,
 *                        otherwise just call generic functions.
 */
int exofs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	int error;

	/* if we are about to modify an object, and it hasn't been
	 * created yet, wait
	 */
	error = wait_obj_created(exofs_i(inode));
	if (unlikely(error))
		return error;

	error = inode_change_ok(inode, iattr);
	if (unlikely(error))
		return error;

	if ((iattr->ia_valid & ATTR_SIZE) &&
	    iattr->ia_size != i_size_read(inode)) {
		error = _do_truncate(inode, iattr->ia_size);
		if (unlikely(error))
			return error;
	}

	setattr_copy(inode, iattr);
	mark_inode_dirty(inode);
	return 0;
}

static const struct osd_attr g_attr_inode_file_layout = ATTR_DEF(
	EXOFS_APAGE_FS_DATA,
	EXOFS_ATTR_INODE_FILE_LAYOUT,
	0);
static const struct osd_attr g_attr_inode_dir_layout = ATTR_DEF(
	EXOFS_APAGE_FS_DATA,
	EXOFS_ATTR_INODE_DIR_LAYOUT,
	0);

/*
 * Read the Linux inode info from the OSD, and return it as is. In exofs the
 * inode info is in an application specific page/attribute of the osd-object.
 */
static int exofs_get_inode(struct super_block *sb, struct exofs_i_info *oi,
		    struct exofs_fcb *inode)
{
	struct exofs_sb_info *sbi = sb->s_fs_info;
	struct osd_attr attrs[] = {
		[0] = g_attr_inode_data,
		[1] = g_attr_inode_file_layout,
		[2] = g_attr_inode_dir_layout,
	};
	struct ore_io_state *ios;
	struct exofs_on_disk_inode_layout *layout;
	int ret;

	ret = ore_get_io_state(&sbi->layout, &oi->oc, &ios);
	if (unlikely(ret)) {
		EXOFS_ERR("%s: ore_get_io_state failed.\n", __func__);
		return ret;
	}

	attrs[1].len = exofs_on_disk_inode_layout_size(sbi->oc.numdevs);
	attrs[2].len = exofs_on_disk_inode_layout_size(sbi->oc.numdevs);

	ios->in_attr = attrs;
	ios->in_attr_len = ARRAY_SIZE(attrs);

	ret = ore_read(ios);
	if (unlikely(ret)) {
		EXOFS_ERR("object(0x%llx) corrupted, return empty file=>%d\n",
			  _LLU(oi->one_comp.obj.id), ret);
		memset(inode, 0, sizeof(*inode));
		inode->i_mode = 0040000 | (0777 & ~022);
		/* If object is lost on target we might as well enable it's
		 * delete.
		 */
		if ((ret == -ENOENT) || (ret == -EINVAL))
			ret = 0;
		goto out;
	}

	ret = extract_attr_from_ios(ios, &attrs[0]);
	if (ret) {
		EXOFS_ERR("%s: extract_attr of inode_data failed\n", __func__);
		goto out;
	}
	WARN_ON(attrs[0].len != EXOFS_INO_ATTR_SIZE);
	memcpy(inode, attrs[0].val_ptr, EXOFS_INO_ATTR_SIZE);

	ret = extract_attr_from_ios(ios, &attrs[1]);
	if (ret) {
		EXOFS_ERR("%s: extract_attr of inode_data failed\n", __func__);
		goto out;
	}
	if (attrs[1].len) {
		layout = attrs[1].val_ptr;
		if (layout->gen_func != cpu_to_le16(LAYOUT_MOVING_WINDOW)) {
			EXOFS_ERR("%s: unsupported files layout %d\n",
				__func__, layout->gen_func);
			ret = -ENOTSUPP;
			goto out;
		}
	}

	ret = extract_attr_from_ios(ios, &attrs[2]);
	if (ret) {
		EXOFS_ERR("%s: extract_attr of inode_data failed\n", __func__);
		goto out;
	}
	if (attrs[2].len) {
		layout = attrs[2].val_ptr;
		if (layout->gen_func != cpu_to_le16(LAYOUT_MOVING_WINDOW)) {
			EXOFS_ERR("%s: unsupported meta-data layout %d\n",
				__func__, layout->gen_func);
			ret = -ENOTSUPP;
			goto out;
		}
	}

out:
	ore_put_io_state(ios);
	return ret;
}

static void __oi_init(struct exofs_i_info *oi)
{
	init_waitqueue_head(&oi->i_wq);
	oi->i_flags = 0;
}
/*
 * Fill in an inode read from the OSD and set it up for use
 */
struct inode *exofs_iget(struct super_block *sb, unsigned long ino)
{
	struct exofs_i_info *oi;
	struct exofs_fcb fcb;
	struct inode *inode;
	int ret;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;
	oi = exofs_i(inode);
	__oi_init(oi);
	exofs_init_comps(&oi->oc, &oi->one_comp, sb->s_fs_info,
			 exofs_oi_objno(oi));

	/* read the inode from the osd */
	ret = exofs_get_inode(sb, oi, &fcb);
	if (ret)
		goto bad_inode;

	set_obj_created(oi);

	/* copy stuff from on-disk struct to in-memory struct */
	inode->i_mode = le16_to_cpu(fcb.i_mode);
	inode->i_uid = le32_to_cpu(fcb.i_uid);
	inode->i_gid = le32_to_cpu(fcb.i_gid);
	set_nlink(inode, le16_to_cpu(fcb.i_links_count));
	inode->i_ctime.tv_sec = (signed)le32_to_cpu(fcb.i_ctime);
	inode->i_atime.tv_sec = (signed)le32_to_cpu(fcb.i_atime);
	inode->i_mtime.tv_sec = (signed)le32_to_cpu(fcb.i_mtime);
	inode->i_ctime.tv_nsec =
		inode->i_atime.tv_nsec = inode->i_mtime.tv_nsec = 0;
	oi->i_commit_size = le64_to_cpu(fcb.i_size);
	i_size_write(inode, oi->i_commit_size);
	inode->i_blkbits = EXOFS_BLKSHIFT;
	inode->i_generation = le32_to_cpu(fcb.i_generation);

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

	inode->i_mapping->backing_dev_info = sb->s_bdi;
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
		EXOFS_DBGMSG("!obj_created\n");
		BUG_ON(!obj_2bcreated(oi));
		wait_event(oi->i_wq, obj_created(oi));
		EXOFS_DBGMSG("wait_event done\n");
	}
	return unlikely(is_bad_inode(&oi->vfs_inode)) ? -EIO : 0;
}

/*
 * Callback function from exofs_new_inode().  The important thing is that we
 * set the obj_created flag so that other methods know that the object exists on
 * the OSD.
 */
static void create_done(struct ore_io_state *ios, void *p)
{
	struct inode *inode = p;
	struct exofs_i_info *oi = exofs_i(inode);
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;
	int ret;

	ret = ore_check_io(ios, NULL);
	ore_put_io_state(ios);

	atomic_dec(&sbi->s_curr_pending);

	if (unlikely(ret)) {
		EXOFS_ERR("object=0x%llx creation failed in pid=0x%llx",
			  _LLU(exofs_oi_objno(oi)),
			  _LLU(oi->one_comp.obj.partition));
		/*TODO: When FS is corrupted creation can fail, object already
		 * exist. Get rid of this asynchronous creation, if exist
		 * increment the obj counter and try the next object. Until we
		 * succeed. All these dangling objects will be made into lost
		 * files by chkfs.exofs
		 */
	}

	set_obj_created(oi);

	wake_up(&oi->i_wq);
}

/*
 * Set up a new inode and create an object for it on the OSD
 */
struct inode *exofs_new_inode(struct inode *dir, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct exofs_sb_info *sbi = sb->s_fs_info;
	struct inode *inode;
	struct exofs_i_info *oi;
	struct ore_io_state *ios;
	int ret;

	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	oi = exofs_i(inode);
	__oi_init(oi);

	set_obj_2bcreated(oi);

	inode->i_mapping->backing_dev_info = sb->s_bdi;
	inode_init_owner(inode, dir, mode);
	inode->i_ino = sbi->s_nextid++;
	inode->i_blkbits = EXOFS_BLKSHIFT;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	oi->i_commit_size = inode->i_size = 0;
	spin_lock(&sbi->s_next_gen_lock);
	inode->i_generation = sbi->s_next_generation++;
	spin_unlock(&sbi->s_next_gen_lock);
	insert_inode_hash(inode);

	exofs_init_comps(&oi->oc, &oi->one_comp, sb->s_fs_info,
			 exofs_oi_objno(oi));
	exofs_sbi_write_stats(sbi); /* Make sure new sbi->s_nextid is on disk */

	mark_inode_dirty(inode);

	ret = ore_get_io_state(&sbi->layout, &oi->oc, &ios);
	if (unlikely(ret)) {
		EXOFS_ERR("exofs_new_inode: ore_get_io_state failed\n");
		return ERR_PTR(ret);
	}

	ios->done = create_done;
	ios->private = inode;

	ret = ore_create(ios);
	if (ret) {
		ore_put_io_state(ios);
		return ERR_PTR(ret);
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
static void updatei_done(struct ore_io_state *ios, void *p)
{
	struct updatei_args *args = p;

	ore_put_io_state(ios);

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
	struct ore_io_state *ios;
	struct osd_attr attr;
	struct exofs_fcb *fcb;
	struct updatei_args *args;
	int ret;

	args = kzalloc(sizeof(*args), GFP_KERNEL);
	if (!args) {
		EXOFS_DBGMSG("Failed kzalloc of args\n");
		return -ENOMEM;
	}

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

	ret = ore_get_io_state(&sbi->layout, &oi->oc, &ios);
	if (unlikely(ret)) {
		EXOFS_ERR("%s: ore_get_io_state failed.\n", __func__);
		goto free_args;
	}

	attr = g_attr_inode_data;
	attr.val_ptr = fcb;
	ios->out_attr_len = 1;
	ios->out_attr = &attr;

	wait_obj_created(oi);

	if (!do_sync) {
		args->sbi = sbi;
		ios->done = updatei_done;
		ios->private = args;
	}

	ret = ore_write(ios);
	if (!do_sync && !ret) {
		atomic_inc(&sbi->s_curr_pending);
		goto out; /* deallocation in updatei_done */
	}

	ore_put_io_state(ios);
free_args:
	kfree(args);
out:
	EXOFS_DBGMSG("(0x%lx) do_sync=%d ret=>%d\n",
		     inode->i_ino, do_sync, ret);
	return ret;
}

int exofs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	/* FIXME: fix fsync and use wbc->sync_mode == WB_SYNC_ALL */
	return exofs_update_inode(inode, 1);
}

/*
 * Callback function from exofs_delete_inode() - don't have much cleaning up to
 * do.
 */
static void delete_done(struct ore_io_state *ios, void *p)
{
	struct exofs_sb_info *sbi = p;

	ore_put_io_state(ios);

	atomic_dec(&sbi->s_curr_pending);
}

/*
 * Called when the refcount of an inode reaches zero.  We remove the object
 * from the OSD here.  We make sure the object was created before we try and
 * delete it.
 */
void exofs_evict_inode(struct inode *inode)
{
	struct exofs_i_info *oi = exofs_i(inode);
	struct super_block *sb = inode->i_sb;
	struct exofs_sb_info *sbi = sb->s_fs_info;
	struct ore_io_state *ios;
	int ret;

	truncate_inode_pages(&inode->i_data, 0);

	/* TODO: should do better here */
	if (inode->i_nlink || is_bad_inode(inode))
		goto no_delete;

	inode->i_size = 0;
	clear_inode(inode);

	/* if we are deleting an obj that hasn't been created yet, wait.
	 * This also makes sure that create_done cannot be called with an
	 * already evicted inode.
	 */
	wait_obj_created(oi);
	/* ignore the error, attempt a remove anyway */

	/* Now Remove the OSD objects */
	ret = ore_get_io_state(&sbi->layout, &oi->oc, &ios);
	if (unlikely(ret)) {
		EXOFS_ERR("%s: ore_get_io_state failed\n", __func__);
		return;
	}

	ios->done = delete_done;
	ios->private = sbi;

	ret = ore_remove(ios);
	if (ret) {
		EXOFS_ERR("%s: ore_remove failed\n", __func__);
		ore_put_io_state(ios);
		return;
	}
	atomic_inc(&sbi->s_curr_pending);

	return;

no_delete:
	clear_inode(inode);
}
