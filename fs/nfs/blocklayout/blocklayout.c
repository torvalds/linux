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

#include "blocklayout.h"

#define NFSDBG_FACILITY	NFSDBG_PNFS_LD

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andy Adamson <andros@citi.umich.edu>");
MODULE_DESCRIPTION("The NFSv4.1 pNFS Block layout driver");

struct dentry *bl_device_pipe;
wait_queue_head_t bl_wq;

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
	struct rpc_call_ops call_ops;
	void (*pnfs_callback) (void *data);
	void *data;
};

static inline struct parallel_io *alloc_parallel(void *data)
{
	struct parallel_io *rv;

	rv  = kmalloc(sizeof(*rv), GFP_NOFS);
	if (rv) {
		rv->data = data;
		kref_init(&rv->refcnt);
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
	p->pnfs_callback(p->data);
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
			rw == READ ? "read" : "write",
			bio->bi_size, (unsigned long long)bio->bi_sector);
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

	bio = bio_alloc(GFP_NOIO, npg);
	if (!bio)
		return NULL;

	bio->bi_sector = isect - be->be_f_offset + be->be_v_offset;
	bio->bi_bdev = be->be_mdev;
	bio->bi_end_io = end_io;
	bio->bi_private = par;
	return bio;
}

static struct bio *bl_add_page_to_bio(struct bio *bio, int npg, int rw,
				      sector_t isect, struct page *page,
				      struct pnfs_block_extent *be,
				      void (*end_io)(struct bio *, int err),
				      struct parallel_io *par)
{
retry:
	if (!bio) {
		bio = bl_alloc_init_bio(npg, isect, be, end_io, par);
		if (!bio)
			return ERR_PTR(-ENOMEM);
	}
	if (bio_add_page(bio, page, PAGE_CACHE_SIZE, 0) < PAGE_CACHE_SIZE) {
		bio = bl_submit_bio(rw, bio);
		goto retry;
	}
	return bio;
}

static void bl_set_lo_fail(struct pnfs_layout_segment *lseg)
{
	if (lseg->pls_range.iomode == IOMODE_RW) {
		dprintk("%s Setting layout IOMODE_RW fail bit\n", __func__);
		set_bit(lo_fail_bit(IOMODE_RW), &lseg->pls_layout->plh_flags);
	} else {
		dprintk("%s Setting layout IOMODE_READ fail bit\n", __func__);
		set_bit(lo_fail_bit(IOMODE_READ), &lseg->pls_layout->plh_flags);
	}
}

/* This is basically copied from mpage_end_io_read */
static void bl_end_io_read(struct bio *bio, int err)
{
	struct parallel_io *par = bio->bi_private;
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;
	struct nfs_read_data *rdata = (struct nfs_read_data *)par->data;

	do {
		struct page *page = bvec->bv_page;

		if (--bvec >= bio->bi_io_vec)
			prefetchw(&bvec->bv_page->flags);
		if (uptodate)
			SetPageUptodate(page);
	} while (bvec >= bio->bi_io_vec);
	if (!uptodate) {
		if (!rdata->pnfs_error)
			rdata->pnfs_error = -EIO;
		bl_set_lo_fail(rdata->lseg);
	}
	bio_put(bio);
	put_parallel(par);
}

static void bl_read_cleanup(struct work_struct *work)
{
	struct rpc_task *task;
	struct nfs_read_data *rdata;
	dprintk("%s enter\n", __func__);
	task = container_of(work, struct rpc_task, u.tk_work);
	rdata = container_of(task, struct nfs_read_data, task);
	pnfs_ld_read_done(rdata);
}

static void
bl_end_par_io_read(void *data)
{
	struct nfs_read_data *rdata = data;

	INIT_WORK(&rdata->task.u.tk_work, bl_read_cleanup);
	schedule_work(&rdata->task.u.tk_work);
}

/* We don't want normal .rpc_call_done callback used, so we replace it
 * with this stub.
 */
static void bl_rpc_do_nothing(struct rpc_task *task, void *calldata)
{
	return;
}

