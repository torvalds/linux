// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) International Business Machines Corp., 2000-2005
 *   Portions Copyright (C) Christoph Hellwig, 2001-2002
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/buffer_head.h>
#include <linux/mempool.h>
#include <linux/seq_file.h>
#include <linux/writeback.h>
#include "jfs_incore.h"
#include "jfs_superblock.h"
#include "jfs_filsys.h"
#include "jfs_metapage.h"
#include "jfs_txnmgr.h"
#include "jfs_debug.h"

#ifdef CONFIG_JFS_STATISTICS
static struct {
	uint	pagealloc;	/* # of page allocations */
	uint	pagefree;	/* # of page frees */
	uint	lockwait;	/* # of sleeping lock_metapage() calls */
} mpStat;
#endif

#define metapage_locked(mp) test_bit(META_locked, &(mp)->flag)
#define trylock_metapage(mp) test_and_set_bit_lock(META_locked, &(mp)->flag)

static inline void unlock_metapage(struct metapage *mp)
{
	clear_bit_unlock(META_locked, &mp->flag);
	wake_up(&mp->wait);
}

static inline void __lock_metapage(struct metapage *mp)
{
	DECLARE_WAITQUEUE(wait, current);
	INCREMENT(mpStat.lockwait);
	add_wait_queue_exclusive(&mp->wait, &wait);
	do {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (metapage_locked(mp)) {
			unlock_page(mp->page);
			io_schedule();
			lock_page(mp->page);
		}
	} while (trylock_metapage(mp));
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&mp->wait, &wait);
}

/*
 * Must have mp->page locked
 */
static inline void lock_metapage(struct metapage *mp)
{
	if (trylock_metapage(mp))
		__lock_metapage(mp);
}

#define METAPOOL_MIN_PAGES 32
static struct kmem_cache *metapage_cache;
static mempool_t *metapage_mempool;

#define MPS_PER_PAGE (PAGE_SIZE >> L2PSIZE)

#if MPS_PER_PAGE > 1

struct meta_anchor {
	int mp_count;
	atomic_t io_count;
	struct metapage *mp[MPS_PER_PAGE];
};
#define mp_anchor(page) ((struct meta_anchor *)page_private(page))

static inline struct metapage *page_to_mp(struct page *page, int offset)
{
	if (!PagePrivate(page))
		return NULL;
	return mp_anchor(page)->mp[offset >> L2PSIZE];
}

static inline int insert_metapage(struct page *page, struct metapage *mp)
{
	struct meta_anchor *a;
	int index;
	int l2mp_blocks;	/* log2 blocks per metapage */

	if (PagePrivate(page))
		a = mp_anchor(page);
	else {
		a = kzalloc(sizeof(struct meta_anchor), GFP_NOFS);
		if (!a)
			return -ENOMEM;
		set_page_private(page, (unsigned long)a);
		SetPagePrivate(page);
		kmap(page);
	}

	if (mp) {
		l2mp_blocks = L2PSIZE - page->mapping->host->i_blkbits;
		index = (mp->index >> l2mp_blocks) & (MPS_PER_PAGE - 1);
		a->mp_count++;
		a->mp[index] = mp;
	}

	return 0;
}

static inline void remove_metapage(struct page *page, struct metapage *mp)
{
	struct meta_anchor *a = mp_anchor(page);
	int l2mp_blocks = L2PSIZE - page->mapping->host->i_blkbits;
	int index;

	index = (mp->index >> l2mp_blocks) & (MPS_PER_PAGE - 1);

	BUG_ON(a->mp[index] != mp);

	a->mp[index] = NULL;
	if (--a->mp_count == 0) {
		kfree(a);
		set_page_private(page, 0);
		ClearPagePrivate(page);
		kunmap(page);
	}
}

static inline void inc_io(struct page *page)
{
	atomic_inc(&mp_anchor(page)->io_count);
}

static inline void dec_io(struct page *page, void (*handler) (struct page *))
{
	if (atomic_dec_and_test(&mp_anchor(page)->io_count))
		handler(page);
}

