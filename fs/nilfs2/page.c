/*
 * page.c - buffer/page management specific to NILFS
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Written by Ryusuke Konishi and Seiji Kihara.
 */

#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/swap.h>
#include <linux/bitops.h>
#include <linux/page-flags.h>
#include <linux/list.h>
#include <linux/highmem.h>
#include <linux/pagevec.h>
#include <linux/gfp.h>
#include "nilfs.h"
#include "page.h"
#include "mdt.h"


#define NILFS_BUFFER_INHERENT_BITS					\
	(BIT(BH_Uptodate) | BIT(BH_Mapped) | BIT(BH_NILFS_Node) |	\
	 BIT(BH_NILFS_Volatile) | BIT(BH_NILFS_Checked))

static struct buffer_head *
__nilfs_get_page_block(struct page *page, unsigned long block, pgoff_t index,
		       int blkbits, unsigned long b_state)

{
	unsigned long first_block;
	struct buffer_head *bh;

	if (!page_has_buffers(page))
		create_empty_buffers(page, 1 << blkbits, b_state);

	first_block = (unsigned long)index << (PAGE_SHIFT - blkbits);
	bh = nilfs_page_get_nth_block(page, block - first_block);

	touch_buffer(bh);
	wait_on_buffer(bh);
	return bh;
}

struct buffer_head *nilfs_grab_buffer(struct inode *inode,
				      struct address_space *mapping,
				      unsigned long blkoff,
				      unsigned long b_state)
{
	int blkbits = inode->i_blkbits;
	pgoff_t index = blkoff >> (PAGE_SHIFT - blkbits);
	struct page *page;
	struct buffer_head *bh;

	page = grab_cache_page(mapping, index);
	if (unlikely(!page))
		return NULL;

	bh = __nilfs_get_page_block(page, blkoff, index, blkbits, b_state);
	if (unlikely(!bh)) {
		unlock_page(page);
		put_page(page);
		return NULL;
	}
	return bh;
}

/**
 * nilfs_forget_buffer - discard dirty state
 * @inode: owner inode of the buffer
 * @bh: buffer head of the buffer to be discarded
 */
void nilfs_forget_buffer(struct buffer_head *bh)
{
	struct page *page = bh->b_page;
	const unsigned long clear_bits =
		(BIT(BH_Uptodate) | BIT(BH_Dirty) | BIT(BH_Mapped) |
		 BIT(BH_Async_Write) | BIT(BH_NILFS_Volatile) |
		 BIT(BH_NILFS_Checked) | BIT(BH_NILFS_Redirected));

	lock_buffer(bh);
	set_mask_bits(&bh->b_state, clear_bits, 0);
	if (nilfs_page_buffers_clean(page))
		__nilfs_clear_page_dirty(page);

	bh->b_blocknr = -1;
	ClearPageUptodate(page);
	ClearPageMappedToDisk(page);
	unlock_buffer(bh);
	brelse(bh);
}

/**
 * nilfs_copy_buffer -- copy buffer data and flags
 * @dbh: destination buffer
 * @sbh: source buffer
 */
void nilfs_copy_buffer(struct buffer_head *dbh, struct buffer_head *sbh)
{
	void *kaddr0, *kaddr1;
	unsigned long bits;
	struct page *spage = sbh->b_page, *dpage = dbh->b_page;
	struct buffer_head *bh;

	kaddr0 = kmap_atomic(spage);
	kaddr1 = kmap_atomic(dpage);
	memcpy(kaddr1 + bh_offset(dbh), kaddr0 + bh_offset(sbh), sbh->b_size);
	kunmap_atomic(kaddr1);
	kunmap_atomic(kaddr0);

	dbh->b_state = sbh->b_state & NILFS_BUFFER_INHERENT_BITS;
	dbh->b_blocknr = sbh->b_blocknr;
	dbh->b_bdev = sbh->b_bdev;

	bh = dbh;
	bits = sbh->b_state & (BIT(BH_Uptodate) | BIT(BH_Mapped));
	while ((bh = bh->b_this_page) != dbh) {
		lock_buffer(bh);
		bits &= bh->b_state;
		unlock_buffer(bh);
	}
	if (bits & BIT(BH_Uptodate))
		SetPageUptodate(dpage);
	else
		ClearPageUptodate(dpage);
	if (bits & BIT(BH_Mapped))
		SetPageMappedToDisk(dpage);
	else
		ClearPageMappedToDisk(dpage);
}

