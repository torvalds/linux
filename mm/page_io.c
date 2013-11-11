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
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/frontswap.h>
#include <linux/aio.h>
#include <linux/blkdev.h>
#include <asm/pgtable.h>

static struct bio *get_swap_bio(gfp_t gfp_flags,
				struct page *page, bio_end_io_t end_io)
{
	struct bio *bio;

	bio = bio_alloc(gfp_flags, 1);
	if (bio) {
		bio->bi_sector = map_swap_page(page, &bio->bi_bdev);
		bio->bi_sector <<= PAGE_SHIFT - 9;
		bio->bi_io_vec[0].bv_page = page;
		bio->bi_io_vec[0].bv_len = PAGE_SIZE;
		bio->bi_io_vec[0].bv_offset = 0;
		bio->bi_vcnt = 1;
		bio->bi_size = PAGE_SIZE;
		bio->bi_end_io = end_io;
	}
	return bio;
}

void end_swap_bio_write(struct bio *bio, int err)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct page *page = bio->bi_io_vec[0].bv_page;

	if (!uptodate) {
		SetPageError(page);
		/*
		 * We failed to write the page out to swap-space.
		 * Re-dirty the page in order to avoid it being reclaimed.
		 * Also print a dire warning that things will go BAD (tm)
		 * very quickly.
		 *
		 * Also clear PG_reclaim to avoid rotate_reclaimable_page()
		 */
		set_page_dirty(page);
		printk(KERN_ALERT "Write-error on swap-device (%u:%u:%Lu)\n",
				imajor(bio->bi_bdev->bd_inode),
				iminor(bio->bi_bdev->bd_inode),
				(unsigned long long)bio->bi_sector);
		ClearPageReclaim(page);
	}
	end_page_writeback(page);
	bio_put(bio);
}

void end_swap_bio_read(struct bio *bio, int err)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct page *page = bio->bi_io_vec[0].bv_page;

	if (!uptodate) {
		SetPageError(page);
		ClearPageUptodate(page);
		printk(KERN_ALERT "Read-error on swap-device (%u:%u:%Lu)\n",
				imajor(bio->bi_bdev->bd_inode),
				iminor(bio->bi_bdev->bd_inode),
				(unsigned long long)bio->bi_sector);
		goto out;
	}

	SetPageUptodate(page);

	/*
	 * There is no guarantee that the page is in swap cache - the software
	 * suspend code (at least) uses end_swap_bio_read() against a non-
	 * swapcache page.  So we must check PG_swapcache before proceeding with
	 * this optimization.
	 */
	if (likely(PageSwapCache(page))) {
		struct swap_info_struct *sis;

		sis = page_swap_info(page);
		if (sis->flags & SWP_BLKDEV) {
			/*
			 * The swap subsystem performs lazy swap slot freeing,
			 * expecting that the page will be swapped out again.
			 * So we can avoid an unnecessary write if the page
			 * isn't redirtied.
			 * This is good for real swap storage because we can
			 * reduce unnecessary I/O and enhance wear-leveling
			 * if an SSD is used as the as swap device.
			 * But if in-memory swap device (eg zram) is used,
			 * this causes a duplicated copy between uncompressed
			 * data in VM-owned memory and compressed data in
			 * zram-owned memory.  So let's free zram-owned memory
			 * and make the VM-owned decompressed page *dirty*,
			 * so the page should be swapped out somewhere again if
			 * we again wish to reclaim it.
			 */
			struct gendisk *disk = sis->bdev->bd_disk;
			if (disk->fops->swap_slot_free_notify) {
				swp_entry_t entry;
				unsigned long offset;

				entry.val = page_private(page);
				offset = swp_offset(entry);

				SetPageDirty(page);
				disk->fops->swap_slot_free_notify(sis->bdev,
						offset);
			}
		}
	}

out:
	unlock_page(page);
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
	 * Map all the blocks into the extent list.  This code doesn't try
	 * to be very smart.
	 */
	probe_block = 0;
	page_no = 0;
	last_block = i_size_read(inode) >> blkbits;
	while ((probe_block + blocks_per_page) <= last_block &&
			page_no < sis->max) {
		unsigned block_in_page;
		sector_t first_block;

		first_block = bmap(inode, probe_block);
		if (first_block == 0)
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

			block = bmap(inode, probe_block + block_in_page);
			if (block == 0)
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
	printk(KERN_ERR "swapon: swapfile has holes\n");
	ret = -EINVAL;
	goto out;
}