#else
static inline struct metapage *page_to_mp(struct page *page, int offset)
{
	return PagePrivate(page) ? (struct metapage *)page_private(page) : NULL;
}

static inline int insert_metapage(struct page *page, struct metapage *mp)
{
	if (mp) {
		set_page_private(page, (unsigned long)mp);
		SetPagePrivate(page);
		kmap(page);
	}
	return 0;
}

static inline void remove_metapage(struct page *page, struct metapage *mp)
{
	set_page_private(page, 0);
	ClearPagePrivate(page);
	kunmap(page);
}

#define inc_io(page) do {} while(0)
#define dec_io(page, handler) handler(page)

#endif

static inline struct metapage *alloc_metapage(gfp_t gfp_mask)
{
	struct metapage *mp = mempool_alloc(metapage_mempool, gfp_mask);

	if (mp) {
		mp->lid = 0;
		mp->lsn = 0;
		mp->data = NULL;
		mp->clsn = 0;
		mp->log = NULL;
		init_waitqueue_head(&mp->wait);
	}
	return mp;
}

static inline void free_metapage(struct metapage *mp)
{
	mempool_free(mp, metapage_mempool);
}

int __init metapage_init(void)
{
	/*
	 * Allocate the metapage structures
	 */
	metapage_cache = kmem_cache_create("jfs_mp", sizeof(struct metapage),
					   0, 0, NULL);
	if (metapage_cache == NULL)
		return -ENOMEM;

	metapage_mempool = mempool_create_slab_pool(METAPOOL_MIN_PAGES,
						    metapage_cache);

	if (metapage_mempool == NULL) {
		kmem_cache_destroy(metapage_cache);
		return -ENOMEM;
	}

	return 0;
}

void metapage_exit(void)
{
	mempool_destroy(metapage_mempool);
	kmem_cache_destroy(metapage_cache);
}

static inline void drop_metapage(struct page *page, struct metapage *mp)
{
	if (mp->count || mp->nohomeok || test_bit(META_dirty, &mp->flag) ||
	    test_bit(META_io, &mp->flag))
		return;
	remove_metapage(page, mp);
	INCREMENT(mpStat.pagefree);
	free_metapage(mp);
}

/*
 * Metapage address space operations
 */

static sector_t metapage_get_blocks(struct inode *inode, sector_t lblock,
				    int *len)
{
	int rc = 0;
	int xflag;
	s64 xaddr;
	sector_t file_blocks = (inode->i_size + inode->i_sb->s_blocksize - 1) >>
			       inode->i_blkbits;

	if (lblock >= file_blocks)
		return 0;
	if (lblock + *len > file_blocks)
		*len = file_blocks - lblock;

	if (inode->i_ino) {
		rc = xtLookup(inode, (s64)lblock, *len, &xflag, &xaddr, len, 0);
		if ((rc == 0) && *len)
			lblock = (sector_t)xaddr;
		else
			lblock = 0;
	} /* else no mapping */

	return lblock;
}

static void last_read_complete(struct page *page)
{
	if (!PageError(page))
		SetPageUptodate(page);
	unlock_page(page);
}

static void metapage_read_end_io(struct bio *bio)
{
	struct page *page = bio->bi_private;

	if (bio->bi_status) {
		printk(KERN_ERR "metapage_read_end_io: I/O error\n");
		SetPageError(page);
	}

	dec_io(page, last_read_complete);
	bio_put(bio);
}

static void remove_from_logsync(struct metapage *mp)
{
	struct jfs_log *log = mp->log;
	unsigned long flags;
/*
 * This can race.  Recheck that log hasn't been set to null, and after
 * acquiring logsync lock, recheck lsn
 */
	if (!log)
		return;

	LOGSYNC_LOCK(log, flags);
	if (mp->lsn) {
		mp->log = NULL;
		mp->lsn = 0;
		mp->clsn = 0;
		log->count--;
		list_del(&mp->synclist);
	}
	LOGSYNC_UNLOCK(log, flags);
}

