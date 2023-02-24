// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/mm/page_io.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, 
 *  Asynchronous swapping added 30.12.95. Stephen Tweedie
 *  Removed race in async swapping. 14.4.1996. Bruno Haible
 *  Add swap of shared pages through the page cache. 20.2.1998. Stephen Tweedie
 *  Always use brw_page, life becomes simpler. 12 May 1998 Eric Biederman
 */

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/gfp.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/bio.h>
#include <linux/swapops.h>
#include <linux/writeback.h>
#include <linux/frontswap.h>
#include <linux/blkdev.h>
#include <linux/psi.h>
#include <linux/uio.h>
#include <linux/sched/task.h>
#include <linux/delayacct.h>
#include "swap.h"

static void __end_swap_bio_write(struct bio *bio)
{
	struct page *page = bio_first_page_all(bio);

	if (bio->bi_status) {
		SetPageError(page);
		/*
		 * We failed to write the page out to swap-space.
		 * Re-dirty the page in order to avoid it being reclaimed.
		 * Also print a dire warning that things will go BAD (tm)
		 * very quickly.
		 *
		 * Also clear PG_reclaim to avoid folio_rotate_reclaimable()
		 */
		set_page_dirty(page);
		pr_alert_ratelimited("Write-error on swap-device (%u:%u:%llu)\n",
				     MAJOR(bio_dev(bio)), MINOR(bio_dev(bio)),
				     (unsigned long long)bio->bi_iter.bi_sector);
		ClearPageReclaim(page);
	}
	end_page_writeback(page);
}

static void end_swap_bio_write(struct bio *bio)
{
	__end_swap_bio_write(bio);
	bio_put(bio);
}

static void __end_swap_bio_read(struct bio *bio)
{
	struct page *page = bio_first_page_all(bio);

	if (bio->bi_status) {
		SetPageError(page);
		ClearPageUptodate(page);
		pr_alert_ratelimited("Read-error on swap-device (%u:%u:%llu)\n",
				     MAJOR(bio_dev(bio)), MINOR(bio_dev(bio)),
				     (unsigned long long)bio->bi_iter.bi_sector);
	} else {
		SetPageUptodate(page);
	}
	unlock_page(page);
}

static void end_swap_bio_read(struct bio *bio)
{
	__end_swap_bio_read(bio);
	bio_put(bio);
}

int generic_swapfile_activate(struct swap_info_struct *sis,
				struct file *swap_file,
				sector_t *span)
{
	struct address_space *mapping = swap_file->f_mapping;
	struct inode *inode = mapping->host;
	unsigned blocks_per_page;
	unsigned long page_no;
	unsigned blkbits;
	sector_t probe_block;
	sector_t last_block;
	sector_t lowest_block = -1;
	sector_t highest_block = 0;
	int nr_extents = 0;
	int ret;

	blkbits = inode->i_blkbits;
	blocks_per_page = PAGE_SIZE >> blkbits;

	/*
	 * Map all the blocks into the extent tree.  This code doesn't try
	 * to be very smart.
	 */
	probe_block = 0;
	page_no = 0;
	last_block = i_size_read(inode) >> blkbits;
	while ((probe_block + blocks_per_page) <= last_block &&
			page_no < sis->max) {
		unsigned block_in_page;
		sector_t first_block;

		cond_resched();

		first_block = probe_block;
		ret = bmap(inode, &first_block);
		if (ret || !first_block)
			goto bad_bmap;

		/*
		 * It must be PAGE_SIZE aligned on-disk
		 */
		if (first_block & (blocks_per_page - 1)) {
			probe_block++;
			goto reprobe;
		}

		for (block_in_page = 1; block_in_page < blocks_per_page;
					block_in_page++) {
			sector_t block;

			block = probe_block + block_in_page;
			ret = bmap(inode, &block);
			if (ret || !block)
				goto bad_bmap;

			if (block != first_block + block_in_page) {
				/* Discontiguity */
				probe_block++;
				goto reprobe;
			}
		}

		first_block >>= (PAGE_SHIFT - blkbits);
		if (page_no) {	/* exclude the header page */
			if (first_block < lowest_block)
				lowest_block = first_block;
			if (first_block > highest_block)
				highest_block = first_block;
		}

		/*
		 * We found a PAGE_SIZE-length, PAGE_SIZE-aligned run of blocks
		 */
		ret = add_swap_extent(sis, page_no, 1, first_block);
		if (ret < 0)
			goto out;
		nr_extents += ret;
		page_no++;
		probe_block += blocks_per_page;
reprobe:
		continue;
	}
	ret = nr_extents;
	*span = 1 + highest_block - lowest_block;
	if (page_no == 0)
		page_no = 1;	/* force Empty message */
	sis->max = page_no;
	sis->pages = page_no - 1;
	sis->highest_bit = page_no - 1;
out:
	return ret;
bad_bmap:
	pr_err("swapon: swapfile has holes\n");
	ret = -EINVAL;
	goto out;
}

