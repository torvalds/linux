/*
 * linux/kernel/power/swsusp.c
 *
 * This file provides code to write suspend image to swap and read it back.
 *
 * Copyright (C) 1998-2001 Gabor Kuti <seasons@fornax.hu>
 * Copyright (C) 1998,2001-2005 Pavel Machek <pavel@suse.cz>
 *
 * This file is released under the GPLv2.
 *
 * I'd like to thank the following people for their work:
 *
 * Pavel Machek <pavel@ucw.cz>:
 * Modifications, defectiveness pointing, being with me at the very beginning,
 * suspend to swap space, stop all tasks. Port to 2.4.18-ac and 2.5.17.
 *
 * Steve Doddi <dirk@loth.demon.co.uk>:
 * Support the possibility of hardware state restoring.
 *
 * Raph <grey.havens@earthling.net>:
 * Support for preserving states of network devices and virtual console
 * (including X and svgatextmode)
 *
 * Kurt Garloff <garloff@suse.de>:
 * Straightened the critical function in order to prevent compilers from
 * playing tricks with local variables.
 *
 * Andreas Mohr <a.mohr@mailto.de>
 *
 * Alex Badea <vampire@go.ro>:
 * Fixed runaway init
 *
 * Rafael J. Wysocki <rjw@sisk.pl>
 * Added the swap map data structure and reworked the handling of swap
 *
 * More state savers are welcome. Especially for the scsi layer...
 *
 * For TODOs,FIXMEs also look in Documentation/power/swsusp.txt
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/suspend.h>
#include <linux/smp_lock.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/swap.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/buffer_head.h>
#include <linux/swapops.h>
#include <linux/bootmem.h>
#include <linux/syscalls.h>
#include <linux/highmem.h>
#include <linux/bio.h>

#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/io.h>

#include "power.h"

/*
 * Preferred image size in bytes (tunable via /sys/power/image_size).
 * When it is set to N, swsusp will do its best to ensure the image
 * size will not exceed N bytes, but if that is impossible, it will
 * try to create the smallest image possible.
 */
unsigned long image_size = 500 * 1024 * 1024;

int in_suspend __nosavedata = 0;

#ifdef CONFIG_HIGHMEM
unsigned int count_highmem_pages(void);
int save_highmem(void);
int restore_highmem(void);
#else
static int save_highmem(void) { return 0; }
static int restore_highmem(void) { return 0; }
static unsigned int count_highmem_pages(void) { return 0; }
#endif

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
 *	The bitmap is used for tracing allocated swap pages
 *
 *	The entire bitmap consists of a number of bitmap_page
 *	structures linked with the help of the .next member.
 *	Thus each page can be allocated individually, so we only
 *	need to make 0-order memory allocations to create
 *	the bitmap.
 */

#define BITMAP_PAGE_SIZE	(PAGE_SIZE - sizeof(void *))
#define BITMAP_PAGE_CHUNKS	(BITMAP_PAGE_SIZE / sizeof(long))
#define BITS_PER_CHUNK		(sizeof(long) * 8)
#define BITMAP_PAGE_BITS	(BITMAP_PAGE_CHUNKS * BITS_PER_CHUNK)

struct bitmap_page {
	unsigned long		chunks[BITMAP_PAGE_CHUNKS];
	struct bitmap_page	*next;
};

/**
 *	The following functions are used for tracing the allocated
 *	swap pages, so that they can be freed in case of an error.
 *
 *	The functions operate on a linked bitmap structure defined
 *	above
 */

static void free_bitmap(struct bitmap_page *bitmap)
{
	struct bitmap_page *bp;

	while (bitmap) {
		bp = bitmap->next;
		free_page((unsigned long)bitmap);
		bitmap = bp;
	}
}

static struct bitmap_page *alloc_bitmap(unsigned int nr_bits)
{
	struct bitmap_page *bitmap, *bp;
	unsigned int n;

	if (!nr_bits)
		return NULL;

	bitmap = (struct bitmap_page *)get_zeroed_page(GFP_KERNEL);
	bp = bitmap;
	for (n = BITMAP_PAGE_BITS; n < nr_bits; n += BITMAP_PAGE_BITS) {
		bp->next = (struct bitmap_page *)get_zeroed_page(GFP_KERNEL);
		bp = bp->next;
		if (!bp) {
			free_bitmap(bitmap);
			return NULL;
		}
	}
	return bitmap;
}