static void last_write_complete(struct page *page)
{
	struct metapage *mp;
	unsigned int offset;

	for (offset = 0; offset < PAGE_SIZE; offset += PSIZE) {
		mp = page_to_mp(page, offset);
		if (mp && test_bit(META_io, &mp->flag)) {
			if (mp->lsn)
				remove_from_logsync(mp);
			clear_bit(META_io, &mp->flag);
		}
		/*
		 * I'd like to call drop_metapage here, but I don't think it's
		 * safe unless I have the page locked
		 */
	}
	end_page_writeback(page);
}

static void metapage_write_end_io(struct bio *bio)
{
	struct page *page = bio->bi_private;

	BUG_ON(!PagePrivate(page));

	if (bio->bi_status) {
		printk(KERN_ERR "metapage_write_end_io: I/O error\n");
		SetPageError(page);
	}
	dec_io(page, last_write_complete);
	bio_put(bio);
}

static int metapage_writepage(struct page *page, struct writeback_control *wbc)
{
	struct bio *bio = NULL;
	int block_offset;	/* block offset of mp within page */
	struct inode *inode = page->mapping->host;
	int blocks_per_mp = JFS_SBI(inode->i_sb)->nbperpage;
	int len;
	int xlen;
	struct metapage *mp;
	int redirty = 0;
	sector_t lblock;
	int nr_underway = 0;
	sector_t pblock;
	sector_t next_block = 0;
	sector_t page_start;
	unsigned long bio_bytes = 0;
	unsigned long bio_offset = 0;
	int offset;
	int bad_blocks = 0;

	page_start = (sector_t)page->index <<
		     (PAGE_SHIFT - inode->i_blkbits);
	BUG_ON(!PageLocked(page));
	BUG_ON(PageWriteback(page));
	set_page_writeback(page);

	for (offset = 0; offset < PAGE_SIZE; offset += PSIZE) {
		mp = page_to_mp(page, offset);

		if (!mp || !test_bit(META_dirty, &mp->flag))
			continue;

		if (mp->nohomeok && !test_bit(META_forcewrite, &mp->flag)) {
			redirty = 1;
			/*
			 * Make sure this page isn't blocked indefinitely.
			 * If the journal isn't undergoing I/O, push it
			 */
			if (mp->log && !(mp->log->cflag & logGC_PAGEOUT))
				jfs_flush_journal(mp->log, 0);
			continue;
		}

		clear_bit(META_dirty, &mp->flag);
		set_bit(META_io, &mp->flag);
		block_offset = offset >> inode->i_blkbits;
		lblock = page_start + block_offset;
		if (bio) {
			if (xlen && lblock == next_block) {
				/* Contiguous, in memory & on disk */
				len = min(xlen, blocks_per_mp);
				xlen -= len;
				bio_bytes += len << inode->i_blkbits;
				continue;
			}
			/* Not contiguous */
			if (bio_add_page(bio, page, bio_bytes, bio_offset) <
			    bio_bytes)
				goto add_failed;
			/*
			 * Increment counter before submitting i/o to keep
			 * count from hitting zero before we're through
			 */
			inc_io(page);
			if (!bio->bi_iter.bi_size)
				goto dump_bio;
			submit_bio(bio);
			nr_underway++;
			bio = NULL;
		} else
			inc_io(page);
		xlen = (PAGE_SIZE - offset) >> inode->i_blkbits;
		pblock = metapage_get_blocks(inode, lblock, &xlen);
		if (!pblock) {
			printk(KERN_ERR "JFS: metapage_get_blocks failed\n");
			/*
			 * We already called inc_io(), but can't cancel it
			 * with dec_io() until we're done with the page
			 */
			bad_blocks++;
			continue;
		}
		len = min(xlen, (int)JFS_SBI(inode->i_sb)->nbperpage);

		bio = bio_alloc(inode->i_sb->s_bdev, 1, REQ_OP_WRITE, GFP_NOFS);
		bio->bi_iter.bi_sector = pblock << (inode->i_blkbits - 9);
		bio->bi_end_io = metapage_write_end_io;
		bio->bi_private = page;

		/* Don't call bio_add_page yet, we may add to this vec */
		bio_offset = offset;
		bio_bytes = len << inode->i_blkbits;

		xlen -= len;
		next_block = lblock + len;
	}
	if (bio) {
		if (bio_add_page(bio, page, bio_bytes, bio_offset) < bio_bytes)
				goto add_failed;
		if (!bio->bi_iter.bi_size)
			goto dump_bio;

		submit_bio(bio);
		nr_underway++;
	}
	if (redirty)
		redirty_page_for_writepage(wbc, page);

	unlock_page(page);

	if (bad_blocks)
		goto err_out;

	if (nr_underway == 0)
		end_page_writeback(page);

	return 0;
add_failed:
	/* We should never reach here, since we're only adding one vec */
	printk(KERN_ERR "JFS: bio_add_page failed unexpectedly\n");
	goto skip;
dump_bio:
	print_hex_dump(KERN_ERR, "JFS: dump of bio: ", DUMP_PREFIX_ADDRESS, 16,
		       4, bio, sizeof(*bio), 0);
skip:
	bio_put(bio);
	unlock_page(page);
	dec_io(page, last_write_complete);
err_out:
	while (bad_blocks--)
		dec_io(page, last_write_complete);
	return -EIO;
}