/**
 * nilfs_page_buffers_clean - check if a page has dirty buffers or not.
 * @page: page to be checked
 *
 * nilfs_page_buffers_clean() returns zero if the page has dirty buffers.
 * Otherwise, it returns non-zero value.
 */
int nilfs_page_buffers_clean(struct page *page)
{
	struct buffer_head *bh, *head;

	bh = head = page_buffers(page);
	do {
		if (buffer_dirty(bh))
			return 0;
		bh = bh->b_this_page;
	} while (bh != head);
	return 1;
}

void nilfs_page_bug(struct page *page)
{
	struct address_space *m;
	unsigned long ino;

	if (unlikely(!page)) {
		printk(KERN_CRIT "NILFS_PAGE_BUG(NULL)\n");
		return;
	}

	m = page->mapping;
	ino = m ? m->host->i_ino : 0;

	printk(KERN_CRIT "NILFS_PAGE_BUG(%p): cnt=%d index#=%llu flags=0x%lx "
	       "mapping=%p ino=%lu\n",
	       page, page_ref_count(page),
	       (unsigned long long)page->index, page->flags, m, ino);

	if (page_has_buffers(page)) {
		struct buffer_head *bh, *head;
		int i = 0;

		bh = head = page_buffers(page);
		do {
			printk(KERN_CRIT
			       " BH[%d] %p: cnt=%d block#=%llu state=0x%lx\n",
			       i++, bh, atomic_read(&bh->b_count),
			       (unsigned long long)bh->b_blocknr, bh->b_state);
			bh = bh->b_this_page;
		} while (bh != head);
	}
}

/**
 * nilfs_copy_page -- copy the page with buffers
 * @dst: destination page
 * @src: source page
 * @copy_dirty: flag whether to copy dirty states on the page's buffer heads.
 *
 * This function is for both data pages and btnode pages.  The dirty flag
 * should be treated by caller.  The page must not be under i/o.
 * Both src and dst page must be locked
 */
static void nilfs_copy_page(struct page *dst, struct page *src, int copy_dirty)
{
	struct buffer_head *dbh, *dbufs, *sbh, *sbufs;
	unsigned long mask = NILFS_BUFFER_INHERENT_BITS;

	BUG_ON(PageWriteback(dst));

	sbh = sbufs = page_buffers(src);
	if (!page_has_buffers(dst))
		create_empty_buffers(dst, sbh->b_size, 0);

	if (copy_dirty)
		mask |= BIT(BH_Dirty);

	dbh = dbufs = page_buffers(dst);
	do {
		lock_buffer(sbh);
		lock_buffer(dbh);
		dbh->b_state = sbh->b_state & mask;
		dbh->b_blocknr = sbh->b_blocknr;
		dbh->b_bdev = sbh->b_bdev;
		sbh = sbh->b_this_page;
		dbh = dbh->b_this_page;
	} while (dbh != dbufs);

	copy_highpage(dst, src);

	if (PageUptodate(src) && !PageUptodate(dst))
		SetPageUptodate(dst);
	else if (!PageUptodate(src) && PageUptodate(dst))
		ClearPageUptodate(dst);
	if (PageMappedToDisk(src) && !PageMappedToDisk(dst))
		SetPageMappedToDisk(dst);
	else if (!PageMappedToDisk(src) && PageMappedToDisk(dst))
		ClearPageMappedToDisk(dst);

	do {
		unlock_buffer(sbh);
		unlock_buffer(dbh);
		sbh = sbh->b_this_page;
		dbh = dbh->b_this_page;
	} while (dbh != dbufs);
}

int nilfs_copy_dirty_pages(struct address_space *dmap,
			   struct address_space *smap)
{
	struct pagevec pvec;
	unsigned int i;
	pgoff_t index = 0;
	int err = 0;

	pagevec_init(&pvec);
repeat:
	if (!pagevec_lookup_tag(&pvec, smap, &index, PAGECACHE_TAG_DIRTY))
		return 0;

	for (i = 0; i < pagevec_count(&pvec); i++) {
		struct page *page = pvec.pages[i], *dpage;

		lock_page(page);
		if (unlikely(!PageDirty(page)))
			NILFS_PAGE_BUG(page, "inconsistent dirty state");

		dpage = grab_cache_page(dmap, page->index);
		if (unlikely(!dpage)) {
			/* No empty page is added to the page cache */
			err = -ENOMEM;
			unlock_page(page);
			break;
		}
		if (unlikely(!page_has_buffers(page)))
			NILFS_PAGE_BUG(page,
				       "found empty page in dat page cache");

		nilfs_copy_page(dpage, page, 1);
		__set_page_dirty_nobuffers(dpage);

		unlock_page(dpage);
		put_page(dpage);
		unlock_page(page);
	}
	pagevec_release(&pvec);
	cond_resched();

