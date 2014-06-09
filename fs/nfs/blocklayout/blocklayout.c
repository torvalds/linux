/*
 *  linux/fs/nfs/blocklayout/blocklayout.c
 *
 *  Module for the NFSv4.1 pNFS block layout driver.
 *
 *  Copyright (c) 2006 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson <andros@citi.umich.edu>
 *  Fred Isaman <iisaman@umich.edu>
 *
 * permission is granted to use, copy, create derivative works and
 * redistribute this software and such derivative works for any purpose,
 * so long as the name of the university of michigan is not used in
 * any advertising or publicity pertaining to the use or distribution
 * of this software without specific, written prior authorization.  if
 * the above copyright notice or any other identification of the
 * university of michigan is included in any copy of any portion of
 * this software, then the disclaimer below must also be included.
 *
 * this software is provided as is, without representation from the
 * university of michigan as to its fitness for any purpose, and without
 * warranty by the university of michigan of any kind, either express
 * or implied, including without limitation the implied warranties of
 * merchantability and fitness for a particular purpose.  the regents
 * of the university of michigan shall not be liable for any damages,
 * including special, indirect, incidental, or consequential damages,
 * with respect to any claim arising out or in connection with the use
 * of the software, even if it has been or is hereafter advised of the
 * possibility of such damages.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/bio.h>		/* struct bio */
#include <linux/buffer_head.h>	/* various write calls */
#include <linux/prefetch.h>
#include <linux/pagevec.h>

#include "../pnfs.h"
#include "../nfs4session.h"
#include "../internal.h"
#include "blocklayout.h"

#define NFSDBG_FACILITY	NFSDBG_PNFS_LD

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andy Adamson <andros@citi.umich.edu>");
MODULE_DESCRIPTION("The NFSv4.1 pNFS Block layout driver");

static void print_page(struct page *page)
{
	dprintk("PRINTPAGE page %p\n", page);
	dprintk("	PagePrivate %d\n", PagePrivate(page));
	dprintk("	PageUptodate %d\n", PageUptodate(page));
	dprintk("	PageError %d\n", PageError(page));
	dprintk("	PageDirty %d\n", PageDirty(page));
	dprintk("	PageReferenced %d\n", PageReferenced(page));
	dprintk("	PageLocked %d\n", PageLocked(page));
	dprintk("	PageWriteback %d\n", PageWriteback(page));
	dprintk("	PageMappedToDisk %d\n", PageMappedToDisk(page));
	dprintk("\n");
}

/* Given the be associated with isect, determine if page data needs to be
 * initialized.
 */
static int is_hole(struct pnfs_block_extent *be, sector_t isect)
{
	if (be->be_state == PNFS_BLOCK_NONE_DATA)
		return 1;
	else if (be->be_state != PNFS_BLOCK_INVALID_DATA)
		return 0;
	else
		return !bl_is_sector_init(be->be_inval, isect);
}

/* Given the be associated with isect, determine if page data can be
 * written to disk.
 */
static int is_writable(struct pnfs_block_extent *be, sector_t isect)
{
	return (be->be_state == PNFS_BLOCK_READWRITE_DATA ||
		be->be_state == PNFS_BLOCK_INVALID_DATA);
}

/* The data we are handed might be spread across several bios.  We need
 * to track when the last one is finished.
 */
struct parallel_io {
	struct kref refcnt;
	void (*pnfs_callback) (void *data, int num_se);
	void *data;
	int bse_count;
};

static inline struct parallel_io *alloc_parallel(void *data)
{
	struct parallel_io *rv;

	rv  = kmalloc(sizeof(*rv), GFP_NOFS);
	if (rv) {
		rv->data = data;
		kref_init(&rv->refcnt);
		rv->bse_count = 0;
	}
	return rv;
}

static inline void get_parallel(struct parallel_io *p)
{
	kref_get(&p->refcnt);
}

static void destroy_parallel(struct kref *kref)
{
	struct parallel_io *p = container_of(kref, struct parallel_io, refcnt);

	dprintk("%s enter\n", __func__);
	p->pnfs_callback(p->data, p->bse_count);
	kfree(p);
}

static inline void put_parallel(struct parallel_io *p)
{
	kref_put(&p->refcnt, destroy_parallel);
}

static struct bio *
bl_submit_bio(int rw, struct bio *bio)
{
	if (bio) {
		get_parallel(bio->bi_private);
		dprintk("%s submitting %s bio %u@%llu\n", __func__,
			rw == READ ? "read" : "write", bio->bi_iter.bi_size,
			(unsigned long long)bio->bi_iter.bi_sector);
		submit_bio(rw, bio);
	}
	return NULL;
}

static struct bio *bl_alloc_init_bio(int npg, sector_t isect,
				     struct pnfs_block_extent *be,
				     void (*end_io)(struct bio *, int err),
				     struct parallel_io *par)
{
	struct bio *bio;

	npg = min(npg, BIO_MAX_PAGES);
	bio = bio_alloc(GFP_NOIO, npg);
	if (!bio && (current->flags & PF_MEMALLOC)) {
		while (!bio && (npg /= 2))
			bio = bio_alloc(GFP_NOIO, npg);
	}

	if (bio) {
		bio->bi_iter.bi_sector = isect - be->be_f_offset +
			be->be_v_offset;
		bio->bi_bdev = be->be_mdev;
		bio->bi_end_io = end_io;
		bio->bi_private = par;
	}
	return bio;
}

static struct bio *do_add_page_to_bio(struct bio *bio, int npg, int rw,
				      sector_t isect, struct page *page,
				      struct pnfs_block_extent *be,
				      void (*end_io)(struct bio *, int err),
				      struct parallel_io *par,
				      unsigned int offset, int len)
{
	isect = isect + (offset >> SECTOR_SHIFT);
	dprintk("%s: npg %d rw %d isect %llu offset %u len %d\n", __func__,
		npg, rw, (unsigned long long)isect, offset, len);
retry:
	if (!bio) {
		bio = bl_alloc_init_bio(npg, isect, be, end_io, par);
		if (!bio)
			return ERR_PTR(-ENOMEM);
	}
	if (bio_add_page(bio, page, len, offset) < len) {
		bio = bl_submit_bio(rw, bio);
		goto retry;
	}
	return bio;
}