static int metapage_readpage(struct file *fp, struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct bio *bio = NULL;
	int block_offset;
	int blocks_per_page = i_blocks_per_page(inode, page);
	sector_t page_start;	/* address of page in fs blocks */
	sector_t pblock;
	int xlen;
	unsigned int len;
	int offset;

	BUG_ON(!PageLocked(page));
	page_start = (sector_t)page->index <<
		     (PAGE_SHIFT - inode->i_blkbits);

	block_offset = 0;
	while (block_offset < blocks_per_page) {
		xlen = blocks_per_page - block_offset;
		pblock = metapage_get_blocks(inode, page_start + block_offset,
					     &xlen);
		if (pblock) {
			if (!PagePrivate(page))
				insert_metapage(page, NULL);
			inc_io(page);
			if (bio)
				submit_bio(bio);

			bio = bio_alloc(inode->i_sb->s_bdev, 1, REQ_OP_READ,
					GFP_NOFS);
			bio->bi_iter.bi_sector =
				pblock << (inode->i_blkbits - 9);
			bio->bi_end_io = metapage_read_end_io;
			bio->bi_private = page;
			len = xlen << inode->i_blkbits;
			offset = block_offset << inode->i_blkbits;
			if (bio_add_page(bio, page, len, offset) < len)
				goto add_failed;
			block_offset += xlen;
		} else
			block_offset++;
	}
	if (bio)
		submit_bio(bio);
	else
		unlock_page(page);

	return 0;

add_failed:
	printk(KERN_ERR "JFS: bio_add_page failed unexpectedly\n");
	bio_put(bio);
	dec_io(page, last_read_complete);
	return -EIO;
}

static int metapage_releasepage(struct page *page, gfp_t gfp_mask)
{
	struct metapage *mp;
	int ret = 1;
	int offset;

	for (offset = 0; offset < PAGE_SIZE; offset += PSIZE) {
		mp = page_to_mp(page, offset);

		if (!mp)
			continue;

		jfs_info("metapage_releasepage: mp = 0x%p", mp);
		if (mp->count || mp->nohomeok ||
		    test_bit(META_dirty, &mp->flag)) {
			jfs_info("count = %ld, nohomeok = %d", mp->count,
				 mp->nohomeok);
			ret = 0;
			continue;
		}
		if (mp->lsn)
			remove_from_logsync(mp);
		remove_metapage(page, mp);
		INCREMENT(mpStat.pagefree);
		free_metapage(mp);
	}
	return ret;
}

