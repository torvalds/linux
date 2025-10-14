// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) International Business Machines Corp., 2000-2005
 *   Portions Copyright (C) Christoph Hellwig, 2001-2002
 */

#include <linux/blkdev.h>
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
#include <linux/migrate.h>
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
			folio_unlock(mp->folio);
			io_schedule();
			folio_lock(mp->folio);
		}
	} while (trylock_metapage(mp));
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&mp->wait, &wait);
}

/*
 * Must have mp->folio locked
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
	blk_status_t status;
	struct metapage *mp[MPS_PER_PAGE];
};

static inline struct metapage *folio_to_mp(struct folio *folio, int offset)
{
	struct meta_anchor *anchor = folio->private;

	if (!anchor)
		return NULL;
	return anchor->mp[offset >> L2PSIZE];
}

static inline int insert_metapage(struct folio *folio, struct metapage *mp)
{
	struct meta_anchor *a;
	int index;
	int l2mp_blocks;	/* log2 blocks per metapage */

	a = folio->private;
	if (!a) {
		a = kzalloc(sizeof(struct meta_anchor), GFP_NOFS);
		if (!a)
			return -ENOMEM;
		folio_attach_private(folio, a);
		kmap(&folio->page);
	}

	if (mp) {
		l2mp_blocks = L2PSIZE - folio->mapping->host->i_blkbits;
		index = (mp->index >> l2mp_blocks) & (MPS_PER_PAGE - 1);
		a->mp_count++;
		a->mp[index] = mp;
	}

	return 0;
}

static inline void remove_metapage(struct folio *folio, struct metapage *mp)
{
	struct meta_anchor *a = folio->private;
	int l2mp_blocks = L2PSIZE - folio->mapping->host->i_blkbits;
	int index;

	index = (mp->index >> l2mp_blocks) & (MPS_PER_PAGE - 1);

	BUG_ON(a->mp[index] != mp);

	a->mp[index] = NULL;
	if (--a->mp_count == 0) {
		kfree(a);
		folio_detach_private(folio);
		kunmap(&folio->page);
	}
}

static inline void inc_io(struct folio *folio)
{
	struct meta_anchor *anchor = folio->private;

	atomic_inc(&anchor->io_count);
}

static inline void dec_io(struct folio *folio, blk_status_t status,
		void (*handler)(struct folio *, blk_status_t))
{
	struct meta_anchor *anchor = folio->private;

	if (anchor->status == BLK_STS_OK)
		anchor->status = status;

	if (atomic_dec_and_test(&anchor->io_count))
		handler(folio, anchor->status);
}

#ifdef CONFIG_MIGRATION
static int __metapage_migrate_folio(struct address_space *mapping,
				    struct folio *dst, struct folio *src,
				    enum migrate_mode mode)
{
	struct meta_anchor *src_anchor = src->private;
	struct metapage *mps[MPS_PER_PAGE] = {0};
	struct metapage *mp;
	int i, rc;

	for (i = 0; i < MPS_PER_PAGE; i++) {
		mp = src_anchor->mp[i];
		if (mp && metapage_locked(mp))
			return -EAGAIN;
	}

	rc = filemap_migrate_folio(mapping, dst, src, mode);
	if (rc)
		return rc;

	for (i = 0; i < MPS_PER_PAGE; i++) {
		mp = src_anchor->mp[i];
		if (!mp)
			continue;
		if (unlikely(insert_metapage(dst, mp))) {
			/* If error, roll-back previosly inserted pages */
			for (int j = 0 ; j < i; j++) {
				if (mps[j])
					remove_metapage(dst, mps[j]);
			}
			return -EAGAIN;
		}
		mps[i] = mp;
	}

	/* Update the metapage and remove it from src */
	for (i = 0; i < MPS_PER_PAGE; i++) {
		mp = mps[i];
		if (mp) {
			int page_offset = mp->data - folio_address(src);

			mp->data = folio_address(dst) + page_offset;
			mp->folio = dst;
			remove_metapage(src, mp);
		}
	}

	return 0;
}
#endif	/* CONFIG_MIGRATION */

#else

static inline struct metapage *folio_to_mp(struct folio *folio, int offset)
{
	return folio->private;
}

static inline int insert_metapage(struct folio *folio, struct metapage *mp)
{
	if (mp) {
		folio_attach_private(folio, mp);
		kmap(&folio->page);
	}
	return 0;
}

static inline void remove_metapage(struct folio *folio, struct metapage *mp)
{
	folio_detach_private(folio);
	kunmap(&folio->page);
}