static struct bio *bl_add_page_to_bio(struct bio *bio, int npg, int rw,
				      sector_t isect, struct page *page,
				      struct pnfs_block_extent *be,
				      void (*end_io)(struct bio *, int err),
				      struct parallel_io *par)
{
	return do_add_page_to_bio(bio, npg, rw, isect, page, be,
				  end_io, par, 0, PAGE_CACHE_SIZE);
}

/* This is basically copied from mpage_end_io_read */
static void bl_end_io_read(struct bio *bio, int err)
{
	struct parallel_io *par = bio->bi_private;
	struct bio_vec *bvec;
	int i;

	if (!err)
		bio_for_each_segment_all(bvec, bio, i)
			SetPageUptodate(bvec->bv_page);

	if (err) {
		struct nfs_pgio_header *header = par->data;

		if (!header->pnfs_error)
			header->pnfs_error = -EIO;
		pnfs_set_lo_fail(header->lseg);
	}
	bio_put(bio);
	put_parallel(par);
}

static void bl_read_cleanup(struct work_struct *work)
{
	struct rpc_task *task;
	struct nfs_pgio_header *hdr;
	dprintk("%s enter\n", __func__);
	task = container_of(work, struct rpc_task, u.tk_work);
	hdr = container_of(task, struct nfs_pgio_header, task);
	pnfs_ld_read_done(hdr);
}

static void
bl_end_par_io_read(void *data, int unused)
{
	struct nfs_pgio_header *hdr = data;

	hdr->task.tk_status = hdr->pnfs_error;
	INIT_WORK(&hdr->task.u.tk_work, bl_read_cleanup);
	schedule_work(&hdr->task.u.tk_work);
}

static enum pnfs_try_status
bl_read_pagelist(struct nfs_pgio_header *hdr)
{
	struct nfs_pgio_header *header = hdr;
	int i, hole;
	struct bio *bio = NULL;
	struct pnfs_block_extent *be = NULL, *cow_read = NULL;
	sector_t isect, extent_length = 0;
	struct parallel_io *par;
	loff_t f_offset = hdr->args.offset;
	size_t bytes_left = hdr->args.count;
	unsigned int pg_offset, pg_len;
	struct page **pages = hdr->args.pages;
	int pg_index = hdr->args.pgbase >> PAGE_CACHE_SHIFT;
	const bool is_dio = (header->dreq != NULL);

	dprintk("%s enter nr_pages %u offset %lld count %u\n", __func__,
		hdr->page_array.npages, f_offset,
		(unsigned int)hdr->args.count);

	par = alloc_parallel(hdr);
	if (!par)
		goto use_mds;
	par->pnfs_callback = bl_end_par_io_read;
	/* At this point, we can no longer jump to use_mds */

	isect = (sector_t) (f_offset >> SECTOR_SHIFT);
	/* Code assumes extents are page-aligned */
	for (i = pg_index; i < hdr->page_array.npages; i++) {
		if (!extent_length) {
			/* We've used up the previous extent */
			bl_put_extent(be);
			bl_put_extent(cow_read);
			bio = bl_submit_bio(READ, bio);
			/* Get the next one */
			be = bl_find_get_extent(BLK_LSEG2EXT(header->lseg),
					     isect, &cow_read);
			if (!be) {
				header->pnfs_error = -EIO;
				goto out;
			}
			extent_length = be->be_length -
				(isect - be->be_f_offset);
			if (cow_read) {
				sector_t cow_length = cow_read->be_length -
					(isect - cow_read->be_f_offset);
				extent_length = min(extent_length, cow_length);
			}
		}

		if (is_dio) {
			pg_offset = f_offset & ~PAGE_CACHE_MASK;
			if (pg_offset + bytes_left > PAGE_CACHE_SIZE)
				pg_len = PAGE_CACHE_SIZE - pg_offset;
			else
				pg_len = bytes_left;

			f_offset += pg_len;
			bytes_left -= pg_len;
			isect += (pg_offset >> SECTOR_SHIFT);
		} else {
			pg_offset = 0;
			pg_len = PAGE_CACHE_SIZE;
		}

		hole = is_hole(be, isect);
		if (hole && !cow_read) {
			bio = bl_submit_bio(READ, bio);
			/* Fill hole w/ zeroes w/o accessing device */
			dprintk("%s Zeroing page for hole\n", __func__);
			zero_user_segment(pages[i], pg_offset, pg_len);
			print_page(pages[i]);
			SetPageUptodate(pages[i]);
		} else {
			struct pnfs_block_extent *be_read;

			be_read = (hole && cow_read) ? cow_read : be;
			bio = do_add_page_to_bio(bio,
						 hdr->page_array.npages - i,
						 READ,
						 isect, pages[i], be_read,
						 bl_end_io_read, par,
						 pg_offset, pg_len);
			if (IS_ERR(bio)) {
				header->pnfs_error = PTR_ERR(bio);
				bio = NULL;
				goto out;
			}
		}
		isect += (pg_len >> SECTOR_SHIFT);
		extent_length -= PAGE_CACHE_SECTORS;
	}
	if ((isect << SECTOR_SHIFT) >= header->inode->i_size) {
		hdr->res.eof = 1;
		hdr->res.count = header->inode->i_size - hdr->args.offset;
	} else {
		hdr->res.count = (isect << SECTOR_SHIFT) - hdr->args.offset;
	}
out:
	bl_put_extent(be);
	bl_put_extent(cow_read);
	bl_submit_bio(READ, bio);
	put_parallel(par);
	return PNFS_ATTEMPTED;

 use_mds:
	dprintk("Giving up and using normal NFS\n");
	return PNFS_NOT_ATTEMPTED;
}