	if (likely(!err))
		goto repeat;
	return err;
}

/**
 * nilfs_copy_back_pages -- copy back pages to original cache from shadow cache
 * @dmap: destination page cache
 * @smap: source page cache
 *
 * No pages must no be added to the cache during this process.
 * This must be ensured by the caller.
 */
void nilfs_copy_back_pages(struct address_space *dmap,
			   struct address_space *smap)
{
	struct pagevec pvec;
	unsigned int i, n;
	pgoff_t index = 0;
	int err;

	pagevec_init(&pvec);
repeat:
	n = pagevec_lookup(&pvec, smap, &index);
	if (!n)
		return;

	for (i = 0; i < pagevec_count(&pvec); i++) {
		struct page *page = pvec.pages[i], *dpage;
		pgoff_t offset = page->index;

		lock_page(page);
		dpage = find_lock_page(dmap, offset);
		if (dpage) {
			/* override existing page on the destination cache */
			WARN_ON(PageDirty(dpage));
			nilfs_copy_page(dpage, page, 0);
			unlock_page(dpage);
			put_page(dpage);
		} else {
			struct page *page2;

			/* move the page to the destination cache */
			xa_lock_irq(&smap->i_pages);
			page2 = radix_tree_delete(&smap->i_pages, offset);
			WARN_ON(page2 != page);

			smap->nrpages--;
			xa_unlock_irq(&smap->i_pages);

			xa_lock_irq(&dmap->i_pages);
			err = radix_tree_insert(&dmap->i_pages, offset, page);
			if (unlikely(err < 0)) {
				WARN_ON(err == -EEXIST);
				page->mapping = NULL;
				put_page(page); /* for cache */
			} else {
				page->mapping = dmap;
				dmap->nrpages++;
				if (PageDirty(page))
					radix_tree_tag_set(&dmap->i_pages,
							   offset,
							   PAGECACHE_TAG_DIRTY);
			}
			xa_unlock_irq(&dmap->i_pages);
		}
		unlock_page(page);
	}
	pagevec_release(&pvec);
	cond_resched();

	goto repeat;
}

/**
 * nilfs_clear_dirty_pages - discard dirty pages in address space
 * @mapping: address space with dirty pages for discarding
 * @silent: suppress [true] or print [false] warning messages
 */
void nilfs_clear_dirty_pages(struct address_space *mapping, bool silent)
{
	struct pagevec pvec;
	unsigned int i;
	pgoff_t index = 0;

	pagevec_init(&pvec);

	while (pagevec_lookup_tag(&pvec, mapping, &index,
					PAGECACHE_TAG_DIRTY)) {
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];

			lock_page(page);
			nilfs_clear_dirty_page(page, silent);
			unlock_page(page);
		}
		pagevec_release(&pvec);
		cond_resched();
	}
}

/**
 * nilfs_clear_dirty_page - discard dirty page
 * @page: dirty page that will be discarded
 * @silent: suppress [true] or print [false] warning messages
 */
void nilfs_clear_dirty_page(struct page *page, bool silent)
{
	struct inode *inode = page->mapping->host;
	struct super_block *sb = inode->i_sb;

	BUG_ON(!PageLocked(page));

	if (!silent)
		nilfs_msg(sb, KERN_WARNING,
			  "discard dirty page: offset=%lld, ino=%lu",
			  page_offset(page), inode->i_ino);

	ClearPageUptodate(page);
	ClearPageMappedToDisk(page);

	if (page_has_buffers(page)) {
		struct buffer_head *bh, *head;
		const unsigned long clear_bits =
			(BIT(BH_Uptodate) | BIT(BH_Dirty) | BIT(BH_Mapped) |
			 BIT(BH_Async_Write) | BIT(BH_NILFS_Volatile) |
			 BIT(BH_NILFS_Checked) | BIT(BH_NILFS_Redirected));

		bh = head = page_buffers(page);
		do {
			lock_buffer(bh);
			if (!silent)
				nilfs_msg(sb, KERN_WARNING,
					  "discard dirty block: blocknr=%llu, size=%zu",
					  (u64)bh->b_blocknr, bh->b_size);

			set_mask_bits(&bh->b_state, clear_bits, 0);
			unlock_buffer(bh);
		} while (bh = bh->b_this_page, bh != head);
	}

	__nilfs_clear_page_dirty(page);
}