#define inc_io(folio) do {} while(0)
#define dec_io(folio, status, handler) handler(folio, status)

#ifdef CONFIG_MIGRATION
static int __metapage_migrate_folio(struct address_space *mapping,
				    struct folio *dst, struct folio *src,
				    enum migrate_mode mode)
{
	struct metapage *mp;
	int page_offset;
	int rc;

	mp = folio_to_mp(src, 0);
	if (metapage_locked(mp))
		return -EAGAIN;

	rc = filemap_migrate_folio(mapping, dst, src, mode);
	if (rc)
		return rc;

	if (unlikely(insert_metapage(dst, mp)))
		return -EAGAIN;

	page_offset = mp->data - folio_address(src);
	mp->data = folio_address(dst) + page_offset;
	mp->folio = dst;
	remove_metapage(src, mp);

	return 0;
}
#endif	/* CONFIG_MIGRATION */

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

static inline void drop_metapage(struct folio *folio, struct metapage *mp)
{
	if (mp->count || mp->nohomeok || test_bit(META_dirty, &mp->flag) ||
	    test_bit(META_io, &mp->flag))
		return;
	remove_metapage(folio, mp);
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

static void last_read_complete(struct folio *folio, blk_status_t status)
{
	if (status)
		printk(KERN_ERR "Read error %d at %#llx\n", status,
				folio_pos(folio));

	folio_end_read(folio, status == 0);
}

static void metapage_read_end_io(struct bio *bio)
{
	struct folio *folio = bio->bi_private;

	dec_io(folio, bio->bi_status, last_read_complete);
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

static void last_write_complete(struct folio *folio, blk_status_t status)
{
	struct metapage *mp;
	unsigned int offset;

	if (status) {
		int err = blk_status_to_errno(status);
		printk(KERN_ERR "metapage_write_end_io: I/O error\n");
		mapping_set_error(folio->mapping, err);
	}

	for (offset = 0; offset < PAGE_SIZE; offset += PSIZE) {
		mp = folio_to_mp(folio, offset);
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
	folio_end_writeback(folio);
}

static void metapage_write_end_io(struct bio *bio)
{
	struct folio *folio = bio->bi_private;

	BUG_ON(!folio->private);

	dec_io(folio, bio->bi_status, last_write_complete);
	bio_put(bio);
}

static int metapage_write_folio(struct folio *folio,
		struct writeback_control *wbc)
{
	struct bio *bio = NULL;
	int block_offset;	/* block offset of mp within page */
	struct inode *inode = folio->mapping->host;
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

	page_start = folio_pos(folio) >> inode->i_blkbits;
	BUG_ON(!folio_test_locked(folio));
	BUG_ON(folio_test_writeback(folio));
	folio_start_writeback(folio);

	for (offset = 0; offset < PAGE_SIZE; offset += PSIZE) {
		mp = folio_to_mp(folio, offset);

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
			bio_add_folio_nofail(bio, folio, bio_bytes, bio_offset);
			/*
			 * Increment counter before submitting i/o to keep
			 * count from hitting zero before we're through
			 */
			inc_io(folio);
			if (!bio->bi_iter.bi_size)
				goto dump_bio;
			submit_bio(bio);
			nr_underway++;
			bio = NULL;
		} else
			inc_io(folio);
		xlen = (folio_size(folio) - offset) >> inode->i_blkbits;
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
		bio->bi_private = folio;

		/* Don't call bio_add_page yet, we may add to this vec */
		bio_offset = offset;
		bio_bytes = len << inode->i_blkbits;

		xlen -= len;
		next_block = lblock + len;
	}
	if (bio) {
		bio_add_folio_nofail(bio, folio, bio_bytes, bio_offset);
		if (!bio->bi_iter.bi_size)
			goto dump_bio;

		submit_bio(bio);
		nr_underway++;
	}
	if (redirty)
		folio_redirty_for_writepage(wbc, folio);

	folio_unlock(folio);

	if (bad_blocks)
		goto err_out;

	if (nr_underway == 0)
		folio_end_writeback(folio);

	return 0;
dump_bio:
	print_hex_dump(KERN_ERR, "JFS: dump of bio: ", DUMP_PREFIX_ADDRESS, 16,
		       4, bio, sizeof(*bio), 0);
	bio_put(bio);
	folio_unlock(folio);
	dec_io(folio, BLK_STS_OK, last_write_complete);
err_out:
	while (bad_blocks--)
		dec_io(folio, BLK_STS_OK, last_write_complete);
	return -EIO;
}

static int metapage_writepages(struct address_space *mapping,
		struct writeback_control *wbc)
{
	struct blk_plug plug;
	struct folio *folio = NULL;
	int err;

	blk_start_plug(&plug);
	while ((folio = writeback_iter(mapping, wbc, folio, &err)))
		err = metapage_write_folio(folio, wbc);
	blk_finish_plug(&plug);

	return err;
}

static int metapage_read_folio(struct file *fp, struct folio *folio)
{
	struct inode *inode = folio->mapping->host;
	struct bio *bio = NULL;
	int block_offset;
	int blocks_per_page = i_blocks_per_folio(inode, folio);
	sector_t page_start;	/* address of page in fs blocks */
	sector_t pblock;
	int xlen;
	unsigned int len;
	int offset;

	BUG_ON(!folio_test_locked(folio));
	page_start = folio_pos(folio) >> inode->i_blkbits;

	block_offset = 0;
	while (block_offset < blocks_per_page) {
		xlen = blocks_per_page - block_offset;
		pblock = metapage_get_blocks(inode, page_start + block_offset,
					     &xlen);
		if (pblock) {
			if (!folio->private)
				insert_metapage(folio, NULL);
			inc_io(folio);
			if (bio)
				submit_bio(bio);

			bio = bio_alloc(inode->i_sb->s_bdev, 1, REQ_OP_READ,
					GFP_NOFS);
			bio->bi_iter.bi_sector =
				pblock << (inode->i_blkbits - 9);
			bio->bi_end_io = metapage_read_end_io;
			bio->bi_private = folio;
			len = xlen << inode->i_blkbits;
			offset = block_offset << inode->i_blkbits;
			bio_add_folio_nofail(bio, folio, len, offset);
			block_offset += xlen;
		} else
			block_offset++;
	}
	if (bio)
		submit_bio(bio);
	else
		folio_unlock(folio);

	return 0;
}

static bool metapage_release_folio(struct folio *folio, gfp_t gfp_mask)
{
	struct metapage *mp;
	bool ret = true;
	int offset;

	for (offset = 0; offset < PAGE_SIZE; offset += PSIZE) {
		mp = folio_to_mp(folio, offset);

		if (!mp)
			continue;

		jfs_info("metapage_release_folio: mp = 0x%p", mp);
		if (mp->count || mp->nohomeok ||
		    test_bit(META_dirty, &mp->flag)) {
			jfs_info("count = %ld, nohomeok = %d", mp->count,
				 mp->nohomeok);
			ret = false;
			continue;
		}
		if (mp->lsn)
			remove_from_logsync(mp);
		remove_metapage(folio, mp);
		INCREMENT(mpStat.pagefree);
		free_metapage(mp);
	}
	return ret;
}

#ifdef CONFIG_MIGRATION
/*
 * metapage_migrate_folio - Migration function for JFS metapages
 */
static int metapage_migrate_folio(struct address_space *mapping,
				  struct folio *dst, struct folio *src,
				  enum migrate_mode mode)
{
	int expected_count;

	if (!src->private)
		return filemap_migrate_folio(mapping, dst, src, mode);

	/* Check whether page does not have extra refs before we do more work */
	expected_count = folio_expected_ref_count(src) + 1;
	if (folio_ref_count(src) != expected_count)
		return -EAGAIN;
	return __metapage_migrate_folio(mapping, dst, src, mode);
}
#else
#define metapage_migrate_folio NULL
#endif	/* CONFIG_MIGRATION */

static void metapage_invalidate_folio(struct folio *folio, size_t offset,
				    size_t length)
{
	BUG_ON(offset || length < folio_size(folio));

	BUG_ON(folio_test_writeback(folio));

	metapage_release_folio(folio, 0);
}

const struct address_space_operations jfs_metapage_aops = {
	.read_folio	= metapage_read_folio,
	.writepages	= metapage_writepages,
	.release_folio	= metapage_release_folio,
	.invalidate_folio = metapage_invalidate_folio,
	.dirty_folio	= filemap_dirty_folio,
	.migrate_folio	= metapage_migrate_folio,
};

struct metapage *__get_metapage(struct inode *inode, unsigned long lblock,
				unsigned int size, int absolute,
				unsigned long new)
{
	int l2BlocksPerPage;
	int l2bsize;
	struct address_space *mapping;
	struct metapage *mp = NULL;
	struct folio *folio;
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
		folio = filemap_grab_folio(mapping, page_index);
		if (IS_ERR(folio)) {
			jfs_err("filemap_grab_folio failed!");
			return NULL;
		}
		folio_mark_uptodate(folio);
	} else {
		folio = read_mapping_folio(mapping, page_index, NULL);
		if (IS_ERR(folio)) {
			jfs_err("read_mapping_page failed!");
			return NULL;
		}
		folio_lock(folio);
	}

	mp = folio_to_mp(folio, page_offset);
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
		mp->folio = folio;
		mp->sb = inode->i_sb;
		mp->flag = 0;
		mp->xflag = COMMIT_PAGE;
		mp->count = 1;
		mp->nohomeok = 0;
		mp->logical_size = size;
		mp->data = folio_address(folio) + page_offset;
		mp->index = lblock;
		if (unlikely(insert_metapage(folio, mp))) {
			free_metapage(mp);
			goto unlock;
		}
		lock_metapage(mp);
	}

	if (new) {
		jfs_info("zeroing mp = 0x%p", mp);
		memset(mp->data, 0, PSIZE);
	}

	folio_unlock(folio);
	jfs_info("__get_metapage: returning = 0x%p data = 0x%p", mp, mp->data);
	return mp;

unlock:
	folio_unlock(folio);
	return NULL;
}