static void mark_extents_written(struct pnfs_block_layout *bl,
				 __u64 offset, __u32 count)
{
	sector_t isect, end;
	struct pnfs_block_extent *be;
	struct pnfs_block_short_extent *se;

	dprintk("%s(%llu, %u)\n", __func__, offset, count);
	if (count == 0)
		return;
	isect = (offset & (long)(PAGE_CACHE_MASK)) >> SECTOR_SHIFT;
	end = (offset + count + PAGE_CACHE_SIZE - 1) & (long)(PAGE_CACHE_MASK);
	end >>= SECTOR_SHIFT;
	while (isect < end) {
		sector_t len;
		be = bl_find_get_extent(bl, isect, NULL);
		BUG_ON(!be); /* FIXME */
		len = min(end, be->be_f_offset + be->be_length) - isect;
		if (be->be_state == PNFS_BLOCK_INVALID_DATA) {
			se = bl_pop_one_short_extent(be->be_inval);
			BUG_ON(!se);
			bl_mark_for_commit(be, isect, len, se);
		}
		isect += len;
		bl_put_extent(be);
	}
}

static void bl_end_io_write_zero(struct bio *bio, int err)
{
	struct parallel_io *par = bio->bi_private;
	struct bio_vec *bvec;
	int i;

	bio_for_each_segment_all(bvec, bio, i) {
		/* This is the zeroing page we added */
		end_page_writeback(bvec->bv_page);
		page_cache_release(bvec->bv_page);
	}

	if (unlikely(err)) {
		struct nfs_pgio_header *header = par->data;

		if (!header->pnfs_error)
			header->pnfs_error = -EIO;
		pnfs_set_lo_fail(header->lseg);
	}
	bio_put(bio);
	put_parallel(par);
}

static void bl_end_io_write(struct bio *bio, int err)
{
	struct parallel_io *par = bio->bi_private;
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct nfs_pgio_header *header = par->data;

	if (!uptodate) {
		if (!header->pnfs_error)
			header->pnfs_error = -EIO;
		pnfs_set_lo_fail(header->lseg);
	}
	bio_put(bio);
	put_parallel(par);
}

/* Function scheduled for call during bl_end_par_io_write,
 * it marks sectors as written and extends the commitlist.
 */
static void bl_write_cleanup(struct work_struct *work)
{
	struct rpc_task *task;
	struct nfs_pgio_header *hdr;
	dprintk("%s enter\n", __func__);
	task = container_of(work, struct rpc_task, u.tk_work);
	hdr = container_of(task, struct nfs_pgio_header, task);
	if (likely(!hdr->pnfs_error)) {
		/* Marks for LAYOUTCOMMIT */
		mark_extents_written(BLK_LSEG2EXT(hdr->lseg),
				     hdr->args.offset, hdr->args.count);
	}
	pnfs_ld_write_done(hdr);
}

/* Called when last of bios associated with a bl_write_pagelist call finishes */
static void bl_end_par_io_write(void *data, int num_se)
{
	struct nfs_pgio_header *hdr = data;

	if (unlikely(hdr->pnfs_error)) {
		bl_free_short_extents(&BLK_LSEG2EXT(hdr->lseg)->bl_inval,
					num_se);
	}

	hdr->task.tk_status = hdr->pnfs_error;
	hdr->writeverf.committed = NFS_FILE_SYNC;
	INIT_WORK(&hdr->task.u.tk_work, bl_write_cleanup);
	schedule_work(&hdr->task.u.tk_work);
}

/* FIXME STUB - mark intersection of layout and page as bad, so is not
 * used again.
 */
static void mark_bad_read(void)
{
	return;
}

/*
 * map_block:  map a requested I/0 block (isect) into an offset in the LVM
 * block_device
 */
static void
map_block(struct buffer_head *bh, sector_t isect, struct pnfs_block_extent *be)
{
	dprintk("%s enter be=%p\n", __func__, be);

	set_buffer_mapped(bh);
	bh->b_bdev = be->be_mdev;
	bh->b_blocknr = (isect - be->be_f_offset + be->be_v_offset) >>
	    (be->be_mdev->bd_inode->i_blkbits - SECTOR_SHIFT);

	dprintk("%s isect %llu, bh->b_blocknr %ld, using bsize %Zd\n",
		__func__, (unsigned long long)isect, (long)bh->b_blocknr,
		bh->b_size);
	return;
}

static void
bl_read_single_end_io(struct bio *bio, int error)
{
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;
	struct page *page = bvec->bv_page;

	/* Only one page in bvec */
	unlock_page(page);
}

static int
bl_do_readpage_sync(struct page *page, struct pnfs_block_extent *be,
		    unsigned int offset, unsigned int len)
{
	struct bio *bio;
	struct page *shadow_page;
	sector_t isect;
	char *kaddr, *kshadow_addr;
	int ret = 0;

	dprintk("%s: offset %u len %u\n", __func__, offset, len);

	shadow_page = alloc_page(GFP_NOFS | __GFP_HIGHMEM);
	if (shadow_page == NULL)
		return -ENOMEM;

	bio = bio_alloc(GFP_NOIO, 1);
	if (bio == NULL)
		return -ENOMEM;

	isect = (page->index << PAGE_CACHE_SECTOR_SHIFT) +
		(offset / SECTOR_SIZE);

	bio->bi_iter.bi_sector = isect - be->be_f_offset + be->be_v_offset;
	bio->bi_bdev = be->be_mdev;
	bio->bi_end_io = bl_read_single_end_io;

	lock_page(shadow_page);
	if (bio_add_page(bio, shadow_page,
			 SECTOR_SIZE, round_down(offset, SECTOR_SIZE)) == 0) {
		unlock_page(shadow_page);
		bio_put(bio);
		return -EIO;
	}

	submit_bio(READ, bio);
	wait_on_page_locked(shadow_page);
	if (unlikely(!test_bit(BIO_UPTODATE, &bio->bi_flags))) {
		ret = -EIO;
	} else {
		kaddr = kmap_atomic(page);
		kshadow_addr = kmap_atomic(shadow_page);
		memcpy(kaddr + offset, kshadow_addr + offset, len);
		kunmap_atomic(kshadow_addr);
		kunmap_atomic(kaddr);
	}
	__free_page(shadow_page);
	bio_put(bio);

	return ret;
}