static void metapage_invalidate_folio(struct folio *folio, size_t offset,
				    size_t length)
{
	BUG_ON(offset || length < folio_size(folio));

	BUG_ON(folio_test_writeback(folio));

	metapage_releasepage(&folio->page, 0);
}

const struct address_space_operations jfs_metapage_aops = {
	.readpage	= metapage_readpage,
	.writepage	= metapage_writepage,
	.releasepage	= metapage_releasepage,
	.invalidate_folio = metapage_invalidate_folio,
	.dirty_folio	= filemap_dirty_folio,
};

struct metapage *__get_metapage(struct inode *inode, unsigned long lblock,
				unsigned int size, int absolute,
				unsigned long new)
{
	int l2BlocksPerPage;
	int l2bsize;
	struct address_space *mapping;
	struct metapage *mp = NULL;
	struct page *page;
	unsigned long page_index;
	unsigned long page_offset;

	jfs_info("__get_metapage: ino = %ld, lblock = 0x%lx, abs=%d",
		 inode->i_ino, lblock, absolute);

	l2bsize = inode->i_blkbits;
	l2BlocksPerPage = PAGE_SHIFT - l2bsize;
	page_index = lblock >> l2BlocksPerPage;
	page_offset = (lblock - (page_index << l2BlocksPerPage)) << l2bsize;
	if ((page_offset + size) > PAGE_SIZE) {
		jfs_err("MetaData crosses page boundary!!");
		jfs_err("lblock = %lx, size  = %d", lblock, size);
		dump_stack();
		return NULL;
	}
	if (absolute)
		mapping = JFS_SBI(inode->i_sb)->direct_inode->i_mapping;
	else {
		/*
		 * If an nfs client tries to read an inode that is larger
		 * than any existing inodes, we may try to read past the
		 * end of the inode map
		 */
		if ((lblock << inode->i_blkbits) >= inode->i_size)
			return NULL;
		mapping = inode->i_mapping;
	}

	if (new && (PSIZE == PAGE_SIZE)) {
		page = grab_cache_page(mapping, page_index);
		if (!page) {
			jfs_err("grab_cache_page failed!");
			return NULL;
		}
		SetPageUptodate(page);
	} else {
		page = read_mapping_page(mapping, page_index, NULL);
		if (IS_ERR(page) || !PageUptodate(page)) {
			jfs_err("read_mapping_page failed!");
			return NULL;
		}
		lock_page(page);
	}

	mp = page_to_mp(page, page_offset);
	if (mp) {
		if (mp->logical_size != size) {
			jfs_error(inode->i_sb,
				  "get_mp->logical_size != size\n");
			jfs_err("logical_size = %d, size = %d",
				mp->logical_size, size);
			dump_stack();
			goto unlock;
		}
		mp->count++;
		lock_metapage(mp);
		if (test_bit(META_discard, &mp->flag)) {
			if (!new) {
				jfs_error(inode->i_sb,
					  "using a discarded metapage\n");
				discard_metapage(mp);
				goto unlock;
			}
			clear_bit(META_discard, &mp->flag);
		}
	} else {
		INCREMENT(mpStat.pagealloc);
		mp = alloc_metapage(GFP_NOFS);
		if (!mp)
			goto unlock;
		mp->page = page;
		mp->sb = inode->i_sb;
		mp->flag = 0;
		mp->xflag = COMMIT_PAGE;
		mp->count = 1;
		mp->nohomeok = 0;
		mp->logical_size = size;
		mp->data = page_address(page) + page_offset;
		mp->index = lblock;
		if (unlikely(insert_metapage(page, mp))) {
			free_metapage(mp);
			goto unlock;
		}
		lock_metapage(mp);
	}

	if (new) {
		jfs_info("zeroing mp = 0x%p", mp);
		memset(mp->data, 0, PSIZE);
	}

	unlock_page(page);
	jfs_info("__get_metapage: returning = 0x%p data = 0x%p", mp, mp->data);
	return mp;

unlock:
	unlock_page(page);
	return NULL;
}

