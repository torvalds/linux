/*
 * fs/logfs/dev_bdev.c	- Device access methods for block devices
 *
 * As should be obvious for Linux kernel code, license is GPLv2
 *
 * Copyright (c) 2005-2008 Joern Engel <joern@logfs.org>
 */
#include "logfs.h"
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>

#define PAGE_OFS(ofs) ((ofs) & (PAGE_SIZE-1))

static void request_complete(struct bio *bio, int err)
{
	complete((struct completion *)bio->bi_private);
}

static int sync_request(struct page *page, struct block_device *bdev, int rw)
{
	struct bio bio;
	struct bio_vec bio_vec;
	struct completion complete;

	bio_init(&bio);
	bio.bi_io_vec = &bio_vec;
	bio_vec.bv_page = page;
	bio_vec.bv_len = PAGE_SIZE;
	bio_vec.bv_offset = 0;
	bio.bi_vcnt = 1;
	bio.bi_idx = 0;
	bio.bi_size = PAGE_SIZE;
	bio.bi_bdev = bdev;
	bio.bi_sector = page->index * (PAGE_SIZE >> 9);
	init_completion(&complete);
	bio.bi_private = &complete;
	bio.bi_end_io = request_complete;

	submit_bio(rw, &bio);
	generic_unplug_device(bdev_get_queue(bdev));
	wait_for_completion(&complete);
	return test_bit(BIO_UPTODATE, &bio.bi_flags) ? 0 : -EIO;
}

static int bdev_readpage(void *_sb, struct page *page)
{
	struct super_block *sb = _sb;
	struct block_device *bdev = logfs_super(sb)->s_bdev;
	int err;

	err = sync_request(page, bdev, READ);
	if (err) {
		ClearPageUptodate(page);
		SetPageError(page);
	} else {
		SetPageUptodate(page);
		ClearPageError(page);
	}
	unlock_page(page);
	return err;
}

static DECLARE_WAIT_QUEUE_HEAD(wq);

static void writeseg_end_io(struct bio *bio, int err)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;
	struct super_block *sb = bio->bi_private;
	struct logfs_super *super = logfs_super(sb);
	struct page *page;

	BUG_ON(!uptodate); /* FIXME: Retry io or write elsewhere */
	BUG_ON(err);
	BUG_ON(bio->bi_vcnt == 0);
	do {
		page = bvec->bv_page;
		if (--bvec >= bio->bi_io_vec)
			prefetchw(&bvec->bv_page->flags);

		end_page_writeback(page);
	} while (bvec >= bio->bi_io_vec);
	bio_put(bio);
	if (atomic_dec_and_test(&super->s_pending_writes))
		wake_up(&wq);
}

static int __bdev_writeseg(struct super_block *sb, u64 ofs, pgoff_t index,
		size_t nr_pages)
{
	struct logfs_super *super = logfs_super(sb);
	struct address_space *mapping = super->s_mapping_inode->i_mapping;
	struct bio *bio;
	struct page *page;
	struct request_queue *q = bdev_get_queue(sb->s_bdev);
	unsigned int max_pages = queue_max_hw_sectors(q) >> (PAGE_SHIFT - 9);
	int i;

	bio = bio_alloc(GFP_NOFS, max_pages);
	BUG_ON(!bio); /* FIXME: handle this */

	for (i = 0; i < nr_pages; i++) {
		if (i >= max_pages) {
			/* Block layer cannot split bios :( */
			bio->bi_vcnt = i;
			bio->bi_idx = 0;
			bio->bi_size = i * PAGE_SIZE;
			bio->bi_bdev = super->s_bdev;
			bio->bi_sector = ofs >> 9;
			bio->bi_private = sb;
			bio->bi_end_io = writeseg_end_io;
			atomic_inc(&super->s_pending_writes);
			submit_bio(WRITE, bio);

			ofs += i * PAGE_SIZE;
			index += i;
			nr_pages -= i;
			i = 0;

			bio = bio_alloc(GFP_NOFS, max_pages);
			BUG_ON(!bio);
		}
		page = find_lock_page(mapping, index + i);
		BUG_ON(!page);
		bio->bi_io_vec[i].bv_page = page;
		bio->bi_io_vec[i].bv_len = PAGE_SIZE;
		bio->bi_io_vec[i].bv_offset = 0;

		BUG_ON(PageWriteback(page));
		set_page_writeback(page);
		unlock_page(page);
	}
	bio->bi_vcnt = nr_pages;
	bio->bi_idx = 0;
	bio->bi_size = nr_pages * PAGE_SIZE;
	bio->bi_bdev = super->s_bdev;
	bio->bi_sector = ofs >> 9;
	bio->bi_private = sb;
	bio->bi_end_io = writeseg_end_io;
	atomic_inc(&super->s_pending_writes);
	submit_bio(WRITE, bio);
	return 0;
}

static void bdev_writeseg(struct super_block *sb, u64 ofs, size_t len)
{
	struct logfs_super *super = logfs_super(sb);
	int head;

	BUG_ON(super->s_flags & LOGFS_SB_FLAG_RO);

	if (len == 0) {
		/* This can happen when the object fit perfectly into a
		 * segment, the segment gets written per sync and subsequently
		 * closed.
		 */
		return;
	}
	head = ofs & (PAGE_SIZE - 1);
	if (head) {
		ofs -= head;
		len += head;
	}
	len = PAGE_ALIGN(len);
	__bdev_writeseg(sb, ofs, ofs >> PAGE_SHIFT, len >> PAGE_SHIFT);
	generic_unplug_device(bdev_get_queue(logfs_super(sb)->s_bdev));
}


