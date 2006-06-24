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

#include "power.h"

/*
 * Preferred image size in bytes (tunable via /sys/power/image_size).
 * When it is set to N, swsusp will do its best to ensure the image
 * size will not exceed N bytes, but if that is impossible, it will
 * try to create the smallest image possible.
 */
unsigned long image_size = 500 * 1024 * 1024;

int in_suspend __nosavedata = 0;

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

unsigned long alloc_swap_page(int swap, struct bitmap_page *bitmap)
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
	long size, tmp;
	struct zone *zone;
	unsigned long pages = 0;
	unsigned int i = 0;
	char *p = "-\\|/";

	printk("Shrinking memory...  ");
	do {
		size = 2 * count_special_pages();
		size += size / 50 + count_data_pages();
		size += (size + PBES_PER_PAGE - 1) / PBES_PER_PAGE +
			PAGES_FOR_IO;
		tmp = size;
		for_each_zone (zone)
			if (!is_highmem(zone) && populated_zone(zone)) {
				tmp -= zone->free_pages;
				tmp += zone->lowmem_reserve[ZONE_NORMAL];
			}
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

	if ((error = save_special_mem())) {
		printk(KERN_ERR "swsusp: Not enough free pages for highmem\n");
		goto Restore_highmem;
	}

	save_processor_state();
	if ((error = swsusp_arch_suspend()))
		printk(KERN_ERR "Error %d suspending\n", error);
	/* Restore control flow magically appears here */
	restore_processor_state();
Restore_highmem:
	restore_special_mem();
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
	restore_special_mem();
	touch_softlockup_watchdog();
	device_power_up();
	local_irq_enable();
	return error;
}
