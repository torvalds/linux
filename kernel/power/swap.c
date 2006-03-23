/*
 * linux/kernel/power/swap.c
 *
 * This file provides functions for reading the suspend image from
 * and writing it to a swap partition.
 *
 * Copyright (C) 1998,2001-2005 Pavel Machek <pavel@suse.cz>
 * Copyright (C) 2006 Rafael J. Wysocki <rjw@sisk.pl>
 *
 * This file is released under the GPLv2.
 *
 */

#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/genhd.h>
#include <linux/device.h>
#include <linux/buffer_head.h>
#include <linux/bio.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/pm.h>

#include "power.h"

extern char resume_file[];

#define SWSUSP_SIG	"S1SUSPEND"

static struct swsusp_header {
	char reserved[PAGE_SIZE - 20 - sizeof(swp_entry_t)];
	swp_entry_t image;
	char	orig_sig[10];
	char	sig[10];
} __attribute__((packed, aligned(PAGE_SIZE))) swsusp_header;

/*
 * Saving part...
 */

static unsigned short root_swap = 0xffff;

static int mark_swapfiles(swp_entry_t start)
{
	int error;

	rw_swap_page_sync(READ,
			  swp_entry(root_swap, 0),
			  virt_to_page((unsigned long)&swsusp_header));
	if (!memcmp("SWAP-SPACE",swsusp_header.sig, 10) ||
	    !memcmp("SWAPSPACE2",swsusp_header.sig, 10)) {
		memcpy(swsusp_header.orig_sig,swsusp_header.sig, 10);
		memcpy(swsusp_header.sig,SWSUSP_SIG, 10);
		swsusp_header.image = start;
		error = rw_swap_page_sync(WRITE,
					  swp_entry(root_swap, 0),
					  virt_to_page((unsigned long)
						       &swsusp_header));
	} else {
		pr_debug("swsusp: Partition is not swap space.\n");
		error = -ENODEV;
	}
	return error;
}

/**
 *	swsusp_swap_check - check if the resume device is a swap device
 *	and get its index (if so)
 */

static int swsusp_swap_check(void) /* This is called before saving image */
{
	int res = swap_type_of(swsusp_resume_device);

	if (res >= 0) {
		root_swap = res;
		return 0;
	}
	return res;
}

/**
 *	write_page - Write one page to given swap location.
 *	@buf:		Address we're writing.
 *	@offset:	Offset of the swap page we're writing to.
 */

static int write_page(void *buf, unsigned long offset)
{
	swp_entry_t entry;
	int error = -ENOSPC;

	if (offset) {
		entry = swp_entry(root_swap, offset);
		error = rw_swap_page_sync(WRITE, entry, virt_to_page(buf));
	}
	return error;
}

/*
 *	The swap map is a data structure used for keeping track of each page
 *	written to a swap partition.  It consists of many swap_map_page
 *	structures that contain each an array of MAP_PAGE_SIZE swap entries.
 *	These structures are stored on the swap and linked together with the
 *	help of the .next_swap member.
 *
 *	The swap map is created during suspend.  The swap map pages are
 *	allocated and populated one at a time, so we only need one memory
 *	page to set up the entire structure.
 *
 *	During resume we also only need to use one swap_map_page structure
 *	at a time.
 */

#define MAP_PAGE_ENTRIES	(PAGE_SIZE / sizeof(long) - 1)

struct swap_map_page {
	unsigned long		entries[MAP_PAGE_ENTRIES];
	unsigned long		next_swap;
};

/**
 *	The swap_map_handle structure is used for handling swap in
 *	a file-alike way
 */

struct swap_map_handle {
	struct swap_map_page *cur;
	unsigned long cur_swap;
	struct bitmap_page *bitmap;
	unsigned int k;
};

static void release_swap_writer(struct swap_map_handle *handle)
{
	if (handle->cur)
		free_page((unsigned long)handle->cur);
	handle->cur = NULL;
	if (handle->bitmap)
		free_bitmap(handle->bitmap);
	handle->bitmap = NULL;
}