static void erase_end_io(struct bio *bio, int err) 
{ 
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags); 
	struct super_block *sb = bio->bi_private; 
	struct logfs_super *super = logfs_super(sb); 

	BUG_ON(!uptodate); /* FIXME: Retry io or write elsewhere */ 
	BUG_ON(err); 
	BUG_ON(bio->bi_vcnt == 0); 
	bio_put(bio); 
	if (atomic_dec_and_test(&super->s_pending_writes))
		wake_up(&wq); 
} 

static int do_erase(struct super_block *sb, u64 ofs, pgoff_t index,
		size_t nr_pages)
{
	struct logfs_super *super = logfs_super(sb);
	struct bio *bio;
	struct request_queue *q = bdev_get_queue(sb->s_bdev);
	unsigned int max_pages = queue_max_hw_sectors(q) >> (PAGE_SHIFT - 9);
	int i;

	bio = bio_alloc(GFP_NOFS, max_pages);
	BUG_ON(!bio); /* FIXME: handle this */

	for (i = 0; i < nr_pages; i++) {
		if (i >= max_pages) {
			/* Block layer cannot split bios :( */
			bio->bi_vcnt = i;
			bio->bi_idx = 0;
			bio->bi_size = i * PAGE_SIZE;
			bio->bi_bdev = super->s_bdev;
			bio->bi_sector = ofs >> 9;
			bio->bi_private = sb;
			bio->bi_end_io = erase_end_io;
			atomic_inc(&super->s_pending_writes);
			submit_bio(WRITE, bio);

			ofs += i * PAGE_SIZE;
			index += i;
			nr_pages -= i;
			i = 0;

			bio = bio_alloc(GFP_NOFS, max_pages);
			BUG_ON(!bio);
		}
		bio->bi_io_vec[i].bv_page = super->s_erase_page;
		bio->bi_io_vec[i].bv_len = PAGE_SIZE;
		bio->bi_io_vec[i].bv_offset = 0;
	}
	bio->bi_vcnt = nr_pages;
	bio->bi_idx = 0;
	bio->bi_size = nr_pages * PAGE_SIZE;
	bio->bi_bdev = super->s_bdev;
	bio->bi_sector = ofs >> 9;
	bio->bi_private = sb;
	bio->bi_end_io = erase_end_io;
	atomic_inc(&super->s_pending_writes);
	submit_bio(WRITE, bio);
	return 0;
}

static int bdev_erase(struct super_block *sb, loff_t to, size_t len,
		int ensure_write)
{
	struct logfs_super *super = logfs_super(sb);

	BUG_ON(to & (PAGE_SIZE - 1));
	BUG_ON(len & (PAGE_SIZE - 1));

	if (super->s_flags & LOGFS_SB_FLAG_RO)
		return -EROFS;

	if (ensure_write) {
		/*
		 * Object store doesn't care whether erases happen or not.
		 * But for the journal they are required.  Otherwise a scan
		 * can find an old commit entry and assume it is the current
		 * one, travelling back in time.
		 */
		do_erase(sb, to, to >> PAGE_SHIFT, len >> PAGE_SHIFT);
	}

	return 0;
}

static void bdev_sync(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);

	wait_event(wq, atomic_read(&super->s_pending_writes) == 0);
}

static struct page *bdev_find_first_sb(struct super_block *sb, u64 *ofs)
{
	struct logfs_super *super = logfs_super(sb);
	struct address_space *mapping = super->s_mapping_inode->i_mapping;
	filler_t *filler = bdev_readpage;

	*ofs = 0;
	return read_cache_page(mapping, 0, filler, sb);
}

static struct page *bdev_find_last_sb(struct super_block *sb, u64 *ofs)
{
	struct logfs_super *super = logfs_super(sb);
	struct address_space *mapping = super->s_mapping_inode->i_mapping;
	filler_t *filler = bdev_readpage;
	u64 pos = (super->s_bdev->bd_inode->i_size & ~0xfffULL) - 0x1000;
	pgoff_t index = pos >> PAGE_SHIFT;

	*ofs = pos;
	return read_cache_page(mapping, index, filler, sb);
}

static int bdev_write_sb(struct super_block *sb, struct page *page)
{
	struct block_device *bdev = logfs_super(sb)->s_bdev;

	/* Nothing special to do for block devices. */
	return sync_request(page, bdev, WRITE);
}

static void bdev_put_device(struct super_block *sb)
{
	close_bdev_exclusive(logfs_super(sb)->s_bdev, FMODE_READ|FMODE_WRITE);
}

static const struct logfs_device_ops bd_devops = {
	.find_first_sb	= bdev_find_first_sb,
	.find_last_sb	= bdev_find_last_sb,
	.write_sb	= bdev_write_sb,
	.readpage	= bdev_readpage,
	.writeseg	= bdev_writeseg,
	.erase		= bdev_erase,
	.sync		= bdev_sync,
	.put_device	= bdev_put_device,
};

int logfs_get_sb_bdev(struct file_system_type *type, int flags,
		const char *devname, struct vfsmount *mnt)
{
	struct block_device *bdev;

	bdev = open_bdev_exclusive(devname, FMODE_READ|FMODE_WRITE, type);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

	if (MAJOR(bdev->bd_dev) == MTD_BLOCK_MAJOR) {
		int mtdnr = MINOR(bdev->bd_dev);
		close_bdev_exclusive(bdev, FMODE_READ|FMODE_WRITE);
		return logfs_get_sb_mtd(type, flags, mtdnr, mnt);
	}

	return logfs_get_sb_device(type, flags, NULL, bdev, &bd_devops, mnt);
}