/*
 * We may have stale swap cache pages in memory: notice
 * them here and get rid of the unnecessary final write.
 */
int swap_writepage(struct page *page, struct writeback_control *wbc)
{
	int ret = 0;

	if (try_to_free_swap(page)) {
		unlock_page(page);
		goto out;
	}
	if (frontswap_store(page) == 0) {
		set_page_writeback(page);
		unlock_page(page);
		end_page_writeback(page);
		goto out;
	}
	ret = __swap_writepage(page, wbc, end_swap_bio_write);
out:
	return ret;
}

int __swap_writepage(struct page *page, struct writeback_control *wbc,
	void (*end_write_func)(struct bio *, int))
{
	struct bio *bio;
	int ret = 0, rw = WRITE;
	struct swap_info_struct *sis = page_swap_info(page);

	if (sis->flags & SWP_FILE) {
		struct kiocb kiocb;
		struct file *swap_file = sis->swap_file;
		struct address_space *mapping = swap_file->f_mapping;
		struct iovec iov = {
			.iov_base = kmap(page),
			.iov_len  = PAGE_SIZE,
		};

		init_sync_kiocb(&kiocb, swap_file);
		kiocb.ki_pos = page_file_offset(page);
		kiocb.ki_left = PAGE_SIZE;
		kiocb.ki_nbytes = PAGE_SIZE;

		set_page_writeback(page);
		unlock_page(page);
		ret = mapping->a_ops->direct_IO(KERNEL_WRITE,
						&kiocb, &iov,
						kiocb.ki_pos, 1);
		kunmap(page);
		if (ret == PAGE_SIZE) {
			count_vm_event(PSWPOUT);
			ret = 0;
		} else {
			/*
			 * In the case of swap-over-nfs, this can be a
			 * temporary failure if the system has limited
			 * memory for allocating transmit buffers.
			 * Mark the page dirty and avoid
			 * rotate_reclaimable_page but rate-limit the
			 * messages but do not flag PageError like
			 * the normal direct-to-bio case as it could
			 * be temporary.
			 */
			set_page_dirty(page);
			ClearPageReclaim(page);
			pr_err_ratelimited("Write error on dio swapfile (%Lu)\n",
				page_file_offset(page));
		}
		end_page_writeback(page);
		return ret;
	}

	bio = get_swap_bio(GFP_NOIO, page, end_write_func);
	if (bio == NULL) {
		set_page_dirty(page);
		unlock_page(page);
		ret = -ENOMEM;
		goto out;
	}
	if (wbc->sync_mode == WB_SYNC_ALL)
		rw |= REQ_SYNC;
	count_vm_event(PSWPOUT);
	set_page_writeback(page);
	unlock_page(page);
	submit_bio(rw, bio);
out:
	return ret;
}

int swap_readpage(struct page *page)
{
	struct bio *bio;
	int ret = 0;
	struct swap_info_struct *sis = page_swap_info(page);

	VM_BUG_ON(!PageLocked(page));
	VM_BUG_ON(PageUptodate(page));
	if (frontswap_load(page) == 0) {
		SetPageUptodate(page);
		unlock_page(page);
		goto out;
	}

	if (sis->flags & SWP_FILE) {
		struct file *swap_file = sis->swap_file;
		struct address_space *mapping = swap_file->f_mapping;

		ret = mapping->a_ops->readpage(swap_file, page);
		if (!ret)
			count_vm_event(PSWPIN);
		return ret;
	}

	bio = get_swap_bio(GFP_KERNEL, page, end_swap_bio_read);
	if (bio == NULL) {
		unlock_page(page);
		ret = -ENOMEM;
		goto out;
	}
	count_vm_event(PSWPIN);
	submit_bio(READ, bio);
out:
	return ret;
}

int swap_set_page_dirty(struct page *page)
{
	struct swap_info_struct *sis = page_swap_info(page);

	if (sis->flags & SWP_FILE) {
		struct address_space *mapping = sis->swap_file->f_mapping;
		return mapping->a_ops->set_page_dirty(page);
	} else {
		return __set_page_dirty_no_writeback(page);
	}
}