static int get_swap_writer(struct swap_map_handle *handle)
{
	handle->cur = (struct swap_map_page *)get_zeroed_page(GFP_KERNEL);
	if (!handle->cur)
		return -ENOMEM;
	handle->bitmap = alloc_bitmap(count_swap_pages(root_swap, 0));
	if (!handle->bitmap) {
		release_swap_writer(handle);
		return -ENOMEM;
	}
	handle->cur_swap = alloc_swap_page(root_swap, handle->bitmap);
	if (!handle->cur_swap) {
		release_swap_writer(handle);
		return -ENOSPC;
	}
	handle->k = 0;
	return 0;
}

static int swap_write_page(struct swap_map_handle *handle, void *buf)
{
	int error;
	unsigned long offset;

	if (!handle->cur)
		return -EINVAL;
	offset = alloc_swap_page(root_swap, handle->bitmap);
	error = write_page(buf, offset);
	if (error)
		return error;
	handle->cur->entries[handle->k++] = offset;
	if (handle->k >= MAP_PAGE_ENTRIES) {
		offset = alloc_swap_page(root_swap, handle->bitmap);
		if (!offset)
			return -ENOSPC;
		handle->cur->next_swap = offset;
		error = write_page(handle->cur, handle->cur_swap);
		if (error)
			return error;
		memset(handle->cur, 0, PAGE_SIZE);
		handle->cur_swap = offset;
		handle->k = 0;
	}
	return 0;
}

static int flush_swap_writer(struct swap_map_handle *handle)
{
	if (handle->cur && handle->cur_swap)
		return write_page(handle->cur, handle->cur_swap);
	else
		return -EINVAL;
}

/**
 *	save_image - save the suspend image data
 */

static int save_image(struct swap_map_handle *handle,
                      struct snapshot_handle *snapshot,
                      unsigned int nr_pages)
{
	unsigned int m;
	int ret;
	int error = 0;

	printk("Saving image data pages (%u pages) ...     ", nr_pages);
	m = nr_pages / 100;
	if (!m)
		m = 1;
	nr_pages = 0;
	do {
		ret = snapshot_read_next(snapshot, PAGE_SIZE);
		if (ret > 0) {
			error = swap_write_page(handle, data_of(*snapshot));
			if (error)
				break;
			if (!(nr_pages % m))
				printk("\b\b\b\b%3d%%", nr_pages / m);
			nr_pages++;
		}
	} while (ret > 0);
	if (!error)
		printk("\b\b\b\bdone\n");
	return error;
}

/**
 *	enough_swap - Make sure we have enough swap to save the image.
 *
 *	Returns TRUE or FALSE after checking the total amount of swap
 *	space avaiable from the resume partition.
 */

static int enough_swap(unsigned int nr_pages)
{
	unsigned int free_swap = count_swap_pages(root_swap, 1);

	pr_debug("swsusp: free swap pages: %u\n", free_swap);
	return free_swap > (nr_pages + PAGES_FOR_IO +
		(nr_pages + PBES_PER_PAGE - 1) / PBES_PER_PAGE);
}

/**
 *	swsusp_write - Write entire image and metadata.
 *
 *	It is important _NOT_ to umount filesystems at this point. We want
 *	them synced (in case something goes wrong) but we DO not want to mark
 *	filesystem clean: it is not. (And it does not matter, if we resume
 *	correctly, we'll mark system clean, anyway.)
 */