/*
 * We may have stale swap cache pages in memory: notice
 * them here and get rid of the unnecessary final write.
 */
int swap_writepage(struct page *page, struct writeback_control *wbc)
{
	struct folio *folio = page_folio(page);
	int ret;

	if (folio_free_swap(folio)) {
		folio_unlock(folio);
		return 0;
	}
	/*
	 * Arch code may have to preserve more data than just the page
	 * contents, e.g. memory tags.
	 */
	ret = arch_prepare_to_swap(&folio->page);
	if (ret) {
		folio_mark_dirty(folio);
		folio_unlock(folio);
		return ret;
	}
	if (frontswap_store(&folio->page) == 0) {
		folio_start_writeback(folio);
		folio_unlock(folio);
		folio_end_writeback(folio);
		return 0;
	}
	__swap_writepage(&folio->page, wbc);
	return 0;
}

static inline void count_swpout_vm_event(struct page *page)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	if (unlikely(PageTransHuge(page)))
		count_vm_event(THP_SWPOUT);
#endif
	count_vm_events(PSWPOUT, thp_nr_pages(page));
}

#if defined(CONFIG_MEMCG) && defined(CONFIG_BLK_CGROUP)
static void bio_associate_blkg_from_page(struct bio *bio, struct page *page)
{
	struct cgroup_subsys_state *css;
	struct mem_cgroup *memcg;

	memcg = page_memcg(page);
	if (!memcg)
		return;

	rcu_read_lock();
	css = cgroup_e_css(memcg->css.cgroup, &io_cgrp_subsys);
	bio_associate_blkg_from_css(bio, css);
	rcu_read_unlock();
}
#else
#define bio_associate_blkg_from_page(bio, page)		do { } while (0)
#endif /* CONFIG_MEMCG && CONFIG_BLK_CGROUP */

struct swap_iocb {
	struct kiocb		iocb;
	struct bio_vec		bvec[SWAP_CLUSTER_MAX];
	int			pages;
	int			len;
};
static mempool_t *sio_pool;

int sio_pool_init(void)
{
	if (!sio_pool) {
		mempool_t *pool = mempool_create_kmalloc_pool(
			SWAP_CLUSTER_MAX, sizeof(struct swap_iocb));
		if (cmpxchg(&sio_pool, NULL, pool))
			mempool_destroy(pool);
	}
	if (!sio_pool)
		return -ENOMEM;
	return 0;
}

static void sio_write_complete(struct kiocb *iocb, long ret)
{
	struct swap_iocb *sio = container_of(iocb, struct swap_iocb, iocb);
	struct page *page = sio->bvec[0].bv_page;
	int p;

	if (ret != sio->len) {
		/*
		 * In the case of swap-over-nfs, this can be a
		 * temporary failure if the system has limited
		 * memory for allocating transmit buffers.
		 * Mark the page dirty and avoid
		 * folio_rotate_reclaimable but rate-limit the
		 * messages but do not flag PageError like
		 * the normal direct-to-bio case as it could
		 * be temporary.
		 */
		pr_err_ratelimited("Write error %ld on dio swapfile (%llu)\n",
				   ret, page_file_offset(page));
		for (p = 0; p < sio->pages; p++) {
			page = sio->bvec[p].bv_page;
			set_page_dirty(page);
			ClearPageReclaim(page);
		}
	} else {
		for (p = 0; p < sio->pages; p++)
			count_swpout_vm_event(sio->bvec[p].bv_page);
	}

	for (p = 0; p < sio->pages; p++)
		end_page_writeback(sio->bvec[p].bv_page);

	mempool_free(sio, sio_pool);
}