static int
bl_read_partial_page_sync(struct page *page, struct pnfs_block_extent *be,
			  unsigned int dirty_offset, unsigned int dirty_len,
			  bool full_page)
{
	int ret = 0;
	unsigned int start, end;

	if (full_page) {
		start = 0;
		end = PAGE_CACHE_SIZE;
	} else {
		start = round_down(dirty_offset, SECTOR_SIZE);
		end = round_up(dirty_offset + dirty_len, SECTOR_SIZE);
	}

	dprintk("%s: offset %u len %d\n", __func__, dirty_offset, dirty_len);
	if (!be) {
		zero_user_segments(page, start, dirty_offset,
				   dirty_offset + dirty_len, end);
		if (start == 0 && end == PAGE_CACHE_SIZE &&
		    trylock_page(page)) {
			SetPageUptodate(page);
			unlock_page(page);
		}
		return ret;
	}

	if (start != dirty_offset)
		ret = bl_do_readpage_sync(page, be, start, dirty_offset - start);

	if (!ret && (dirty_offset + dirty_len < end))
		ret = bl_do_readpage_sync(page, be, dirty_offset + dirty_len,
					  end - dirty_offset - dirty_len);

	return ret;
}

/* Given an unmapped page, zero it or read in page for COW, page is locked
 * by caller.
 */
static int
init_page_for_write(struct page *page, struct pnfs_block_extent *cow_read)
{
	struct buffer_head *bh = NULL;
	int ret = 0;
	sector_t isect;

	dprintk("%s enter, %p\n", __func__, page);
	BUG_ON(PageUptodate(page));
	if (!cow_read) {
		zero_user_segment(page, 0, PAGE_SIZE);
		SetPageUptodate(page);
		goto cleanup;
	}

	bh = alloc_page_buffers(page, PAGE_CACHE_SIZE, 0);
	if (!bh) {
		ret = -ENOMEM;
		goto cleanup;
	}

	isect = (sector_t) page->index << PAGE_CACHE_SECTOR_SHIFT;
	map_block(bh, isect, cow_read);
	if (!bh_uptodate_or_lock(bh))
		ret = bh_submit_read(bh);
	if (ret)
		goto cleanup;
	SetPageUptodate(page);

cleanup:
	if (bh)
		free_buffer_head(bh);
	if (ret) {
		/* Need to mark layout with bad read...should now
		 * just use nfs4 for reads and writes.
		 */
		mark_bad_read();
	}
	return ret;
}

/* Find or create a zeroing page marked being writeback.
 * Return ERR_PTR on error, NULL to indicate skip this page and page itself
 * to indicate write out.
 */
static struct page *
bl_find_get_zeroing_page(struct inode *inode, pgoff_t index,
			struct pnfs_block_extent *cow_read)
{
	struct page *page;
	int locked = 0;
	page = find_get_page(inode->i_mapping, index);
	if (page)
		goto check_page;

	page = find_or_create_page(inode->i_mapping, index, GFP_NOFS);
	if (unlikely(!page)) {
		dprintk("%s oom\n", __func__);
		return ERR_PTR(-ENOMEM);
	}
	locked = 1;

check_page:
	/* PageDirty: Other will write this out
	 * PageWriteback: Other is writing this out
	 * PageUptodate: It was read before
	 */
	if (PageDirty(page) || PageWriteback(page)) {
		print_page(page);
		if (locked)
			unlock_page(page);
		page_cache_release(page);
		return NULL;
	}

	if (!locked) {
		lock_page(page);
		locked = 1;
		goto check_page;
	}
	if (!PageUptodate(page)) {
		/* New page, readin or zero it */
		init_page_for_write(page, cow_read);
	}
	set_page_writeback(page);
	unlock_page(page);

	return page;
}