int swsusp_write(void)
{
	struct swap_map_handle handle;
	struct snapshot_handle snapshot;
	struct swsusp_info *header;
	unsigned long start;
	int error;

	if ((error = swsusp_swap_check())) {
		printk(KERN_ERR "swsusp: Cannot find swap device, try swapon -a.\n");
		return error;
	}
	memset(&snapshot, 0, sizeof(struct snapshot_handle));
	error = snapshot_read_next(&snapshot, PAGE_SIZE);
	if (error < PAGE_SIZE)
		return error < 0 ? error : -EFAULT;
	header = (struct swsusp_info *)data_of(snapshot);
	if (!enough_swap(header->pages)) {
		printk(KERN_ERR "swsusp: Not enough free swap\n");
		return -ENOSPC;
	}
	error = get_swap_writer(&handle);
	if (!error) {
		start = handle.cur_swap;
		error = swap_write_page(&handle, header);
	}
	if (!error)
		error = save_image(&handle, &snapshot, header->pages - 1);
	if (!error) {
		flush_swap_writer(&handle);
		printk("S");
		error = mark_swapfiles(swp_entry(root_swap, start));
		printk("|\n");
	}
	if (error)
		free_all_swap_pages(root_swap, handle.bitmap);
	release_swap_writer(&handle);
	return error;
}

/*
 *	Using bio to read from swap.
 *	This code requires a bit more work than just using buffer heads
 *	but, it is the recommended way for 2.5/2.6.
 *	The following are to signal the beginning and end of I/O. Bios
 *	finish asynchronously, while we want them to happen synchronously.
 *	A simple atomic_t, and a wait loop take care of this problem.
 */

static atomic_t io_done = ATOMIC_INIT(0);

static int end_io(struct bio *bio, unsigned int num, int err)
{
	if (!test_bit(BIO_UPTODATE, &bio->bi_flags))
		panic("I/O error reading memory image");
	atomic_set(&io_done, 0);
	return 0;
}

static struct block_device *resume_bdev;

/**
 *	submit - submit BIO request.
 *	@rw:	READ or WRITE.
 *	@off	physical offset of page.
 *	@page:	page we're reading or writing.
 *
 *	Straight from the textbook - allocate and initialize the bio.
 *	If we're writing, make sure the page is marked as dirty.
 *	Then submit it and wait.
 */

static int submit(int rw, pgoff_t page_off, void *page)
{
	int error = 0;
	struct bio *bio;

	bio = bio_alloc(GFP_ATOMIC, 1);
	if (!bio)
		return -ENOMEM;
	bio->bi_sector = page_off * (PAGE_SIZE >> 9);
	bio->bi_bdev = resume_bdev;
	bio->bi_end_io = end_io;

	if (bio_add_page(bio, virt_to_page(page), PAGE_SIZE, 0) < PAGE_SIZE) {
		printk("swsusp: ERROR: adding page to bio at %ld\n",page_off);
		error = -EFAULT;
		goto Done;
	}

	atomic_set(&io_done, 1);
	submit_bio(rw | (1 << BIO_RW_SYNC), bio);
	while (atomic_read(&io_done))
		yield();
	if (rw == READ)
		bio_set_pages_dirty(bio);
 Done:
	bio_put(bio);
	return error;
}

static int bio_read_page(pgoff_t page_off, void *page)
{
	return submit(READ, page_off, page);
}

static int bio_write_page(pgoff_t page_off, void *page)
{
	return submit(WRITE, page_off, page);
}

/**
 *	The following functions allow us to read data using a swap map
 *	in a file-alike way
 */

static void release_swap_reader(struct swap_map_handle *handle)
{
	if (handle->cur)
		free_page((unsigned long)handle->cur);
	handle->cur = NULL;
}

static int get_swap_reader(struct swap_map_handle *handle,
                                      swp_entry_t start)
{
	int error;

	if (!swp_offset(start))
		return -EINVAL;
	handle->cur = (struct swap_map_page *)get_zeroed_page(GFP_ATOMIC);
	if (!handle->cur)
		return -ENOMEM;
	error = bio_read_page(swp_offset(start), handle->cur);
	if (error) {
		release_swap_reader(handle);
		return error;
	}
	handle->k = 0;
	return 0;
}