static void swap_writepage_fs(struct page *page, struct writeback_control *wbc)
{
	struct swap_iocb *sio = NULL;
	struct swap_info_struct *sis = page_swap_info(page);
	struct file *swap_file = sis->swap_file;
	loff_t pos = page_file_offset(page);

	set_page_writeback(page);
	unlock_page(page);
	if (wbc->swap_plug)
		sio = *wbc->swap_plug;
	if (sio) {
		if (sio->iocb.ki_filp != swap_file ||
		    sio->iocb.ki_pos + sio->len != pos) {
			swap_write_unplug(sio);
			sio = NULL;
		}
	}
	if (!sio) {
		sio = mempool_alloc(sio_pool, GFP_NOIO);
		init_sync_kiocb(&sio->iocb, swap_file);
		sio->iocb.ki_complete = sio_write_complete;
		sio->iocb.ki_pos = pos;
		sio->pages = 0;
		sio->len = 0;
	}
	bvec_set_page(&sio->bvec[sio->pages], page, thp_size(page), 0);
	sio->len += thp_size(page);
	sio->pages += 1;
	if (sio->pages == ARRAY_SIZE(sio->bvec) || !wbc->swap_plug) {
		swap_write_unplug(sio);
		sio = NULL;
	}
	if (wbc->swap_plug)
		*wbc->swap_plug = sio;
}

static void swap_writepage_bdev_sync(struct page *page,
		struct writeback_control *wbc, struct swap_info_struct *sis)
{
	struct bio_vec bv;
	struct bio bio;

	bio_init(&bio, sis->bdev, &bv, 1,
		 REQ_OP_WRITE | REQ_SWAP | wbc_to_write_flags(wbc));
	bio.bi_iter.bi_sector = swap_page_sector(page);
	bio_add_page(&bio, page, thp_size(page), 0);

	bio_associate_blkg_from_page(&bio, page);
	count_swpout_vm_event(page);

	set_page_writeback(page);
	unlock_page(page);

	submit_bio_wait(&bio);
	__end_swap_bio_write(&bio);
}

static void swap_writepage_bdev_async(struct page *page,
		struct writeback_control *wbc, struct swap_info_struct *sis)
{
	struct bio *bio;

	bio = bio_alloc(sis->bdev, 1,
			REQ_OP_WRITE | REQ_SWAP | wbc_to_write_flags(wbc),
			GFP_NOIO);
	bio->bi_iter.bi_sector = swap_page_sector(page);
	bio->bi_end_io = end_swap_bio_write;
	bio_add_page(bio, page, thp_size(page), 0);

	bio_associate_blkg_from_page(bio, page);
	count_swpout_vm_event(page);
	set_page_writeback(page);
	unlock_page(page);
	submit_bio(bio);
}

void __swap_writepage(struct page *page, struct writeback_control *wbc)
{
	struct swap_info_struct *sis = page_swap_info(page);

	VM_BUG_ON_PAGE(!PageSwapCache(page), page);
	/*
	 * ->flags can be updated non-atomicially (scan_swap_map_slots),
	 * but that will never affect SWP_FS_OPS, so the data_race
	 * is safe.
	 */
	if (data_race(sis->flags & SWP_FS_OPS))
		swap_writepage_fs(page, wbc);
	else if (sis->flags & SWP_SYNCHRONOUS_IO)
		swap_writepage_bdev_sync(page, wbc, sis);
	else
		swap_writepage_bdev_async(page, wbc, sis);
}

void swap_write_unplug(struct swap_iocb *sio)
{
	struct iov_iter from;
	struct address_space *mapping = sio->iocb.ki_filp->f_mapping;
	int ret;

	iov_iter_bvec(&from, ITER_SOURCE, sio->bvec, sio->pages, sio->len);
	ret = mapping->a_ops->swap_rw(&sio->iocb, &from);
	if (ret != -EIOCBQUEUED)
		sio_write_complete(&sio->iocb, ret);
}

static void sio_read_complete(struct kiocb *iocb, long ret)
{
	struct swap_iocb *sio = container_of(iocb, struct swap_iocb, iocb);
	int p;

	if (ret == sio->len) {
		for (p = 0; p < sio->pages; p++) {
			struct page *page = sio->bvec[p].bv_page;

			SetPageUptodate(page);
			unlock_page(page);
		}
		count_vm_events(PSWPIN, sio->pages);
	} else {
		for (p = 0; p < sio->pages; p++) {
			struct page *page = sio->bvec[p].bv_page;

			SetPageError(page);
			ClearPageUptodate(page);
			unlock_page(page);
		}
		pr_alert_ratelimited("Read-error on swap-device\n");
	}
	mempool_free(sio, sio_pool);
}