void grab_metapage(struct metapage * mp)
{
	jfs_info("grab_metapage: mp = 0x%p", mp);
	get_page(mp->page);
	lock_page(mp->page);
	mp->count++;
	lock_metapage(mp);
	unlock_page(mp->page);
}

void force_metapage(struct metapage *mp)
{
	struct page *page = mp->page;
	jfs_info("force_metapage: mp = 0x%p", mp);
	set_bit(META_forcewrite, &mp->flag);
	clear_bit(META_sync, &mp->flag);
	get_page(page);
	lock_page(page);
	set_page_dirty(page);
	if (write_one_page(page))
		jfs_error(mp->sb, "write_one_page() failed\n");
	clear_bit(META_forcewrite, &mp->flag);
	put_page(page);
}

void hold_metapage(struct metapage *mp)
{
	lock_page(mp->page);
}

void put_metapage(struct metapage *mp)
{
	if (mp->count || mp->nohomeok) {
		/* Someone else will release this */
		unlock_page(mp->page);
		return;
	}
	get_page(mp->page);
	mp->count++;
	lock_metapage(mp);
	unlock_page(mp->page);
	release_metapage(mp);
}

void release_metapage(struct metapage * mp)
{
	struct page *page = mp->page;
	jfs_info("release_metapage: mp = 0x%p, flag = 0x%lx", mp, mp->flag);

	BUG_ON(!page);

	lock_page(page);
	unlock_metapage(mp);

	assert(mp->count);
	if (--mp->count || mp->nohomeok) {
		unlock_page(page);
		put_page(page);
		return;
	}

	if (test_bit(META_dirty, &mp->flag)) {
		set_page_dirty(page);
		if (test_bit(META_sync, &mp->flag)) {
			clear_bit(META_sync, &mp->flag);
			if (write_one_page(page))
				jfs_error(mp->sb, "write_one_page() failed\n");
			lock_page(page); /* write_one_page unlocks the page */
		}
	} else if (mp->lsn)	/* discard_metapage doesn't remove it */
		remove_from_logsync(mp);

	/* Try to keep metapages from using up too much memory */
	drop_metapage(page, mp);

	unlock_page(page);
	put_page(page);
}

void __invalidate_metapages(struct inode *ip, s64 addr, int len)
{
	sector_t lblock;
	int l2BlocksPerPage = PAGE_SHIFT - ip->i_blkbits;
	int BlocksPerPage = 1 << l2BlocksPerPage;
	/* All callers are interested in block device's mapping */
	struct address_space *mapping =
		JFS_SBI(ip->i_sb)->direct_inode->i_mapping;
	struct metapage *mp;
	struct page *page;
	unsigned int offset;

	/*
	 * Mark metapages to discard.  They will eventually be
	 * released, but should not be written.
	 */
	for (lblock = addr & ~(BlocksPerPage - 1); lblock < addr + len;
	     lblock += BlocksPerPage) {
		page = find_lock_page(mapping, lblock >> l2BlocksPerPage);
		if (!page)
			continue;
		for (offset = 0; offset < PAGE_SIZE; offset += PSIZE) {
			mp = page_to_mp(page, offset);
			if (!mp)
				continue;
			if (mp->index < addr)
				continue;
			if (mp->index >= addr + len)
				break;

			clear_bit(META_dirty, &mp->flag);
			set_bit(META_discard, &mp->flag);
			if (mp->lsn)
				remove_from_logsync(mp);
		}
		unlock_page(page);
		put_page(page);
	}
}

#ifdef CONFIG_JFS_STATISTICS
int jfs_mpstat_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m,
		       "JFS Metapage statistics\n"
		       "=======================\n"
		       "page allocations = %d\n"
		       "page frees = %d\n"
		       "lock waits = %d\n",
		       mpStat.pagealloc,
		       mpStat.pagefree,
		       mpStat.lockwait);
	return 0;
}
#endif