static enum pnfs_try_status
bl_write_pagelist(struct nfs_pgio_header *header, int sync)
{
	int i, ret, npg_zero, pg_index, last = 0;
	struct bio *bio = NULL;
	struct pnfs_block_extent *be = NULL, *cow_read = NULL;
	sector_t isect, last_isect = 0, extent_length = 0;
	struct parallel_io *par = NULL;
	loff_t offset = header->args.offset;
	size_t count = header->args.count;
	unsigned int pg_offset, pg_len, saved_len;
	struct page **pages = header->args.pages;
	struct page *page;
	pgoff_t index;
	u64 temp;
	int npg_per_block =
	    NFS_SERVER(header->inode)->pnfs_blksize >> PAGE_CACHE_SHIFT;

	dprintk("%s enter, %Zu@%lld\n", __func__, count, offset);

	if (header->dreq != NULL &&
	    (!IS_ALIGNED(offset, NFS_SERVER(header->inode)->pnfs_blksize) ||
	     !IS_ALIGNED(count, NFS_SERVER(header->inode)->pnfs_blksize))) {
		dprintk("pnfsblock nonblock aligned DIO writes. Resend MDS\n");
		goto out_mds;
	}
	/* At this point, header->page_aray is a (sequential) list of nfs_pages.
	 * We want to write each, and if there is an error set pnfs_error
	 * to have it redone using nfs.
	 */
	par = alloc_parallel(header);
	if (!par)
		goto out_mds;
	par->pnfs_callback = bl_end_par_io_write;
	/* At this point, have to be more careful with error handling */

	isect = (sector_t) ((offset & (long)PAGE_CACHE_MASK) >> SECTOR_SHIFT);
	be = bl_find_get_extent(BLK_LSEG2EXT(header->lseg), isect, &cow_read);
	if (!be || !is_writable(be, isect)) {
		dprintk("%s no matching extents!\n", __func__);
		goto out_mds;
	}

	/* First page inside INVALID extent */
	if (be->be_state == PNFS_BLOCK_INVALID_DATA) {
		if (likely(!bl_push_one_short_extent(be->be_inval)))
			par->bse_count++;
		else
			goto out_mds;
		temp = offset >> PAGE_CACHE_SHIFT;
		npg_zero = do_div(temp, npg_per_block);
		isect = (sector_t) (((offset - npg_zero * PAGE_CACHE_SIZE) &
				     (long)PAGE_CACHE_MASK) >> SECTOR_SHIFT);
		extent_length = be->be_length - (isect - be->be_f_offset);

fill_invalid_ext:
		dprintk("%s need to zero %d pages\n", __func__, npg_zero);
		for (;npg_zero > 0; npg_zero--) {
			if (bl_is_sector_init(be->be_inval, isect)) {
				dprintk("isect %llu already init\n",
					(unsigned long long)isect);
				goto next_page;
			}
			/* page ref released in bl_end_io_write_zero */
			index = isect >> PAGE_CACHE_SECTOR_SHIFT;
			dprintk("%s zero %dth page: index %lu isect %llu\n",
				__func__, npg_zero, index,
				(unsigned long long)isect);
			page = bl_find_get_zeroing_page(header->inode, index,
							cow_read);
			if (unlikely(IS_ERR(page))) {
				header->pnfs_error = PTR_ERR(page);
				goto out;
			} else if (page == NULL)
				goto next_page;

			ret = bl_mark_sectors_init(be->be_inval, isect,
						       PAGE_CACHE_SECTORS);
			if (unlikely(ret)) {
				dprintk("%s bl_mark_sectors_init fail %d\n",
					__func__, ret);
				end_page_writeback(page);
				page_cache_release(page);
				header->pnfs_error = ret;
				goto out;
			}
			if (likely(!bl_push_one_short_extent(be->be_inval)))
				par->bse_count++;
			else {
				end_page_writeback(page);
				page_cache_release(page);
				header->pnfs_error = -ENOMEM;
				goto out;
			}
			/* FIXME: This should be done in bi_end_io */
			mark_extents_written(BLK_LSEG2EXT(header->lseg),
					     page->index << PAGE_CACHE_SHIFT,
					     PAGE_CACHE_SIZE);

			bio = bl_add_page_to_bio(bio, npg_zero, WRITE,
						 isect, page, be,
						 bl_end_io_write_zero, par);
			if (IS_ERR(bio)) {
				header->pnfs_error = PTR_ERR(bio);
				bio = NULL;
				goto out;
			}
next_page:
			isect += PAGE_CACHE_SECTORS;
			extent_length -= PAGE_CACHE_SECTORS;
		}
		if (last)
			goto write_done;
	}
	bio = bl_submit_bio(WRITE, bio);

	/* Middle pages */
	pg_index = header->args.pgbase >> PAGE_CACHE_SHIFT;
	for (i = pg_index; i < header->page_array.npages; i++) {
		if (!extent_length) {
			/* We've used up the previous extent */
			bl_put_extent(be);
			bl_put_extent(cow_read);
			bio = bl_submit_bio(WRITE, bio);
			/* Get the next one */
			be = bl_find_get_extent(BLK_LSEG2EXT(header->lseg),
					     isect, &cow_read);
			if (!be || !is_writable(be, isect)) {
				header->pnfs_error = -EINVAL;
				goto out;
			}
			if (be->be_state == PNFS_BLOCK_INVALID_DATA) {
				if (likely(!bl_push_one_short_extent(
								be->be_inval)))
					par->bse_count++;
				else {
					header->pnfs_error = -ENOMEM;
					goto out;
				}
			}
			extent_length = be->be_length -
			    (isect - be->be_f_offset);
		}

		dprintk("%s offset %lld count %Zu\n", __func__, offset, count);
		pg_offset = offset & ~PAGE_CACHE_MASK;
		if (pg_offset + count > PAGE_CACHE_SIZE)
			pg_len = PAGE_CACHE_SIZE - pg_offset;
		else
			pg_len = count;

		saved_len = pg_len;
		if (be->be_state == PNFS_BLOCK_INVALID_DATA &&
		    !bl_is_sector_init(be->be_inval, isect)) {
			ret = bl_read_partial_page_sync(pages[i], cow_read,
							pg_offset, pg_len, true);
			if (ret) {
				dprintk("%s bl_read_partial_page_sync fail %d\n",
					__func__, ret);
				header->pnfs_error = ret;
				goto out;
			}

			ret = bl_mark_sectors_init(be->be_inval, isect,
						       PAGE_CACHE_SECTORS);
			if (unlikely(ret)) {
				dprintk("%s bl_mark_sectors_init fail %d\n",
					__func__, ret);
				header->pnfs_error = ret;
				goto out;
			}

			/* Expand to full page write */
			pg_offset = 0;
			pg_len = PAGE_CACHE_SIZE;
		} else if  ((pg_offset & (SECTOR_SIZE - 1)) ||
			    (pg_len & (SECTOR_SIZE - 1))){
			/* ahh, nasty case. We have to do sync full sector
			 * read-modify-write cycles.
			 */
			unsigned int saved_offset = pg_offset;
			ret = bl_read_partial_page_sync(pages[i], be, pg_offset,
							pg_len, false);
			pg_offset = round_down(pg_offset, SECTOR_SIZE);
			pg_len = round_up(saved_offset + pg_len, SECTOR_SIZE)
				 - pg_offset;
		}


		bio = do_add_page_to_bio(bio, header->page_array.npages - i,
					 WRITE,
					 isect, pages[i], be,
					 bl_end_io_write, par,
					 pg_offset, pg_len);
		if (IS_ERR(bio)) {
			header->pnfs_error = PTR_ERR(bio);
			bio = NULL;
			goto out;
		}
		offset += saved_len;
		count -= saved_len;
		isect += PAGE_CACHE_SECTORS;
		last_isect = isect;
		extent_length -= PAGE_CACHE_SECTORS;
	}

	/* Last page inside INVALID extent */
	if (be->be_state == PNFS_BLOCK_INVALID_DATA) {
		bio = bl_submit_bio(WRITE, bio);
		temp = last_isect >> PAGE_CACHE_SECTOR_SHIFT;
		npg_zero = npg_per_block - do_div(temp, npg_per_block);
		if (npg_zero < npg_per_block) {
			last = 1;
			goto fill_invalid_ext;
		}
	}

write_done:
	header->res.count = header->args.count;
out:
	bl_put_extent(be);
	bl_put_extent(cow_read);
	bl_submit_bio(WRITE, bio);
	put_parallel(par);
	return PNFS_ATTEMPTED;
out_mds:
	bl_put_extent(be);
	bl_put_extent(cow_read);
	kfree(par);
	return PNFS_NOT_ATTEMPTED;
}

/* FIXME - range ignored */
static void
release_extents(struct pnfs_block_layout *bl, struct pnfs_layout_range *range)
{
	int i;
	struct pnfs_block_extent *be;

	spin_lock(&bl->bl_ext_lock);
	for (i = 0; i < EXTENT_LISTS; i++) {
		while (!list_empty(&bl->bl_extents[i])) {
			be = list_first_entry(&bl->bl_extents[i],
					      struct pnfs_block_extent,
					      be_node);
			list_del(&be->be_node);
			bl_put_extent(be);
		}
	}
	spin_unlock(&bl->bl_ext_lock);
}