void grab_metapage(struct metapage * mp)
{
	jfs_info("grab_metapage: mp = 0x%p", mp);
	folio_get(mp->folio);
	folio_lock(mp->folio);
	mp->count++;
	lock_metapage(mp);
	folio_unlock(mp->folio);
}

static int metapage_write_one(struct folio *folio)
{
	struct address_space *mapping = folio->mapping;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = folio_nr_pages(folio),
	};
	int ret = 0;

	BUG_ON(!folio_test_locked(folio));

	folio_wait_writeback(folio);

	if (folio_clear_dirty_for_io(folio)) {
		folio_get(folio);
		ret = metapage_write_folio(folio, &wbc);
		if (ret == 0)
			folio_wait_writeback(folio);
		folio_put(folio);
	} else {
		folio_unlock(folio);
	}

	if (!ret)
		ret = filemap_check_errors(mapping);
	return ret;
}

void force_metapage(struct metapage *mp)
{
	struct folio *folio = mp->folio;
	jfs_info("force_metapage: mp = 0x%p", mp);
	set_bit(META_forcewrite, &mp->flag);
	clear_bit(META_sync, &mp->flag);
	folio_get(folio);
	folio_lock(folio);
	folio_mark_dirty(folio);
	if (metapage_write_one(folio))
		jfs_error(mp->sb, "metapage_write_one() failed\n");
	clear_bit(META_forcewrite, &mp->flag);
	folio_put(folio);
}