static int swap_read_page(struct swap_map_handle *handle, void *buf)
{
	unsigned long offset;
	int error;

	if (!handle->cur)
		return -EINVAL;
	offset = handle->cur->entries[handle->k];
	if (!offset)
		return -EFAULT;
	error = bio_read_page(offset, buf);
	if (error)
		return error;
	if (++handle->k >= MAP_PAGE_ENTRIES) {
		handle->k = 0;
		offset = handle->cur->next_swap;
		if (!offset)
			release_swap_reader(handle);
		else
			error = bio_read_page(offset, handle->cur);
	}
	return error;
}

/**
 *	load_image - load the image using the swap map handle
 *	@handle and the snapshot handle @snapshot
 *	(assume there are @nr_pages pages to load)
 */

static int load_image(struct swap_map_handle *handle,
                      struct snapshot_handle *snapshot,
                      unsigned int nr_pages)
{
	unsigned int m;
	int ret;
	int error = 0;

	printk("Loading image data pages (%u pages) ...     ", nr_pages);
	m = nr_pages / 100;
	if (!m)
		m = 1;
	nr_pages = 0;
	do {
		ret = snapshot_write_next(snapshot, PAGE_SIZE);
		if (ret > 0) {
			error = swap_read_page(handle, data_of(*snapshot));
			if (error)
				break;
			if (!(nr_pages % m))
				printk("\b\b\b\b%3d%%", nr_pages / m);
			nr_pages++;
		}
	} while (ret > 0);
	if (!error)
		printk("\b\b\b\bdone\n");
	if (!snapshot_image_loaded(snapshot))
		error = -ENODATA;
	return error;
}

int swsusp_read(void)
{
	int error;
	struct swap_map_handle handle;
	struct snapshot_handle snapshot;
	struct swsusp_info *header;

	if (IS_ERR(resume_bdev)) {
		pr_debug("swsusp: block device not initialised\n");
		return PTR_ERR(resume_bdev);
	}

	memset(&snapshot, 0, sizeof(struct snapshot_handle));
	error = snapshot_write_next(&snapshot, PAGE_SIZE);
	if (error < PAGE_SIZE)
		return error < 0 ? error : -EFAULT;
	header = (struct swsusp_info *)data_of(snapshot);
	error = get_swap_reader(&handle, swsusp_header.image);
	if (!error)
		error = swap_read_page(&handle, header);
	if (!error)
		error = load_image(&handle, &snapshot, header->pages - 1);
	release_swap_reader(&handle);

	blkdev_put(resume_bdev);

	if (!error)
		pr_debug("swsusp: Reading resume file was successful\n");
	else
		pr_debug("swsusp: Error %d resuming\n", error);
	return error;
}

/**
 *      swsusp_check - Check for swsusp signature in the resume device
 */

int swsusp_check(void)
{
	int error;

	resume_bdev = open_by_devnum(swsusp_resume_device, FMODE_READ);
	if (!IS_ERR(resume_bdev)) {
		set_blocksize(resume_bdev, PAGE_SIZE);
		memset(&swsusp_header, 0, sizeof(swsusp_header));
		if ((error = bio_read_page(0, &swsusp_header)))
			return error;
		if (!memcmp(SWSUSP_SIG, swsusp_header.sig, 10)) {
			memcpy(swsusp_header.sig, swsusp_header.orig_sig, 10);
			/* Reset swap signature now */
			error = bio_write_page(0, &swsusp_header);
		} else {
			return -EINVAL;
		}
		if (error)
			blkdev_put(resume_bdev);
		else
			pr_debug("swsusp: Signature found, resuming\n");
	} else {
		error = PTR_ERR(resume_bdev);
	}

	if (error)
		pr_debug("swsusp: Error %d check for resume file\n", error);

	return error;
}

/**
 *	swsusp_close - close swap device.
 */

void swsusp_close(void)
{
	if (IS_ERR(resume_bdev)) {
		pr_debug("swsusp: block device not initialised\n");
		return;
	}

	blkdev_put(resume_bdev);
}
