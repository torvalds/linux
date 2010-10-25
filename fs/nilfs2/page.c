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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Ryusuke Konishi <ryusuke@osrg.net>,
 *            Seiji Kihara <kihara@osrg.net>.
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


#define NILFS_BUFFER_INHERENT_BITS  \
	((1UL << BH_Uptodate) | (1UL << BH_Mapped) | (1UL << BH_NILFS_Node) | \
	 (1UL << BH_NILFS_Volatile) | (1UL << BH_NILFS_Allocated) | \
	 (1UL << BH_NILFS_Checked))

static struct buffer_head *
__nilfs_get_page_block(struct page *page, unsigned long block, pgoff_t index,
		       int blkbits, unsigned long b_state)

{
	unsigned long first_block;
	struct buffer_head *bh;

	if (!page_has_buffers(page))
		create_empty_buffers(page, 1 << blkbits, b_state);

	first_block = (unsigned long)index << (PAGE_CACHE_SHIFT - blkbits);
	bh = nilfs_page_get_nth_block(page, block - first_block);

	touch_buffer(bh);
	wait_on_buffer(bh);
	return bh;
}

/*
 * Since the page cache of B-tree node pages or data page cache of pseudo
 * inodes does not have a valid mapping->host pointer, calling
 * mark_buffer_dirty() for their buffers causes a NULL pointer dereference;
 * it calls __mark_inode_dirty(NULL) through __set_page_dirty().
 * To avoid this problem, the old style mark_buffer_dirty() is used instead.
 */
void nilfs_mark_buffer_dirty(struct buffer_head *bh)
{
	if (!buffer_dirty(bh) && !test_set_buffer_dirty(bh))
		__set_page_dirty_nobuffers(bh->b_page);
}

struct buffer_head *nilfs_grab_buffer(struct inode *inode,
				      struct address_space *mapping,
				      unsigned long blkoff,
				      unsigned long b_state)
{
	int blkbits = inode->i_blkbits;
	pgoff_t index = blkoff >> (PAGE_CACHE_SHIFT - blkbits);
	struct page *page;
	struct buffer_head *bh;

	page = grab_cache_page(mapping, index);
	if (unlikely(!page))
		return NULL;

	bh = __nilfs_get_page_block(page, blkoff, index, blkbits, b_state);
	if (unlikely(!bh)) {
		unlock_page(page);
		page_cache_release(page);
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

	lock_buffer(bh);
	clear_buffer_nilfs_volatile(bh);
	clear_buffer_nilfs_checked(bh);
	clear_buffer_nilfs_redirected(bh);
	clear_buffer_dirty(bh);
	if (nilfs_page_buffers_clean(page))
		__nilfs_clear_page_dirty(page);

	clear_buffer_uptodate(bh);
	clear_buffer_mapped(bh);
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

	kaddr0 = kmap_atomic(spage, KM_USER0);
	kaddr1 = kmap_atomic(dpage, KM_USER1);
	memcpy(kaddr1 + bh_offset(dbh), kaddr0 + bh_offset(sbh), sbh->b_size);
	kunmap_atomic(kaddr1, KM_USER1);
	kunmap_atomic(kaddr0, KM_USER0);

	dbh->b_state = sbh->b_state & NILFS_BUFFER_INHERENT_BITS;
	dbh->b_blocknr = sbh->b_blocknr;
	dbh->b_bdev = sbh->b_bdev;

	bh = dbh;
	bits = sbh->b_state & ((1UL << BH_Uptodate) | (1UL << BH_Mapped));
	while ((bh = bh->b_this_page) != dbh) {
		lock_buffer(bh);
		bits &= bh->b_state;
		unlock_buffer(bh);
	}
	if (bits & (1UL << BH_Uptodate))
		SetPageUptodate(dpage);
	else
		ClearPageUptodate(dpage);
	if (bits & (1UL << BH_Mapped))
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
	unsigned long ino = 0;

	if (unlikely(!page)) {
		printk(KERN_CRIT "NILFS_PAGE_BUG(NULL)\n");
		return;
	}

	m = page->mapping;
	if (m) {
		struct inode *inode = NILFS_AS_I(m);
		if (inode != NULL)
			ino = inode->i_ino;
	}
	printk(KERN_CRIT "NILFS_PAGE_BUG(%p): cnt=%d index#=%llu flags=0x%lx "
	       "mapping=%p ino=%lu\n",
	       page, atomic_read(&page->_count),
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
 * nilfs_alloc_private_page - allocate a private page with buffer heads
 *
 * Return Value: On success, a pointer to the allocated page is returned.
 * On error, NULL is returned.
 */
struct page *nilfs_alloc_private_page(struct block_device *bdev, int size,
				      unsigned long state)
{
	struct buffer_head *bh, *head, *tail;
	struct page *page;

	page = alloc_page(GFP_NOFS); /* page_count of the returned page is 1 */
	if (unlikely(!page))
		return NULL;

	lock_page(page);
	head = alloc_page_buffers(page, size, 0);
	if (unlikely(!head)) {
		unlock_page(page);
		__free_page(page);
		return NULL;
	}

	bh = head;
	do {
		bh->b_state = (1UL << BH_NILFS_Allocated) | state;
		tail = bh;
		bh->b_bdev = bdev;
		bh = bh->b_this_page;
	} while (bh);

	tail->b_this_page = head;
	attach_page_buffers(page, head);

	return page;
}

void nilfs_free_private_page(struct page *page)
{
	BUG_ON(!PageLocked(page));
	BUG_ON(page->mapping);

	if (page_has_buffers(page) && !try_to_free_buffers(page))
		NILFS_PAGE_BUG(page, "failed to free page");

	unlock_page(page);
	__free_page(page);
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
		mask |= (1UL << BH_Dirty);

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

	pagevec_init(&pvec, 0);
repeat:
	if (!pagevec_lookup_tag(&pvec, smap, &index, PAGECACHE_TAG_DIRTY,
				PAGEVEC_SIZE))
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
		page_cache_release(dpage);
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

	pagevec_init(&pvec, 0);
repeat:
	n = pagevec_lookup(&pvec, smap, index, PAGEVEC_SIZE);
	if (!n)
		return;
	index = pvec.pages[n - 1]->index + 1;

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
			page_cache_release(dpage);
		} else {
			struct page *page2;

			/* move the page to the destination cache */
			spin_lock_irq(&smap->tree_lock);
			page2 = radix_tree_delete(&smap->page_tree, offset);
			WARN_ON(page2 != page);

			smap->nrpages--;
			spin_unlock_irq(&smap->tree_lock);

			spin_lock_irq(&dmap->tree_lock);
			err = radix_tree_insert(&dmap->page_tree, offset, page);
			if (unlikely(err < 0)) {
				WARN_ON(err == -EEXIST);
				page->mapping = NULL;
				page_cache_release(page); /* for cache */
			} else {
				page->mapping = dmap;
				dmap->nrpages++;
				if (PageDirty(page))
					radix_tree_tag_set(&dmap->page_tree,
							   offset,
							   PAGECACHE_TAG_DIRTY);
			}
			spin_unlock_irq(&dmap->tree_lock);
		}
		unlock_page(page);
	}
	pagevec_release(&pvec);
	cond_resched();

	goto repeat;
}

void nilfs_clear_dirty_pages(struct address_space *mapping)
{
	struct pagevec pvec;
	unsigned int i;
	pgoff_t index = 0;

	pagevec_init(&pvec, 0);

	while (pagevec_lookup_tag(&pvec, mapping, &index, PAGECACHE_TAG_DIRTY,
				  PAGEVEC_SIZE)) {
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];
			struct buffer_head *bh, *head;

			lock_page(page);
			ClearPageUptodate(page);
			ClearPageMappedToDisk(page);
			bh = head = page_buffers(page);
			do {
				lock_buffer(bh);
				clear_buffer_dirty(bh);
				clear_buffer_nilfs_volatile(bh);
				clear_buffer_nilfs_checked(bh);
				clear_buffer_nilfs_redirected(bh);
				clear_buffer_uptodate(bh);
				clear_buffer_mapped(bh);
				unlock_buffer(bh);
				bh = bh->b_this_page;
			} while (bh != head);

			__nilfs_clear_page_dirty(page);
			unlock_page(page);
		}
		pagevec_release(&pvec);
		cond_resched();
	}
}