static int bitmap_set(struct bitmap_page *bitmap, unsigned long bit)
{
	unsigned int n;

	n = BITMAP_PAGE_BITS;
	while (bitmap && n <= bit) {
		n += BITMAP_PAGE_BITS;
		bitmap = bitmap->next;
	}
	if (!bitmap)
		return -EINVAL;
	n -= BITMAP_PAGE_BITS;
	bit -= n;
	n = 0;
	while (bit >= BITS_PER_CHUNK) {
		bit -= BITS_PER_CHUNK;
		n++;
	}
	bitmap->chunks[n] |= (1UL << bit);
	return 0;
}

static unsigned long alloc_swap_page(int swap, struct bitmap_page *bitmap)
{
	unsigned long offset;

	offset = swp_offset(get_swap_page_of_type(swap));
	if (offset) {
		if (bitmap_set(bitmap, offset)) {
			swap_free(swp_entry(swap, offset));
			offset = 0;
		}
	}
	return offset;
}

static void free_all_swap_pages(int swap, struct bitmap_page *bitmap)
{
	unsigned int bit, n;
	unsigned long test;

	bit = 0;
	while (bitmap) {
		for (n = 0; n < BITMAP_PAGE_CHUNKS; n++)
			for (test = 1UL; test; test <<= 1) {
				if (bitmap->chunks[n] & test)
					swap_free(swp_entry(swap, bit));
				bit++;
			}
		bitmap = bitmap->next;
	}
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

/**
 *	swsusp_shrink_memory -  Try to free as much memory as needed
 *
 *	... but do not OOM-kill anyone
 *
 *	Notice: all userland should be stopped before it is called, or
 *	livelock is possible.
 */

#define SHRINK_BITE	10000

int swsusp_shrink_memory(void)
{
	long size, tmp;
	struct zone *zone;
	unsigned long pages = 0;
	unsigned int i = 0;
	char *p = "-\\|/";

	printk("Shrinking memory...  ");
	do {
		size = 2 * count_highmem_pages();
		size += size / 50 + count_data_pages();
		size += (size + PBES_PER_PAGE - 1) / PBES_PER_PAGE +
			PAGES_FOR_IO;
		tmp = size;
		for_each_zone (zone)
			if (!is_highmem(zone))
				tmp -= zone->free_pages;
		if (tmp > 0) {
			tmp = shrink_all_memory(SHRINK_BITE);
			if (!tmp)
				return -ENOMEM;
			pages += tmp;
		} else if (size > image_size / PAGE_SIZE) {
			tmp = shrink_all_memory(SHRINK_BITE);
			pages += tmp;
		}
		printk("\b%c", p[i++%4]);
	} while (tmp > 0);
	printk("\bdone (%lu pages freed)\n", pages);

	return 0;
}

int swsusp_suspend(void)
{
	int error;

	if ((error = arch_prepare_suspend()))
		return error;
	local_irq_disable();
	/* At this point, device_suspend() has been called, but *not*
	 * device_power_down(). We *must* device_power_down() now.
	 * Otherwise, drivers for some devices (e.g. interrupt controllers)
	 * become desynchronized with the actual state of the hardware
	 * at resume time, and evil weirdness ensues.
	 */
	if ((error = device_power_down(PMSG_FREEZE))) {
		printk(KERN_ERR "Some devices failed to power down, aborting suspend\n");
		goto Enable_irqs;
	}

	if ((error = save_highmem())) {
		printk(KERN_ERR "swsusp: Not enough free pages for highmem\n");
		goto Restore_highmem;
	}

	save_processor_state();
	if ((error = swsusp_arch_suspend()))
		printk(KERN_ERR "Error %d suspending\n", error);
	/* Restore control flow magically appears here */
	restore_processor_state();
Restore_highmem:
	restore_highmem();
	device_power_up();
Enable_irqs:
	local_irq_enable();
	return error;
}

int swsusp_resume(void)
{
	int error;
	local_irq_disable();
	if (device_power_down(PMSG_FREEZE))
		printk(KERN_ERR "Some devices failed to power down, very bad\n");
	/* We'll ignore saved state, but this gets preempt count (etc) right */
	save_processor_state();
	error = swsusp_arch_resume();
	/* Code below is only ever reached in case of failure. Otherwise
	 * execution continues at place where swsusp_arch_suspend was called
         */
	BUG_ON(!error);
	/* The only reason why swsusp_arch_resume() can fail is memory being
	 * very tight, so we have to free it as soon as we can to avoid
	 * subsequent failures
	 */
	swsusp_free();
	restore_processor_state();
	restore_highmem();
	touch_softlockup_watchdog();
	device_power_up();
	local_irq_enable();
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
	unsigned int nr_pages;

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
	if (!error) {
		nr_pages = header->image_pages;
		error = load_image(&handle, &snapshot, nr_pages);
	}
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