static void
release_inval_marks(struct pnfs_inval_markings *marks)
{
	struct pnfs_inval_tracking *pos, *temp;
	struct pnfs_block_short_extent *se, *stemp;

	list_for_each_entry_safe(pos, temp, &marks->im_tree.mtt_stub, it_link) {
		list_del(&pos->it_link);
		kfree(pos);
	}

	list_for_each_entry_safe(se, stemp, &marks->im_extents, bse_node) {
		list_del(&se->bse_node);
		kfree(se);
	}
	return;
}

static void bl_free_layout_hdr(struct pnfs_layout_hdr *lo)
{
	struct pnfs_block_layout *bl = BLK_LO2EXT(lo);

	dprintk("%s enter\n", __func__);
	release_extents(bl, NULL);
	release_inval_marks(&bl->bl_inval);
	kfree(bl);
}

static struct pnfs_layout_hdr *bl_alloc_layout_hdr(struct inode *inode,
						   gfp_t gfp_flags)
{
	struct pnfs_block_layout *bl;

	dprintk("%s enter\n", __func__);
	bl = kzalloc(sizeof(*bl), gfp_flags);
	if (!bl)
		return NULL;
	spin_lock_init(&bl->bl_ext_lock);
	INIT_LIST_HEAD(&bl->bl_extents[0]);
	INIT_LIST_HEAD(&bl->bl_extents[1]);
	INIT_LIST_HEAD(&bl->bl_commit);
	INIT_LIST_HEAD(&bl->bl_committing);
	bl->bl_count = 0;
	bl->bl_blocksize = NFS_SERVER(inode)->pnfs_blksize >> SECTOR_SHIFT;
	BL_INIT_INVAL_MARKS(&bl->bl_inval, bl->bl_blocksize);
	return &bl->bl_layout;
}

static void bl_free_lseg(struct pnfs_layout_segment *lseg)
{
	dprintk("%s enter\n", __func__);
	kfree(lseg);
}

/* We pretty much ignore lseg, and store all data layout wide, so we
 * can correctly merge.
 */
static struct pnfs_layout_segment *bl_alloc_lseg(struct pnfs_layout_hdr *lo,
						 struct nfs4_layoutget_res *lgr,
						 gfp_t gfp_flags)
{
	struct pnfs_layout_segment *lseg;
	int status;

	dprintk("%s enter\n", __func__);
	lseg = kzalloc(sizeof(*lseg), gfp_flags);
	if (!lseg)
		return ERR_PTR(-ENOMEM);
	status = nfs4_blk_process_layoutget(lo, lgr, gfp_flags);
	if (status) {
		/* We don't want to call the full-blown bl_free_lseg,
		 * since on error extents were not touched.
		 */
		kfree(lseg);
		return ERR_PTR(status);
	}
	return lseg;
}

static void
bl_encode_layoutcommit(struct pnfs_layout_hdr *lo, struct xdr_stream *xdr,
		       const struct nfs4_layoutcommit_args *arg)
{
	dprintk("%s enter\n", __func__);
	encode_pnfs_block_layoutupdate(BLK_LO2EXT(lo), xdr, arg);
}

static void
bl_cleanup_layoutcommit(struct nfs4_layoutcommit_data *lcdata)
{
	struct pnfs_layout_hdr *lo = NFS_I(lcdata->args.inode)->layout;

	dprintk("%s enter\n", __func__);
	clean_pnfs_block_layoutupdate(BLK_LO2EXT(lo), &lcdata->args, lcdata->res.status);
}

static void free_blk_mountid(struct block_mount_id *mid)
{
	if (mid) {
		struct pnfs_block_dev *dev, *tmp;

		/* No need to take bm_lock as we are last user freeing bm_devlist */
		list_for_each_entry_safe(dev, tmp, &mid->bm_devlist, bm_node) {
			list_del(&dev->bm_node);
			bl_free_block_dev(dev);
		}
		kfree(mid);
	}
}

/* This is mostly copied from the filelayout_get_device_info function.
 * It seems much of this should be at the generic pnfs level.
 */
static struct pnfs_block_dev *
nfs4_blk_get_deviceinfo(struct nfs_server *server, const struct nfs_fh *fh,
			struct nfs4_deviceid *d_id)
{
	struct pnfs_device *dev;
	struct pnfs_block_dev *rv;
	u32 max_resp_sz;
	int max_pages;
	struct page **pages = NULL;
	int i, rc;

	/*
	 * Use the session max response size as the basis for setting
	 * GETDEVICEINFO's maxcount
	 */
	max_resp_sz = server->nfs_client->cl_session->fc_attrs.max_resp_sz;
	max_pages = nfs_page_array_len(0, max_resp_sz);
	dprintk("%s max_resp_sz %u max_pages %d\n",
		__func__, max_resp_sz, max_pages);