static void swap_readpage_fs(struct page *page,
			     struct swap_iocb **plug)
{
	struct swap_info_struct *sis = page_swap_info(page);
	struct swap_iocb *sio = NULL;
	loff_t pos = page_file_offset(page);

	if (plug)
		sio = *plug;
	if (sio) {
		if (sio->iocb.ki_filp != sis->swap_file ||
		    sio->iocb.ki_pos + sio->len != pos) {
			swap_read_unplug(sio);
			sio = NULL;
		}
	}
	if (!sio) {
		sio = mempool_alloc(sio_pool, GFP_KERNEL);
		init_sync_kiocb(&sio->iocb, sis->swap_file);
		sio->iocb.ki_pos = pos;
		sio->iocb.ki_complete = sio_read_complete;
		sio->pages = 0;
		sio->len = 0;
	}
	bvec_set_page(&sio->bvec[sio->pages], page, thp_size(page), 0);
	sio->len += thp_size(page);
	sio->pages += 1;
	if (sio->pages == ARRAY_SIZE(sio->bvec) || !plug) {
		swap_read_unplug(sio);
		sio = NULL;
	}
	if (plug)
		*plug = sio;
}

static void swap_readpage_bdev_sync(struct page *page,
		struct swap_info_struct *sis)
{
	struct bio_vec bv;
	struct bio bio;

	bio_init(&bio, sis->bdev, &bv, 1, REQ_OP_READ);
	bio.bi_iter.bi_sector = swap_page_sector(page);
	bio_add_page(&bio, page, thp_size(page), 0);
	/*
	 * Keep this task valid during swap readpage because the oom killer may
	 * attempt to access it in the page fault retry time check.
	 */
	get_task_struct(current);
	count_vm_event(PSWPIN);
	submit_bio_wait(&bio);
	__end_swap_bio_read(&bio);
	put_task_struct(current);
}

static void swap_readpage_bdev_async(struct page *page,
		struct swap_info_struct *sis)
{
	struct bio *bio;

	bio = bio_alloc(sis->bdev, 1, REQ_OP_READ, GFP_KERNEL);
	bio->bi_iter.bi_sector = swap_page_sector(page);
	bio->bi_end_io = end_swap_bio_read;
	bio_add_page(bio, page, thp_size(page), 0);
	count_vm_event(PSWPIN);
	submit_bio(bio);
}

void swap_readpage(struct page *page, bool synchronous, struct swap_iocb **plug)
{
	struct swap_info_struct *sis = page_swap_info(page);
	bool workingset = PageWorkingset(page);
	unsigned long pflags;
	bool in_thrashing;

	VM_BUG_ON_PAGE(!PageSwapCache(page) && !synchronous, page);
	VM_BUG_ON_PAGE(!PageLocked(page), page);
	VM_BUG_ON_PAGE(PageUptodate(page), page);

	/*
	 * Count submission time as memory stall and delay. When the device
	 * is congested, or the submitting cgroup IO-throttled, submission
	 * can be a significant part of overall IO time.
	 */
	if (workingset) {
		delayacct_thrashing_start(&in_thrashing);
		psi_memstall_enter(&pflags);
	}
	delayacct_swapin_start();

	if (frontswap_load(page) == 0) {
		SetPageUptodate(page);
		unlock_page(page);
	} else if (data_race(sis->flags & SWP_FS_OPS)) {
		swap_readpage_fs(page, plug);
	} else if (synchronous || (sis->flags & SWP_SYNCHRONOUS_IO)) {
		swap_readpage_bdev_sync(page, sis);
	} else {
		swap_readpage_bdev_async(page, sis);
	}

	if (workingset) {
		delayacct_thrashing_end(&in_thrashing);
		psi_memstall_leave(&pflags);
	}
	delayacct_swapin_end();
}

void __swap_read_unplug(struct swap_iocb *sio)
{
	struct iov_iter from;
	struct address_space *mapping = sio->iocb.ki_filp->f_mapping;
	int ret;

	iov_iter_bvec(&from, ITER_DEST, sio->bvec, sio->pages, sio->len);
	ret = mapping->a_ops->swap_rw(&sio->iocb, &from);
	if (ret != -EIOCBQUEUED)
		sio_read_complete(&sio->iocb, ret);
}