static enum pnfs_try_status
bl_read_pagelist(struct nfs_read_data *rdata)
{
	int i, hole;
	struct bio *bio = NULL;
	struct pnfs_block_extent *be = NULL, *cow_read = NULL;
	sector_t isect, extent_length = 0;
	struct parallel_io *par;
	loff_t f_offset = rdata->args.offset;
	size_t count = rdata->args.count;
	struct page **pages = rdata->args.pages;
	int pg_index = rdata->args.pgbase >> PAGE_CACHE_SHIFT;

	dprintk("%s enter nr_pages %u offset %lld count %Zd\n", __func__,
	       rdata->npages, f_offset, count);

	par = alloc_parallel(rdata);
	if (!par)
		goto use_mds;
	par->call_ops = *rdata->mds_ops;
	par->call_ops.rpc_call_done = bl_rpc_do_nothing;
	par->pnfs_callback = bl_end_par_io_read;
	/* At this point, we can no longer jump to use_mds */

	isect = (sector_t) (f_offset >> SECTOR_SHIFT);
	/* Code assumes extents are page-aligned */
	for (i = pg_index; i < rdata->npages; i++) {
		if (!extent_length) {
			/* We've used up the previous extent */
			bl_put_extent(be);
			bl_put_extent(cow_read);
			bio = bl_submit_bio(READ, bio);
			/* Get the next one */
			be = bl_find_get_extent(BLK_LSEG2EXT(rdata->lseg),
					     isect, &cow_read);
			if (!be) {
				rdata->pnfs_error = -EIO;
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
		hole = is_hole(be, isect);
		if (hole && !cow_read) {
			bio = bl_submit_bio(READ, bio);
			/* Fill hole w/ zeroes w/o accessing device */
			dprintk("%s Zeroing page for hole\n", __func__);
			zero_user_segment(pages[i], 0, PAGE_CACHE_SIZE);
			print_page(pages[i]);
			SetPageUptodate(pages[i]);
		} else {
			struct pnfs_block_extent *be_read;

			be_read = (hole && cow_read) ? cow_read : be;
			bio = bl_add_page_to_bio(bio, rdata->npages - i, READ,
						 isect, pages[i], be_read,
						 bl_end_io_read, par);
			if (IS_ERR(bio)) {
				rdata->pnfs_error = PTR_ERR(bio);
				goto out;
			}
		}
		isect += PAGE_CACHE_SECTORS;
		extent_length -= PAGE_CACHE_SECTORS;
	}
	if ((isect << SECTOR_SHIFT) >= rdata->inode->i_size) {
		rdata->res.eof = 1;
		rdata->res.count = rdata->inode->i_size - f_offset;
	} else {
		rdata->res.count = (isect << SECTOR_SHIFT) - f_offset;
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
		if (be->be_state == PNFS_BLOCK_INVALID_DATA)
			bl_mark_for_commit(be, isect, len); /* What if fails? */
		isect += len;
		bl_put_extent(be);
	}
}

static void bl_end_io_write_zero(struct bio *bio, int err)
{
	struct parallel_io *par = bio->bi_private;
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;
	struct nfs_write_data *wdata = (struct nfs_write_data *)par->data;

	do {
		struct page *page = bvec->bv_page;

		if (--bvec >= bio->bi_io_vec)
			prefetchw(&bvec->bv_page->flags);
		/* This is the zeroing page we added */
		end_page_writeback(page);
		page_cache_release(page);
	} while (bvec >= bio->bi_io_vec);
	if (!uptodate) {
		if (!wdata->pnfs_error)
			wdata->pnfs_error = -EIO;
		bl_set_lo_fail(wdata->lseg);
	}
	bio_put(bio);
	put_parallel(par);
}

/* This is basically copied from mpage_end_io_read */
static void bl_end_io_write(struct bio *bio, int err)
{
	struct parallel_io *par = bio->bi_private;
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct nfs_write_data *wdata = (struct nfs_write_data *)par->data;

	if (!uptodate) {
		if (!wdata->pnfs_error)
			wdata->pnfs_error = -EIO;
		bl_set_lo_fail(wdata->lseg);
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
	struct nfs_write_data *wdata;
	dprintk("%s enter\n", __func__);
	task = container_of(work, struct rpc_task, u.tk_work);
	wdata = container_of(task, struct nfs_write_data, task);
	if (!wdata->pnfs_error) {
		/* Marks for LAYOUTCOMMIT */
		mark_extents_written(BLK_LSEG2EXT(wdata->lseg),
				     wdata->args.offset, wdata->args.count);
	}
	pnfs_ld_write_done(wdata);
}

/* Called when last of bios associated with a bl_write_pagelist call finishes */
static void bl_end_par_io_write(void *data)
{
	struct nfs_write_data *wdata = data;

	wdata->task.tk_status = 0;
	wdata->verf.committed = NFS_FILE_SYNC;
	INIT_WORK(&wdata->task.u.tk_work, bl_write_cleanup);
	schedule_work(&wdata->task.u.tk_work);
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
	bl_put_extent(cow_read);
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

static enum pnfs_try_status
bl_write_pagelist(struct nfs_write_data *wdata, int sync)
{
	int i, ret, npg_zero, pg_index, last = 0;
	struct bio *bio = NULL;
	struct pnfs_block_extent *be = NULL, *cow_read = NULL;
	sector_t isect, last_isect = 0, extent_length = 0;
	struct parallel_io *par;
	loff_t offset = wdata->args.offset;
	size_t count = wdata->args.count;
	struct page **pages = wdata->args.pages;
	struct page *page;
	pgoff_t index;
	u64 temp;
	int npg_per_block =
	    NFS_SERVER(wdata->inode)->pnfs_blksize >> PAGE_CACHE_SHIFT;

	dprintk("%s enter, %Zu@%lld\n", __func__, count, offset);
	/* At this point, wdata->pages is a (sequential) list of nfs_pages.
	 * We want to write each, and if there is an error set pnfs_error
	 * to have it redone using nfs.
	 */
	par = alloc_parallel(wdata);
	if (!par)
		return PNFS_NOT_ATTEMPTED;
	par->call_ops = *wdata->mds_ops;
	par->call_ops.rpc_call_done = bl_rpc_do_nothing;
	par->pnfs_callback = bl_end_par_io_write;
	/* At this point, have to be more careful with error handling */

	isect = (sector_t) ((offset & (long)PAGE_CACHE_MASK) >> SECTOR_SHIFT);
	be = bl_find_get_extent(BLK_LSEG2EXT(wdata->lseg), isect, &cow_read);
	if (!be || !is_writable(be, isect)) {
		dprintk("%s no matching extents!\n", __func__);
		wdata->pnfs_error = -EINVAL;
		goto out;
	}

	/* First page inside INVALID extent */
	if (be->be_state == PNFS_BLOCK_INVALID_DATA) {
		temp = offset >> PAGE_CACHE_SHIFT;
		npg_zero = do_div(temp, npg_per_block);
		isect = (sector_t) (((offset - npg_zero * PAGE_CACHE_SIZE) &
				     (long)PAGE_CACHE_MASK) >> SECTOR_SHIFT);
		extent_length = be->be_length - (isect - be->be_f_offset);

fill_invalid_ext:
		dprintk("%s need to zero %d pages\n", __func__, npg_zero);
		for (;npg_zero > 0; npg_zero--) {
			/* page ref released in bl_end_io_write_zero */
			index = isect >> PAGE_CACHE_SECTOR_SHIFT;
			dprintk("%s zero %dth page: index %lu isect %llu\n",
				__func__, npg_zero, index,
				(unsigned long long)isect);
			page =
			    find_or_create_page(wdata->inode->i_mapping, index,
						GFP_NOFS);
			if (!page) {
				dprintk("%s oom\n", __func__);
				wdata->pnfs_error = -ENOMEM;
				goto out;
			}

			/* PageDirty: Other will write this out
			 * PageWriteback: Other is writing this out
			 * PageUptodate: It was read before
			 * sector_initialized: already written out
			 */
			if (PageDirty(page) || PageWriteback(page) ||
			    bl_is_sector_init(be->be_inval, isect)) {
				print_page(page);
				unlock_page(page);
				page_cache_release(page);
				goto next_page;
			}
			if (!PageUptodate(page)) {
				/* New page, readin or zero it */
				init_page_for_write(page, cow_read);
			}
			set_page_writeback(page);
			unlock_page(page);

			ret = bl_mark_sectors_init(be->be_inval, isect,
						       PAGE_CACHE_SECTORS,
						       NULL);
			if (unlikely(ret)) {
				dprintk("%s bl_mark_sectors_init fail %d\n",
					__func__, ret);
				end_page_writeback(page);
				page_cache_release(page);
				wdata->pnfs_error = ret;
				goto out;
			}
			bio = bl_add_page_to_bio(bio, npg_zero, WRITE,
						 isect, page, be,
						 bl_end_io_write_zero, par);
			if (IS_ERR(bio)) {
				wdata->pnfs_error = PTR_ERR(bio);
				goto out;
			}
			/* FIXME: This should be done in bi_end_io */
			mark_extents_written(BLK_LSEG2EXT(wdata->lseg),
					     page->index << PAGE_CACHE_SHIFT,
					     PAGE_CACHE_SIZE);
next_page:
			isect += PAGE_CACHE_SECTORS;
			extent_length -= PAGE_CACHE_SECTORS;
		}
		if (last)
			goto write_done;
	}
	bio = bl_submit_bio(WRITE, bio);

	/* Middle pages */
	pg_index = wdata->args.pgbase >> PAGE_CACHE_SHIFT;
	for (i = pg_index; i < wdata->npages; i++) {
		if (!extent_length) {
			/* We've used up the previous extent */
			bl_put_extent(be);
			bio = bl_submit_bio(WRITE, bio);
			/* Get the next one */
			be = bl_find_get_extent(BLK_LSEG2EXT(wdata->lseg),
					     isect, NULL);
			if (!be || !is_writable(be, isect)) {
				wdata->pnfs_error = -EINVAL;
				goto out;
			}
			extent_length = be->be_length -
			    (isect - be->be_f_offset);
		}
		if (be->be_state == PNFS_BLOCK_INVALID_DATA) {
			ret = bl_mark_sectors_init(be->be_inval, isect,
						       PAGE_CACHE_SECTORS,
						       NULL);
			if (unlikely(ret)) {
				dprintk("%s bl_mark_sectors_init fail %d\n",
					__func__, ret);
				wdata->pnfs_error = ret;
				goto out;
			}
		}
		bio = bl_add_page_to_bio(bio, wdata->npages - i, WRITE,
					 isect, pages[i], be,
					 bl_end_io_write, par);
		if (IS_ERR(bio)) {
			wdata->pnfs_error = PTR_ERR(bio);
			goto out;
		}
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
	wdata->res.count = (last_isect << SECTOR_SHIFT) - (offset);
	if (count < wdata->res.count) {
		wdata->res.count = count;
	}
out:
	bl_put_extent(be);
	bl_submit_bio(WRITE, bio);
	put_parallel(par);
	return PNFS_ATTEMPTED;
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

	list_for_each_entry_safe(pos, temp, &marks->im_tree.mtt_stub, it_link) {
		list_del(&pos->it_link);
		kfree(pos);
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
		struct pnfs_block_dev *dev;
		spin_lock(&mid->bm_lock);
		while (!list_empty(&mid->bm_devlist)) {
			dev = list_first_entry(&mid->bm_devlist,
					       struct pnfs_block_dev,
					       bm_node);
			list_del(&dev->bm_node);
			bl_free_block_dev(dev);
		}
		spin_unlock(&mid->bm_lock);
		kfree(mid);
	}
}

/* This is mostly copied from the filelayout's get_device_info function.
 * It seems much of this should be at the generic pnfs level.
 */
static struct pnfs_block_dev *
nfs4_blk_get_deviceinfo(struct nfs_server *server, const struct nfs_fh *fh,
			struct nfs4_deviceid *d_id)
{
	struct pnfs_device *dev;
	struct pnfs_block_dev *rv = NULL;
	u32 max_resp_sz;
	int max_pages;
	struct page **pages = NULL;
	int i, rc;

	/*
	 * Use the session max response size as the basis for setting
	 * GETDEVICEINFO's maxcount
	 */
	max_resp_sz = server->nfs_client->cl_session->fc_attrs.max_resp_sz;
	max_pages = max_resp_sz >> PAGE_SHIFT;
	dprintk("%s max_resp_sz %u max_pages %d\n",
		__func__, max_resp_sz, max_pages);

	dev = kmalloc(sizeof(*dev), GFP_NOFS);
	if (!dev) {
		dprintk("%s kmalloc failed\n", __func__);
		return NULL;
	}

	pages = kzalloc(max_pages * sizeof(struct page *), GFP_NOFS);
	if (pages == NULL) {
		kfree(dev);
		return NULL;
	}
	for (i = 0; i < max_pages; i++) {
		pages[i] = alloc_page(GFP_NOFS);
		if (!pages[i])
			goto out_free;
	}

	memcpy(&dev->dev_id, d_id, sizeof(*d_id));
	dev->layout_type = LAYOUT_BLOCK_VOLUME;
	dev->pages = pages;
	dev->pgbase = 0;
	dev->pglen = PAGE_SIZE * max_pages;
	dev->mincount = 0;

	dprintk("%s: dev_id: %s\n", __func__, dev->dev_id.data);
	rc = nfs4_proc_getdeviceinfo(server, dev);
	dprintk("%s getdevice info returns %d\n", __func__, rc);
	if (rc)
		goto out_free;

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
	int status = 0, i;

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
			if (!bdev) {
				status = -ENODEV;
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

static const struct nfs_pageio_ops bl_pg_read_ops = {
	.pg_init = pnfs_generic_pg_init_read,
	.pg_test = pnfs_generic_pg_test,
	.pg_doio = pnfs_generic_pg_readpages,
};

static const struct nfs_pageio_ops bl_pg_write_ops = {
	.pg_init = pnfs_generic_pg_init_write,
	.pg_test = pnfs_generic_pg_test,
	.pg_doio = pnfs_generic_pg_writepages,
};

static struct pnfs_layoutdriver_type blocklayout_type = {
	.id				= LAYOUT_BLOCK_VOLUME,
	.name				= "LAYOUT_BLOCK_VOLUME",
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
	.upcall		= bl_pipe_upcall,
	.downcall	= bl_pipe_downcall,
	.destroy_msg	= bl_pipe_destroy_msg,
};

static int __init nfs4blocklayout_init(void)
{
	struct vfsmount *mnt;
	struct path path;
	int ret;

	dprintk("%s: NFSv4 Block Layout Driver Registering...\n", __func__);

	ret = pnfs_register_layoutdriver(&blocklayout_type);
	if (ret)
		goto out;

	init_waitqueue_head(&bl_wq);

	mnt = rpc_get_mount();
	if (IS_ERR(mnt)) {
		ret = PTR_ERR(mnt);
		goto out_remove;
	}

	ret = vfs_path_lookup(mnt->mnt_root,
			      mnt,
			      NFS_PIPE_DIRNAME, 0, &path);
	if (ret)
		goto out_remove;

	bl_device_pipe = rpc_mkpipe(path.dentry, "blocklayout", NULL,
				    &bl_upcall_ops, 0);
	if (IS_ERR(bl_device_pipe)) {
		ret = PTR_ERR(bl_device_pipe);
		goto out_remove;
	}
out:
	return ret;

out_remove:
	pnfs_unregister_layoutdriver(&blocklayout_type);
	return ret;
}

static void __exit nfs4blocklayout_exit(void)
{
	dprintk("%s: NFSv4 Block Layout Driver Unregistering...\n",
	       __func__);

	pnfs_unregister_layoutdriver(&blocklayout_type);
	rpc_unlink(bl_device_pipe);
}

MODULE_ALIAS("nfs-layouttype4-3");

module_init(nfs4blocklayout_init);
module_exit(nfs4blocklayout_exit);
