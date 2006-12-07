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
 * Reworked the freeing of memory and the handling of swap
 *
 * More state savers are welcome. Especially for the scsi layer...
 *
 * For TODOs,FIXMEs also look in Documentation/power/swsusp.txt
 */

#include <linux/mm.h>
#include <linux/suspend.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/swap.h>
#include <linux/pm.h>
#include <linux/swapops.h>
#include <linux/bootmem.h>
#include <linux/syscalls.h>
#include <linux/highmem.h>
#include <linux/time.h>

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
int restore_highmem(void);
#else
static inline int restore_highmem(void) { return 0; }
static inline unsigned int count_highmem_pages(void) { return 0; }
#endif

/**
 *	The following functions are used for tracing the allocated
 *	swap pages, so that they can be freed in case of an error.
 *
 *	The functions operate on a linked bitmap structure defined
 *	in power.h
 */

void free_bitmap(struct bitmap_page *bitmap)
{
	struct bitmap_page *bp;

	while (bitmap) {
		bp = bitmap->next;
		free_page((unsigned long)bitmap);
		bitmap = bp;
	}
}

struct bitmap_page *alloc_bitmap(unsigned int nr_bits)
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

sector_t alloc_swapdev_block(int swap, struct bitmap_page *bitmap)
{
	unsigned long offset;

	offset = swp_offset(get_swap_page_of_type(swap));
	if (offset) {
		if (bitmap_set(bitmap, offset))
			swap_free(swp_entry(swap, offset));
		else
			return swapdev_block(swap, offset);
	}
	return 0;
}

void free_all_swap_pages(int swap, struct bitmap_page *bitmap)
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
 *	swsusp_show_speed - print the time elapsed between two events represented by
 *	@start and @stop
 *
 *	@nr_pages -	number of pages processed between @start and @stop
 *	@msg -		introductory message to print
 */

void swsusp_show_speed(struct timeval *start, struct timeval *stop,
			unsigned nr_pages, char *msg)
{
	s64 elapsed_centisecs64;
	int centisecs;
	int k;
	int kps;

	elapsed_centisecs64 = timeval_to_ns(stop) - timeval_to_ns(start);
	do_div(elapsed_centisecs64, NSEC_PER_SEC / 100);
	centisecs = elapsed_centisecs64;
	if (centisecs == 0)
		centisecs = 1;	/* avoid div-by-zero */
	k = nr_pages * (PAGE_SIZE / 1024);
	kps = (k * 100) / centisecs;
	printk("%s %d kbytes in %d.%02d seconds (%d.%02d MB/s)\n", msg, k,
			centisecs / 100, centisecs % 100,
			kps / 1000, (kps % 1000) / 10);
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
static inline unsigned long __shrink_memory(long tmp)
{
	if (tmp > SHRINK_BITE)
		tmp = SHRINK_BITE;
	return shrink_all_memory(tmp);
}

int swsusp_shrink_memory(void)
{
	long tmp;
	struct zone *zone;
	unsigned long pages = 0;
	unsigned int i = 0;
	char *p = "-\\|/";
	struct timeval start, stop;

	printk("Shrinking memory...  ");
	do_gettimeofday(&start);
	do {
		long size, highmem_size;

		highmem_size = count_highmem_pages();
		size = count_data_pages() + PAGES_FOR_IO;
		tmp = size;
		size += highmem_size;
		for_each_zone (zone)
			if (populated_zone(zone)) {
				if (is_highmem(zone)) {
					highmem_size -= zone->free_pages;
				} else {
					tmp -= zone->free_pages;
					tmp += zone->lowmem_reserve[ZONE_NORMAL];
					tmp += snapshot_additional_pages(zone);
				}
			}

		if (highmem_size < 0)
			highmem_size = 0;

		tmp += highmem_size;
		if (tmp > 0) {
			tmp = __shrink_memory(tmp);
			if (!tmp)
				return -ENOMEM;
			pages += tmp;
		} else if (size > image_size / PAGE_SIZE) {
			tmp = __shrink_memory(size - (image_size / PAGE_SIZE));
			pages += tmp;
		}
		printk("\b%c", p[i++%4]);
	} while (tmp > 0);
	do_gettimeofday(&stop);
	printk("\bdone (%lu pages freed)\n", pages);
	swsusp_show_speed(&start, &stop, pages, "Freed");

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

	save_processor_state();
	if ((error = swsusp_arch_suspend()))
		printk(KERN_ERR "Error %d suspending\n", error);
	/* Restore control flow magically appears here */
	restore_processor_state();
	/* NOTE:  device_power_up() is just a resume() for devices
	 * that suspended with irqs off ... no overall powerup.
	 */
	device_power_up();
Enable_irqs:
	local_irq_enable();
	return error;
}

int swsusp_resume(void)
{
	int error;

	local_irq_disable();
	/* NOTE:  device_power_down() is just a suspend() with irqs off;
	 * it has no special "power things down" semantics
	 */
	if (device_power_down(PMSG_PRETHAW))
		printk(KERN_ERR "Some devices failed to power down, very bad\n");
	/* We'll ignore saved state, but this gets preempt count (etc) right */
	save_processor_state();
	error = restore_highmem();
	if (!error) {
		error = swsusp_arch_resume();
		/* The code below is only ever reached in case of a failure.
		 * Otherwise execution continues at place where
		 * swsusp_arch_suspend() was called
        	 */
		BUG_ON(!error);
		/* This call to restore_highmem() undos the previous one */
		restore_highmem();
	}
	/* The only reason why swsusp_arch_resume() can fail is memory being
	 * very tight, so we have to free it as soon as we can to avoid
	 * subsequent failures
	 */
	swsusp_free();
	restore_processor_state();
	touch_softlockup_watchdog();
	device_power_up();
	local_irq_enable();
	return error;
}