void hold_metapage(struct metapage *mp)
{
	folio_lock(mp->folio);
}

void put_metapage(struct metapage *mp)
{
	if (mp->count || mp->nohomeok) {
		/* Someone else will release this */
		folio_unlock(mp->folio);
		return;
	}
	folio_get(mp->folio);
	mp->count++;
	lock_metapage(mp);
	folio_unlock(mp->folio);
	release_metapage(mp);
}

void release_metapage(struct metapage * mp)
{
	struct folio *folio = mp->folio;
	jfs_info("release_metapage: mp = 0x%p, flag = 0x%lx", mp, mp->flag);

	folio_lock(folio);
	unlock_metapage(mp);

	assert(mp->count);
	if (--mp->count || mp->nohomeok) {
		folio_unlock(folio);
		folio_put(folio);
		return;
	}

	if (test_bit(META_dirty, &mp->flag)) {
		folio_mark_dirty(folio);
		if (test_bit(META_sync, &mp->flag)) {
			clear_bit(META_sync, &mp->flag);
			if (metapage_write_one(folio))
				jfs_error(mp->sb, "metapage_write_one() failed\n");
			folio_lock(folio);
		}
	} else if (mp->lsn)	/* discard_metapage doesn't remove it */
		remove_from_logsync(mp);

	/* Try to keep metapages from using up too much memory */
	drop_metapage(folio, mp);

	folio_unlock(folio);
	folio_put(folio);
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
	unsigned int offset;

	/*
	 * Mark metapages to discard.  They will eventually be
	 * released, but should not be written.
	 */
	for (lblock = addr & ~(BlocksPerPage - 1); lblock < addr + len;
	     lblock += BlocksPerPage) {
		struct folio *folio = filemap_lock_folio(mapping,
				lblock >> l2BlocksPerPage);
		if (IS_ERR(folio))
			continue;
		for (offset = 0; offset < PAGE_SIZE; offset += PSIZE) {
			mp = folio_to_mp(folio, offset);
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
		folio_unlock(folio);
		folio_put(folio);
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
