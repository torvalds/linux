// SPDX-License-Identifier: GPL-2.0+
/*
 * Buffer/page management specific to NILFS
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
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

static struct buffer_head *__nilfs_get_folio_block(struct folio *folio,
		unsigned long block, pgoff_t index, int blkbits,
		unsigned long b_state)

{
	unsigned long first_block;
	struct buffer_head *bh = folio_buffers(folio);

	if (!bh)
		bh = create_empty_buffers(folio, 1 << blkbits, b_state);

	first_block = (unsigned long)index << (PAGE_SHIFT - blkbits);
	bh = get_nth_bh(bh, block - first_block);

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
	struct folio *folio;
	struct buffer_head *bh;

	folio = filemap_grab_folio(mapping, index);
	if (IS_ERR(folio))
		return NULL;

	bh = __nilfs_get_folio_block(folio, blkoff, index, blkbits, b_state);
	if (unlikely(!bh)) {
		folio_unlock(folio);
		folio_put(folio);
		return NULL;
	}
	bh->b_bdev = inode->i_sb->s_bdev;
	return bh;
}

/**
 * nilfs_forget_buffer - discard dirty state
 * @bh: buffer head of the buffer to be discarded
 */
void nilfs_forget_buffer(struct buffer_head *bh)
{
	struct folio *folio = bh->b_folio;
	const unsigned long clear_bits =
		(BIT(BH_Uptodate) | BIT(BH_Dirty) | BIT(BH_Mapped) |
		 BIT(BH_Async_Write) | BIT(BH_NILFS_Volatile) |
		 BIT(BH_NILFS_Checked) | BIT(BH_NILFS_Redirected) |
		 BIT(BH_Delay));

	lock_buffer(bh);
	set_mask_bits(&bh->b_state, clear_bits, 0);
	if (nilfs_folio_buffers_clean(folio))
		__nilfs_clear_folio_dirty(folio);

	bh->b_blocknr = -1;
	folio_clear_uptodate(folio);
	folio_clear_mappedtodisk(folio);
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
	void *saddr, *daddr;
	unsigned long bits;
	struct folio *sfolio = sbh->b_folio, *dfolio = dbh->b_folio;
	struct buffer_head *bh;

	saddr = kmap_local_folio(sfolio, bh_offset(sbh));
	daddr = kmap_local_folio(dfolio, bh_offset(dbh));
	memcpy(daddr, saddr, sbh->b_size);
	kunmap_local(daddr);
	kunmap_local(saddr);

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
		folio_mark_uptodate(dfolio);
	else
		folio_clear_uptodate(dfolio);
	if (bits & BIT(BH_Mapped))
		folio_set_mappedtodisk(dfolio);
	else
		folio_clear_mappedtodisk(dfolio);
}

/**
 * nilfs_folio_buffers_clean - Check if a folio has dirty buffers or not.
 * @folio: Folio to be checked.
 *
 * nilfs_folio_buffers_clean() returns false if the folio has dirty buffers.
 * Otherwise, it returns true.
 */
bool nilfs_folio_buffers_clean(struct folio *folio)
{
	struct buffer_head *bh, *head;

	bh = head = folio_buffers(folio);
	do {
		if (buffer_dirty(bh))
			return false;
		bh = bh->b_this_page;
	} while (bh != head);
	return true;
}