unsigned int nilfs_page_count_clean_buffers(struct page *page,
					    unsigned int from, unsigned int to)
{
	unsigned int block_start, block_end;
	struct buffer_head *bh, *head;
	unsigned int nc = 0;

	for (bh = head = page_buffers(page), block_start = 0;
	     bh != head || !block_start;
	     block_start = block_end, bh = bh->b_this_page) {
		block_end = block_start + bh->b_size;
		if (block_end > from && block_start < to && !buffer_dirty(bh))
			nc++;
	}
	return nc;
}

void nilfs_mapping_init(struct address_space *mapping, struct inode *inode)
{
	mapping->host = inode;
	mapping->flags = 0;
	mapping_set_gfp_mask(mapping, GFP_NOFS);
	mapping->private_data = NULL;
	mapping->a_ops = &empty_aops;
}

/*
 * NILFS2 needs clear_page_dirty() in the following two cases:
 *
 * 1) For B-tree node pages and data pages of the dat/gcdat, NILFS2 clears
 *    page dirty flags when it copies back pages from the shadow cache
 *    (gcdat->{i_mapping,i_btnode_cache}) to its original cache
 *    (dat->{i_mapping,i_btnode_cache}).
 *
 * 2) Some B-tree operations like insertion or deletion may dispose buffers
 *    in dirty state, and this needs to cancel the dirty state of their pages.
 */
int __nilfs_clear_page_dirty(struct page *page)
{
	struct address_space *mapping = page->mapping;

	if (mapping) {
		xa_lock_irq(&mapping->i_pages);
		if (test_bit(PG_dirty, &page->flags)) {
			radix_tree_tag_clear(&mapping->i_pages,
					     page_index(page),
					     PAGECACHE_TAG_DIRTY);
			xa_unlock_irq(&mapping->i_pages);
			return clear_page_dirty_for_io(page);
		}
		xa_unlock_irq(&mapping->i_pages);
		return 0;
	}
	return TestClearPageDirty(page);
}

/**
 * nilfs_find_uncommitted_extent - find extent of uncommitted data
 * @inode: inode
 * @start_blk: start block offset (in)
 * @blkoff: start offset of the found extent (out)
 *
 * This function searches an extent of buffers marked "delayed" which
 * starts from a block offset equal to or larger than @start_blk.  If
 * such an extent was found, this will store the start offset in
 * @blkoff and return its length in blocks.  Otherwise, zero is
 * returned.
 */
unsigned long nilfs_find_uncommitted_extent(struct inode *inode,
					    sector_t start_blk,
					    sector_t *blkoff)
{
	unsigned int i;
	pgoff_t index;
	unsigned int nblocks_in_page;
	unsigned long length = 0;
	sector_t b;
	struct pagevec pvec;
	struct page *page;

	if (inode->i_mapping->nrpages == 0)
		return 0;

	index = start_blk >> (PAGE_SHIFT - inode->i_blkbits);
	nblocks_in_page = 1U << (PAGE_SHIFT - inode->i_blkbits);

	pagevec_init(&pvec);

repeat:
	pvec.nr = find_get_pages_contig(inode->i_mapping, index, PAGEVEC_SIZE,
					pvec.pages);
	if (pvec.nr == 0)
		return length;

	if (length > 0 && pvec.pages[0]->index > index)
		goto out;

	b = pvec.pages[0]->index << (PAGE_SHIFT - inode->i_blkbits);
	i = 0;
	do {
		page = pvec.pages[i];

		lock_page(page);
		if (page_has_buffers(page)) {
			struct buffer_head *bh, *head;

			bh = head = page_buffers(page);
			do {
				if (b < start_blk)
					continue;
				if (buffer_delay(bh)) {
					if (length == 0)
						*blkoff = b;
					length++;
				} else if (length > 0) {
					goto out_locked;
				}
			} while (++b, bh = bh->b_this_page, bh != head);
		} else {
			if (length > 0)
				goto out_locked;

			b += nblocks_in_page;
		}
		unlock_page(page);

	} while (++i < pagevec_count(&pvec));

	index = page->index + 1;
	pagevec_release(&pvec);
	cond_resched();
	goto repeat;

out_locked:
	unlock_page(page);
out:
	pagevec_release(&pvec);
	return length;
}