	dev = kmalloc(sizeof(*dev), GFP_NOFS);
	if (!dev) {
		dprintk("%s kmalloc failed\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	pages = kzalloc(max_pages * sizeof(struct page *), GFP_NOFS);
	if (pages == NULL) {
		kfree(dev);
		return ERR_PTR(-ENOMEM);
	}
	for (i = 0; i < max_pages; i++) {
		pages[i] = alloc_page(GFP_NOFS);
		if (!pages[i]) {
			rv = ERR_PTR(-ENOMEM);
			goto out_free;
		}
	}

	memcpy(&dev->dev_id, d_id, sizeof(*d_id));
	dev->layout_type = LAYOUT_BLOCK_VOLUME;
	dev->pages = pages;
	dev->pgbase = 0;
	dev->pglen = PAGE_SIZE * max_pages;
	dev->mincount = 0;
	dev->maxcount = max_resp_sz - nfs41_maxgetdevinfo_overhead;

	dprintk("%s: dev_id: %s\n", __func__, dev->dev_id.data);
	rc = nfs4_proc_getdeviceinfo(server, dev, NULL);
	dprintk("%s getdevice info returns %d\n", __func__, rc);
	if (rc) {
		rv = ERR_PTR(rc);
		goto out_free;
	}

	rv = nfs4_blk_decode_device(server, dev);
 out_free:
	for (i = 0; i < max_pages; i++)
		__free_page(pages[i]);
	kfree(pages);
	kfree(dev);
	return rv;
}

static int
bl_set_layoutdriver(struct nfs_server *server, const struct nfs_fh *fh)
{
	struct block_mount_id *b_mt_id = NULL;
	struct pnfs_devicelist *dlist = NULL;
	struct pnfs_block_dev *bdev;
	LIST_HEAD(block_disklist);
	int status, i;

	dprintk("%s enter\n", __func__);

	if (server->pnfs_blksize == 0) {
		dprintk("%s Server did not return blksize\n", __func__);
		return -EINVAL;
	}
	b_mt_id = kzalloc(sizeof(struct block_mount_id), GFP_NOFS);
	if (!b_mt_id) {
		status = -ENOMEM;
		goto out_error;
	}
	/* Initialize nfs4 block layout mount id */
	spin_lock_init(&b_mt_id->bm_lock);
	INIT_LIST_HEAD(&b_mt_id->bm_devlist);

	dlist = kmalloc(sizeof(struct pnfs_devicelist), GFP_NOFS);
	if (!dlist) {
		status = -ENOMEM;
		goto out_error;
	}
	dlist->eof = 0;
	while (!dlist->eof) {
		status = nfs4_proc_getdevicelist(server, fh, dlist);
		if (status)
			goto out_error;
		dprintk("%s GETDEVICELIST numdevs=%i, eof=%i\n",
			__func__, dlist->num_devs, dlist->eof);
		for (i = 0; i < dlist->num_devs; i++) {
			bdev = nfs4_blk_get_deviceinfo(server, fh,
						       &dlist->dev_id[i]);
			if (IS_ERR(bdev)) {
				status = PTR_ERR(bdev);
				goto out_error;
			}
			spin_lock(&b_mt_id->bm_lock);
			list_add(&bdev->bm_node, &b_mt_id->bm_devlist);
			spin_unlock(&b_mt_id->bm_lock);
		}
	}
	dprintk("%s SUCCESS\n", __func__);
	server->pnfs_ld_data = b_mt_id;

 out_return:
	kfree(dlist);
	return status;

 out_error:
	free_blk_mountid(b_mt_id);
	goto out_return;
}

static int
bl_clear_layoutdriver(struct nfs_server *server)
{
	struct block_mount_id *b_mt_id = server->pnfs_ld_data;

	dprintk("%s enter\n", __func__);
	free_blk_mountid(b_mt_id);
	dprintk("%s RETURNS\n", __func__);
	return 0;
}

static bool
is_aligned_req(struct nfs_page *req, unsigned int alignment)
{
	return IS_ALIGNED(req->wb_offset, alignment) &&
	       IS_ALIGNED(req->wb_bytes, alignment);
}

static void
bl_pg_init_read(struct nfs_pageio_descriptor *pgio, struct nfs_page *req)
{
	if (pgio->pg_dreq != NULL &&
	    !is_aligned_req(req, SECTOR_SIZE))
		nfs_pageio_reset_read_mds(pgio);
	else
		pnfs_generic_pg_init_read(pgio, req);
}

/*
 * Return 0 if @req cannot be coalesced into @pgio, otherwise return the number
 * of bytes (maximum @req->wb_bytes) that can be coalesced.
 */
static size_t
bl_pg_test_read(struct nfs_pageio_descriptor *pgio, struct nfs_page *prev,
		struct nfs_page *req)
{
	if (pgio->pg_dreq != NULL &&
	    !is_aligned_req(req, SECTOR_SIZE))
		return 0;

	return pnfs_generic_pg_test(pgio, prev, req);
}

/*
 * Return the number of contiguous bytes for a given inode
 * starting at page frame idx.
 */
static u64 pnfs_num_cont_bytes(struct inode *inode, pgoff_t idx)
{
	struct address_space *mapping = inode->i_mapping;
	pgoff_t end;

	/* Optimize common case that writes from 0 to end of file */
	end = DIV_ROUND_UP(i_size_read(inode), PAGE_CACHE_SIZE);
	if (end != NFS_I(inode)->npages) {
		rcu_read_lock();
		end = page_cache_next_hole(mapping, idx + 1, ULONG_MAX);
		rcu_read_unlock();
	}

	if (!end)
		return i_size_read(inode) - (idx << PAGE_CACHE_SHIFT);
	else
		return (end - idx) << PAGE_CACHE_SHIFT;
}

static void
bl_pg_init_write(struct nfs_pageio_descriptor *pgio, struct nfs_page *req)
{
	if (pgio->pg_dreq != NULL &&
	    !is_aligned_req(req, PAGE_CACHE_SIZE)) {
		nfs_pageio_reset_write_mds(pgio);
	} else {
		u64 wb_size;
		if (pgio->pg_dreq == NULL)
			wb_size = pnfs_num_cont_bytes(pgio->pg_inode,
						      req->wb_index);
		else
			wb_size = nfs_dreq_bytes_left(pgio->pg_dreq);

		pnfs_generic_pg_init_write(pgio, req, wb_size);
	}
}

/*
 * Return 0 if @req cannot be coalesced into @pgio, otherwise return the number
 * of bytes (maximum @req->wb_bytes) that can be coalesced.
 */
static size_t
bl_pg_test_write(struct nfs_pageio_descriptor *pgio, struct nfs_page *prev,
		 struct nfs_page *req)
{
	if (pgio->pg_dreq != NULL &&
	    !is_aligned_req(req, PAGE_CACHE_SIZE))
		return 0;

	return pnfs_generic_pg_test(pgio, prev, req);
}

static const struct nfs_pageio_ops bl_pg_read_ops = {
	.pg_init = bl_pg_init_read,
	.pg_test = bl_pg_test_read,
	.pg_doio = pnfs_generic_pg_readpages,
};

static const struct nfs_pageio_ops bl_pg_write_ops = {
	.pg_init = bl_pg_init_write,
	.pg_test = bl_pg_test_write,
	.pg_doio = pnfs_generic_pg_writepages,
};

static struct pnfs_layoutdriver_type blocklayout_type = {
	.id				= LAYOUT_BLOCK_VOLUME,
	.name				= "LAYOUT_BLOCK_VOLUME",
	.owner				= THIS_MODULE,
	.read_pagelist			= bl_read_pagelist,
	.write_pagelist			= bl_write_pagelist,
	.alloc_layout_hdr		= bl_alloc_layout_hdr,
	.free_layout_hdr		= bl_free_layout_hdr,
	.alloc_lseg			= bl_alloc_lseg,
	.free_lseg			= bl_free_lseg,
	.encode_layoutcommit		= bl_encode_layoutcommit,
	.cleanup_layoutcommit		= bl_cleanup_layoutcommit,
	.set_layoutdriver		= bl_set_layoutdriver,
	.clear_layoutdriver		= bl_clear_layoutdriver,
	.pg_read_ops			= &bl_pg_read_ops,
	.pg_write_ops			= &bl_pg_write_ops,
};

static const struct rpc_pipe_ops bl_upcall_ops = {
	.upcall		= rpc_pipe_generic_upcall,
	.downcall	= bl_pipe_downcall,
	.destroy_msg	= bl_pipe_destroy_msg,
};

static struct dentry *nfs4blocklayout_register_sb(struct super_block *sb,
					    struct rpc_pipe *pipe)
{
	struct dentry *dir, *dentry;

	dir = rpc_d_lookup_sb(sb, NFS_PIPE_DIRNAME);
	if (dir == NULL)
		return ERR_PTR(-ENOENT);
	dentry = rpc_mkpipe_dentry(dir, "blocklayout", NULL, pipe);
	dput(dir);
	return dentry;
}

static void nfs4blocklayout_unregister_sb(struct super_block *sb,
					  struct rpc_pipe *pipe)
{
	if (pipe->dentry)
		rpc_unlink(pipe->dentry);
}

static int rpc_pipefs_event(struct notifier_block *nb, unsigned long event,
			   void *ptr)
{
	struct super_block *sb = ptr;
	struct net *net = sb->s_fs_info;
	struct nfs_net *nn = net_generic(net, nfs_net_id);
	struct dentry *dentry;
	int ret = 0;

	if (!try_module_get(THIS_MODULE))
		return 0;

	if (nn->bl_device_pipe == NULL) {
		module_put(THIS_MODULE);
		return 0;
	}

	switch (event) {
	case RPC_PIPEFS_MOUNT:
		dentry = nfs4blocklayout_register_sb(sb, nn->bl_device_pipe);
		if (IS_ERR(dentry)) {
			ret = PTR_ERR(dentry);
			break;
		}
		nn->bl_device_pipe->dentry = dentry;
		break;
	case RPC_PIPEFS_UMOUNT:
		if (nn->bl_device_pipe->dentry)
			nfs4blocklayout_unregister_sb(sb, nn->bl_device_pipe);
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}
	module_put(THIS_MODULE);
	return ret;
}

static struct notifier_block nfs4blocklayout_block = {
	.notifier_call = rpc_pipefs_event,
};

static struct dentry *nfs4blocklayout_register_net(struct net *net,
						   struct rpc_pipe *pipe)
{
	struct super_block *pipefs_sb;
	struct dentry *dentry;

	pipefs_sb = rpc_get_sb_net(net);
	if (!pipefs_sb)
		return NULL;
	dentry = nfs4blocklayout_register_sb(pipefs_sb, pipe);
	rpc_put_sb_net(net);
	return dentry;
}

static void nfs4blocklayout_unregister_net(struct net *net,
					   struct rpc_pipe *pipe)
{
	struct super_block *pipefs_sb;

	pipefs_sb = rpc_get_sb_net(net);
	if (pipefs_sb) {
		nfs4blocklayout_unregister_sb(pipefs_sb, pipe);
		rpc_put_sb_net(net);
	}
}

static int nfs4blocklayout_net_init(struct net *net)
{
	struct nfs_net *nn = net_generic(net, nfs_net_id);
	struct dentry *dentry;

	init_waitqueue_head(&nn->bl_wq);
	nn->bl_device_pipe = rpc_mkpipe_data(&bl_upcall_ops, 0);
	if (IS_ERR(nn->bl_device_pipe))
		return PTR_ERR(nn->bl_device_pipe);
	dentry = nfs4blocklayout_register_net(net, nn->bl_device_pipe);
	if (IS_ERR(dentry)) {
		rpc_destroy_pipe_data(nn->bl_device_pipe);
		return PTR_ERR(dentry);
	}
	nn->bl_device_pipe->dentry = dentry;
	return 0;
}

static void nfs4blocklayout_net_exit(struct net *net)
{
	struct nfs_net *nn = net_generic(net, nfs_net_id);

	nfs4blocklayout_unregister_net(net, nn->bl_device_pipe);
	rpc_destroy_pipe_data(nn->bl_device_pipe);
	nn->bl_device_pipe = NULL;
}

static struct pernet_operations nfs4blocklayout_net_ops = {
	.init = nfs4blocklayout_net_init,
	.exit = nfs4blocklayout_net_exit,
};

static int __init nfs4blocklayout_init(void)
{
	int ret;

	dprintk("%s: NFSv4 Block Layout Driver Registering...\n", __func__);

	ret = pnfs_register_layoutdriver(&blocklayout_type);
	if (ret)
		goto out;

	ret = rpc_pipefs_notifier_register(&nfs4blocklayout_block);
	if (ret)
		goto out_remove;
	ret = register_pernet_subsys(&nfs4blocklayout_net_ops);
	if (ret)
		goto out_notifier;
out:
	return ret;

out_notifier:
	rpc_pipefs_notifier_unregister(&nfs4blocklayout_block);
out_remove:
	pnfs_unregister_layoutdriver(&blocklayout_type);
	return ret;
}

static void __exit nfs4blocklayout_exit(void)
{
	dprintk("%s: NFSv4 Block Layout Driver Unregistering...\n",
	       __func__);

	rpc_pipefs_notifier_unregister(&nfs4blocklayout_block);
	unregister_pernet_subsys(&nfs4blocklayout_net_ops);
	pnfs_unregister_layoutdriver(&blocklayout_type);
}

MODULE_ALIAS("nfs-layouttype4-3");

module_init(nfs4blocklayout_init);
module_exit(nfs4blocklayout_exit);