unsigned nilfs_page_count_clean_buffers(struct page *page,
					unsigned from, unsigned to)
{
	unsigned block_start, block_end;
	struct buffer_head *bh, *head;
	unsigned nc = 0;

	for (bh = head = page_buffers(page), block_start = 0;
	     bh != head || !block_start;
	     block_start = block_end, bh = bh->b_this_page) {
		block_end = block_start + bh->b_size;
		if (block_end > from && block_start < to && !buffer_dirty(bh))
			nc++;
	}
	return nc;
}
 
void nilfs_mapping_init_once(struct address_space *mapping)
{
	memset(mapping, 0, sizeof(*mapping));
	INIT_RADIX_TREE(&mapping->page_tree, GFP_ATOMIC);
	spin_lock_init(&mapping->tree_lock);
	INIT_LIST_HEAD(&mapping->private_list);
	spin_lock_init(&mapping->private_lock);

	spin_lock_init(&mapping->i_mmap_lock);
	INIT_RAW_PRIO_TREE_ROOT(&mapping->i_mmap);
	INIT_LIST_HEAD(&mapping->i_mmap_nonlinear);
}

void nilfs_mapping_init(struct address_space *mapping,
			struct backing_dev_info *bdi,
			const struct address_space_operations *aops)
{
	mapping->host = NULL;
	mapping->flags = 0;
	mapping_set_gfp_mask(mapping, GFP_NOFS);
	mapping->assoc_mapping = NULL;
	mapping->backing_dev_info = bdi;
	mapping->a_ops = aops;
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
		spin_lock_irq(&mapping->tree_lock);
		if (test_bit(PG_dirty, &page->flags)) {
			radix_tree_tag_clear(&mapping->page_tree,
					     page_index(page),
					     PAGECACHE_TAG_DIRTY);
			spin_unlock_irq(&mapping->tree_lock);
			return clear_page_dirty_for_io(page);
		}
		spin_unlock_irq(&mapping->tree_lock);
		return 0;
	}
	return TestClearPageDirty(page);
}