void nilfs_folio_bug(struct folio *folio)
{
	struct buffer_head *bh, *head;
	struct address_space *m;
	unsigned long ino;

	if (unlikely(!folio)) {
		printk(KERN_CRIT "NILFS_FOLIO_BUG(NULL)\n");
		return;
	}

	m = folio->mapping;
	ino = m ? m->host->i_ino : 0;

	printk(KERN_CRIT "NILFS_FOLIO_BUG(%p): cnt=%d index#=%llu flags=0x%lx "
	       "mapping=%p ino=%lu\n",
	       folio, folio_ref_count(folio),
	       (unsigned long long)folio->index, folio->flags, m, ino);

	head = folio_buffers(folio);
	if (head) {
		int i = 0;

		bh = head;
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
 * nilfs_copy_folio -- copy the folio with buffers
 * @dst: destination folio
 * @src: source folio
 * @copy_dirty: flag whether to copy dirty states on the folio's buffer heads.
 *
 * This function is for both data folios and btnode folios.  The dirty flag
 * should be treated by caller.  The folio must not be under i/o.
 * Both src and dst folio must be locked
 */
static void nilfs_copy_folio(struct folio *dst, struct folio *src,
		bool copy_dirty)
{
	struct buffer_head *dbh, *dbufs, *sbh;
	unsigned long mask = NILFS_BUFFER_INHERENT_BITS;

	BUG_ON(folio_test_writeback(dst));

	sbh = folio_buffers(src);
	dbh = folio_buffers(dst);
	if (!dbh)
		dbh = create_empty_buffers(dst, sbh->b_size, 0);

	if (copy_dirty)
		mask |= BIT(BH_Dirty);

	dbufs = dbh;
	do {
		lock_buffer(sbh);
		lock_buffer(dbh);
		dbh->b_state = sbh->b_state & mask;
		dbh->b_blocknr = sbh->b_blocknr;
		dbh->b_bdev = sbh->b_bdev;
		sbh = sbh->b_this_page;
		dbh = dbh->b_this_page;
	} while (dbh != dbufs);

	folio_copy(dst, src);

	if (folio_test_uptodate(src) && !folio_test_uptodate(dst))
		folio_mark_uptodate(dst);
	else if (!folio_test_uptodate(src) && folio_test_uptodate(dst))
		folio_clear_uptodate(dst);
	if (folio_test_mappedtodisk(src) && !folio_test_mappedtodisk(dst))
		folio_set_mappedtodisk(dst);
	else if (!folio_test_mappedtodisk(src) && folio_test_mappedtodisk(dst))
		folio_clear_mappedtodisk(dst);

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
	struct folio_batch fbatch;
	unsigned int i;
	pgoff_t index = 0;
	int err = 0;

	folio_batch_init(&fbatch);
repeat:
	if (!filemap_get_folios_tag(smap, &index, (pgoff_t)-1,
				PAGECACHE_TAG_DIRTY, &fbatch))
		return 0;

	for (i = 0; i < folio_batch_count(&fbatch); i++) {
		struct folio *folio = fbatch.folios[i], *dfolio;

		folio_lock(folio);
		if (unlikely(!folio_test_dirty(folio)))
			NILFS_FOLIO_BUG(folio, "inconsistent dirty state");

		dfolio = filemap_grab_folio(dmap, folio->index);
		if (IS_ERR(dfolio)) {
			/* No empty page is added to the page cache */
			folio_unlock(folio);
			err = PTR_ERR(dfolio);
			break;
		}
		if (unlikely(!folio_buffers(folio)))
			NILFS_FOLIO_BUG(folio,
				       "found empty page in dat page cache");

		nilfs_copy_folio(dfolio, folio, true);
		filemap_dirty_folio(folio_mapping(dfolio), dfolio);

		folio_unlock(dfolio);
		folio_put(dfolio);
		folio_unlock(folio);
	}
	folio_batch_release(&fbatch);
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
 * No pages must be added to the cache during this process.
 * This must be ensured by the caller.
 */
void nilfs_copy_back_pages(struct address_space *dmap,
			   struct address_space *smap)
{
	struct folio_batch fbatch;
	unsigned int i, n;
	pgoff_t start = 0;

	folio_batch_init(&fbatch);
repeat:
	n = filemap_get_folios(smap, &start, ~0UL, &fbatch);
	if (!n)
		return;

	for (i = 0; i < folio_batch_count(&fbatch); i++) {
		struct folio *folio = fbatch.folios[i], *dfolio;
		pgoff_t index = folio->index;

		folio_lock(folio);
		dfolio = filemap_lock_folio(dmap, index);
		if (!IS_ERR(dfolio)) {
			/* overwrite existing folio in the destination cache */
			WARN_ON(folio_test_dirty(dfolio));
			nilfs_copy_folio(dfolio, folio, false);
			folio_unlock(dfolio);
			folio_put(dfolio);
			/* Do we not need to remove folio from smap here? */
		} else {
			struct folio *f;

			/* move the folio to the destination cache */
			xa_lock_irq(&smap->i_pages);
			f = __xa_erase(&smap->i_pages, index);
			WARN_ON(folio != f);
			smap->nrpages--;
			xa_unlock_irq(&smap->i_pages);

			xa_lock_irq(&dmap->i_pages);
			f = __xa_store(&dmap->i_pages, index, folio, GFP_NOFS);
			if (unlikely(f)) {
				/* Probably -ENOMEM */
				folio->mapping = NULL;
				folio_put(folio);
			} else {
				folio->mapping = dmap;
				dmap->nrpages++;
				if (folio_test_dirty(folio))
					__xa_set_mark(&dmap->i_pages, index,
							PAGECACHE_TAG_DIRTY);
			}
			xa_unlock_irq(&dmap->i_pages);
		}
		folio_unlock(folio);
	}
	folio_batch_release(&fbatch);
	cond_resched();

	goto repeat;
}

/**
 * nilfs_clear_dirty_pages - discard dirty pages in address space
 * @mapping: address space with dirty pages for discarding
 */
void nilfs_clear_dirty_pages(struct address_space *mapping)
{
	struct folio_batch fbatch;
	unsigned int i;
	pgoff_t index = 0;

	folio_batch_init(&fbatch);

	while (filemap_get_folios_tag(mapping, &index, (pgoff_t)-1,
				PAGECACHE_TAG_DIRTY, &fbatch)) {
		for (i = 0; i < folio_batch_count(&fbatch); i++) {
			struct folio *folio = fbatch.folios[i];

			folio_lock(folio);

			/*
			 * This folio may have been removed from the address
			 * space by truncation or invalidation when the lock
			 * was acquired.  Skip processing in that case.
			 */
			if (likely(folio->mapping == mapping))
				nilfs_clear_folio_dirty(folio);

			folio_unlock(folio);
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}
}

/**
 * nilfs_clear_folio_dirty - discard dirty folio
 * @folio: dirty folio that will be discarded
 */
void nilfs_clear_folio_dirty(struct folio *folio)
{
	struct buffer_head *bh, *head;

	BUG_ON(!folio_test_locked(folio));

	folio_clear_uptodate(folio);
	folio_clear_mappedtodisk(folio);
	folio_clear_checked(folio);

	head = folio_buffers(folio);
	if (head) {
		const unsigned long clear_bits =
			(BIT(BH_Uptodate) | BIT(BH_Dirty) | BIT(BH_Mapped) |
			 BIT(BH_Async_Write) | BIT(BH_NILFS_Volatile) |
			 BIT(BH_NILFS_Checked) | BIT(BH_NILFS_Redirected) |
			 BIT(BH_Delay));

		bh = head;
		do {
			lock_buffer(bh);
			set_mask_bits(&bh->b_state, clear_bits, 0);
			unlock_buffer(bh);
		} while (bh = bh->b_this_page, bh != head);
	}

	__nilfs_clear_folio_dirty(folio);
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

/*
 * NILFS2 needs clear_page_dirty() in the following two cases:
 *
 * 1) For B-tree node pages and data pages of DAT file, NILFS2 clears dirty
 *    flag of pages when it copies back pages from shadow cache to the
 *    original cache.
 *
 * 2) Some B-tree operations like insertion or deletion may dispose buffers
 *    in dirty state, and this needs to cancel the dirty state of their pages.
 */
void __nilfs_clear_folio_dirty(struct folio *folio)
{
	struct address_space *mapping = folio->mapping;

	if (mapping) {
		xa_lock_irq(&mapping->i_pages);
		if (folio_test_dirty(folio)) {
			__xa_clear_mark(&mapping->i_pages, folio->index,
					     PAGECACHE_TAG_DIRTY);
			xa_unlock_irq(&mapping->i_pages);
			folio_clear_dirty_for_io(folio);
			return;
		}
		xa_unlock_irq(&mapping->i_pages);
		return;
	}
	folio_clear_dirty(folio);
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
	unsigned int i, nr_folios;
	pgoff_t index;
	unsigned long length = 0;
	struct folio_batch fbatch;
	struct folio *folio;

	if (inode->i_mapping->nrpages == 0)
		return 0;

	index = start_blk >> (PAGE_SHIFT - inode->i_blkbits);

	folio_batch_init(&fbatch);

repeat:
	nr_folios = filemap_get_folios_contig(inode->i_mapping, &index, ULONG_MAX,
			&fbatch);
	if (nr_folios == 0)
		return length;

	i = 0;
	do {
		folio = fbatch.folios[i];

		folio_lock(folio);
		if (folio_buffers(folio)) {
			struct buffer_head *bh, *head;
			sector_t b;

			b = folio->index << (PAGE_SHIFT - inode->i_blkbits);
			bh = head = folio_buffers(folio);
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
		}
		folio_unlock(folio);

	} while (++i < nr_folios);

	folio_batch_release(&fbatch);
	cond_resched();
	goto repeat;

out_locked:
	folio_unlock(folio);
	folio_batch_release(&fbatch);
	return length;
}
