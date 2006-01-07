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
 * Preferred image size in MB (tunable via /sys/power/image_size).
 * When it is set to N, swsusp will do its best to ensure the image
 * size will not exceed N MB, but if that is impossible, it will
 * try to create the smallest image possible.
 */
unsigned int image_size = 500;

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

static struct swsusp_info swsusp_info;

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

/*
 * Check whether the swap device is the specified resume
 * device, irrespective of whether they are specified by
 * identical names.
 *
 * (Thus, device inode aliasing is allowed.  You can say /dev/hda4
 * instead of /dev/ide/host0/bus0/target0/lun0/part4 [if using devfs]
 * and they'll be considered the same device.  This is *necessary* for
 * devfs, since the resume code can only recognize the form /dev/hda4,
 * but the suspend code would see the long name.)
 */
static inline int is_resume_device(const struct swap_info_struct *swap_info)
{
	struct file *file = swap_info->swap_file;
	struct inode *inode = file->f_dentry->d_inode;

	return S_ISBLK(inode->i_mode) &&
		swsusp_resume_device == MKDEV(imajor(inode), iminor(inode));
}

static int swsusp_swap_check(void) /* This is called before saving image */
{
	int i;

	if (!swsusp_resume_device)
		return -ENODEV;
	spin_lock(&swap_lock);
	for (i = 0; i < MAX_SWAPFILES; i++) {
		if (!(swap_info[i].flags & SWP_WRITEOK))
			continue;
		if (is_resume_device(swap_info + i)) {
			spin_unlock(&swap_lock);
			root_swap = i;
			return 0;
		}
	}
	spin_unlock(&swap_lock);
	return -ENODEV;
}

/**
 *	write_page - Write one page to a fresh swap location.
 *	@addr:	Address we're writing.
 *	@loc:	Place to store the entry we used.
 *
 *	Allocate a new swap entry and 'sync' it. Note we discard -EIO
 *	errors. That is an artifact left over from swsusp. It did not
 *	check the return of rw_swap_page_sync() at all, since most pages
 *	written back to swap would return -EIO.
 *	This is a partial improvement, since we will at least return other
 *	errors, though we need to eventually fix the damn code.
 */
static int write_page(unsigned long addr, swp_entry_t *loc)
{
	swp_entry_t entry;
	int error = -ENOSPC;

	entry = get_swap_page_of_type(root_swap);
	if (swp_offset(entry)) {
		error = rw_swap_page_sync(WRITE, entry, virt_to_page(addr));
		if (!error || error == -EIO)
			*loc = entry;
	}
	return error;
}

/**
 *	Swap map-handling functions
 *
 *	The swap map is a data structure used for keeping track of each page
 *	written to the swap.  It consists of many swap_map_page structures
 *	that contain each an array of MAP_PAGE_SIZE swap entries.
 *	These structures are linked together with the help of either the
 *	.next (in memory) or the .next_swap (in swap) member.
 *
 *	The swap map is created during suspend.  At that time we need to keep
 *	it in memory, because we have to free all of the allocated swap
 *	entries if an error occurs.  The memory needed is preallocated
 *	so that we know in advance if there's enough of it.
 *
 *	The first swap_map_page structure is filled with the swap entries that
 *	correspond to the first MAP_PAGE_SIZE data pages written to swap and
 *	so on.  After the all of the data pages have been written, the order
 *	of the swap_map_page structures in the map is reversed so that they
 *	can be read from swap in the original order.  This causes the data
 *	pages to be loaded in exactly the same order in which they have been
 *	saved.
 *
 *	During resume we only need to use one swap_map_page structure
 *	at a time, which means that we only need to use two memory pages for
 *	reading the image - one for reading the swap_map_page structures
 *	and the second for reading the data pages from swap.
 */

#define MAP_PAGE_SIZE	((PAGE_SIZE - sizeof(swp_entry_t) - sizeof(void *)) \
			/ sizeof(swp_entry_t))

struct swap_map_page {
	swp_entry_t		entries[MAP_PAGE_SIZE];
	swp_entry_t		next_swap;
	struct swap_map_page	*next;
};

static inline void free_swap_map(struct swap_map_page *swap_map)
{
	struct swap_map_page *swp;

	while (swap_map) {
		swp = swap_map->next;
		free_page((unsigned long)swap_map);
		swap_map = swp;
	}
}

static struct swap_map_page *alloc_swap_map(unsigned int nr_pages)
{
	struct swap_map_page *swap_map, *swp;
	unsigned n = 0;

	if (!nr_pages)
		return NULL;

	pr_debug("alloc_swap_map(): nr_pages = %d\n", nr_pages);
	swap_map = (struct swap_map_page *)get_zeroed_page(GFP_ATOMIC);
	swp = swap_map;
	for (n = MAP_PAGE_SIZE; n < nr_pages; n += MAP_PAGE_SIZE) {
		swp->next = (struct swap_map_page *)get_zeroed_page(GFP_ATOMIC);
		swp = swp->next;
		if (!swp) {
			free_swap_map(swap_map);
			return NULL;
		}
	}
	return swap_map;
}

/**
 *	reverse_swap_map - reverse the order of pages in the swap map
 *	@swap_map
 */

static inline struct swap_map_page *reverse_swap_map(struct swap_map_page *swap_map)
{
	struct swap_map_page *prev, *next;

	prev = NULL;
	while (swap_map) {
		next = swap_map->next;
		swap_map->next = prev;
		prev = swap_map;
		swap_map = next;
	}
	return prev;
}

/**
 *	free_swap_map_entries - free the swap entries allocated to store
 *	the swap map @swap_map (this is only called in case of an error)
 */
static inline void free_swap_map_entries(struct swap_map_page *swap_map)
{
	while (swap_map) {
		if (swap_map->next_swap.val)
			swap_free(swap_map->next_swap);
		swap_map = swap_map->next;
	}
}

/**
 *	save_swap_map - save the swap map used for tracing the data pages
 *	stored in the swap
 */

static int save_swap_map(struct swap_map_page *swap_map, swp_entry_t *start)
{
	swp_entry_t entry = (swp_entry_t){0};
	int error;

	while (swap_map) {
		swap_map->next_swap = entry;
		if ((error = write_page((unsigned long)swap_map, &entry)))
			return error;
		swap_map = swap_map->next;
	}
	*start = entry;
	return 0;
}

/**
 *	free_image_entries - free the swap entries allocated to store
 *	the image data pages (this is only called in case of an error)
 */

static inline void free_image_entries(struct swap_map_page *swp)
{
	unsigned k;

	while (swp) {
		for (k = 0; k < MAP_PAGE_SIZE; k++)
			if (swp->entries[k].val)
				swap_free(swp->entries[k]);
		swp = swp->next;
	}
}

/**
 *	The swap_map_handle structure is used for handling the swap map in
 *	a file-alike way
 */

struct swap_map_handle {
	struct swap_map_page *cur;
	unsigned int k;
};

static inline void init_swap_map_handle(struct swap_map_handle *handle,
                                        struct swap_map_page *map)
{
	handle->cur = map;
	handle->k = 0;
}

static inline int swap_map_write_page(struct swap_map_handle *handle,
                                      unsigned long addr)
{
	int error;

	error = write_page(addr, handle->cur->entries + handle->k);
	if (error)
		return error;
	if (++handle->k >= MAP_PAGE_SIZE) {
		handle->cur = handle->cur->next;
		handle->k = 0;
	}
	return 0;
}

/**
 *	save_image_data - save the data pages pointed to by the PBEs
 *	from the list @pblist using the swap map handle @handle
 *	(assume there are @nr_pages data pages to save)
 */

static int save_image_data(struct pbe *pblist,
                           struct swap_map_handle *handle,
                           unsigned int nr_pages)
{
	unsigned int m;
	struct pbe *p;
	int error = 0;

	printk("Saving image data pages (%u pages) ...     ", nr_pages);
	m = nr_pages / 100;
	if (!m)
		m = 1;
	nr_pages = 0;
	for_each_pbe (p, pblist) {
		error = swap_map_write_page(handle, p->address);
		if (error)
			break;
		if (!(nr_pages % m))
			printk("\b\b\b\b%3d%%", nr_pages / m);
		nr_pages++;
	}
	if (!error)
		printk("\b\b\b\bdone\n");
	return error;
}

static void dump_info(void)
{
	pr_debug(" swsusp: Version: %u\n",swsusp_info.version_code);
	pr_debug(" swsusp: Num Pages: %ld\n",swsusp_info.num_physpages);
	pr_debug(" swsusp: UTS Sys: %s\n",swsusp_info.uts.sysname);
	pr_debug(" swsusp: UTS Node: %s\n",swsusp_info.uts.nodename);
	pr_debug(" swsusp: UTS Release: %s\n",swsusp_info.uts.release);
	pr_debug(" swsusp: UTS Version: %s\n",swsusp_info.uts.version);
	pr_debug(" swsusp: UTS Machine: %s\n",swsusp_info.uts.machine);
	pr_debug(" swsusp: UTS Domain: %s\n",swsusp_info.uts.domainname);
	pr_debug(" swsusp: CPUs: %d\n",swsusp_info.cpus);
	pr_debug(" swsusp: Image: %ld Pages\n",swsusp_info.image_pages);
	pr_debug(" swsusp: Total: %ld Pages\n", swsusp_info.pages);
}

static void init_header(unsigned int nr_pages)
{
	memset(&swsusp_info, 0, sizeof(swsusp_info));
	swsusp_info.version_code = LINUX_VERSION_CODE;
	swsusp_info.num_physpages = num_physpages;
	memcpy(&swsusp_info.uts, &system_utsname, sizeof(system_utsname));

	swsusp_info.cpus = num_online_cpus();
	swsusp_info.image_pages = nr_pages;
	swsusp_info.pages = nr_pages +
		((nr_pages * sizeof(long) + PAGE_SIZE - 1) >> PAGE_SHIFT) + 1;
}

/**
 *	pack_orig_addresses - the .orig_address fields of the PBEs from the
 *	list starting at @pbe are stored in the array @buf[] (1 page)
 */

static inline struct pbe *pack_orig_addresses(unsigned long *buf,
                                              struct pbe *pbe)
{
	int j;

	for (j = 0; j < PAGE_SIZE / sizeof(long) && pbe; j++) {
		buf[j] = pbe->orig_address;
		pbe = pbe->next;
	}
	if (!pbe)
		for (; j < PAGE_SIZE / sizeof(long); j++)
			buf[j] = 0;
	return pbe;
}

/**
 *	save_image_metadata - save the .orig_address fields of the PBEs
 *	from the list @pblist using the swap map handle @handle
 */

static int save_image_metadata(struct pbe *pblist,
                               struct swap_map_handle *handle)
{
	unsigned long *buf;
	unsigned int n = 0;
	struct pbe *p;
	int error = 0;

	printk("Saving image metadata ... ");
	buf = (unsigned long *)get_zeroed_page(GFP_ATOMIC);
	if (!buf)
		return -ENOMEM;
	p = pblist;
	while (p) {
		p = pack_orig_addresses(buf, p);
		error = swap_map_write_page(handle, (unsigned long)buf);
		if (error)
			break;
		n++;
	}
	free_page((unsigned long)buf);
	if (!error)
		printk("done (%u pages saved)\n", n);
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
	unsigned int free_swap = swap_info[root_swap].pages -
		swap_info[root_swap].inuse_pages;

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

int swsusp_write(struct pbe *pblist, unsigned int nr_pages)
{
	struct swap_map_page *swap_map;
	struct swap_map_handle handle;
	swp_entry_t start;
	int error;

	if ((error = swsusp_swap_check())) {
		printk(KERN_ERR "swsusp: Cannot find swap device, try swapon -a.\n");
		return error;
	}
	if (!enough_swap(nr_pages)) {
		printk(KERN_ERR "swsusp: Not enough free swap\n");
		return -ENOSPC;
	}

	init_header(nr_pages);
	swap_map = alloc_swap_map(swsusp_info.pages);
	if (!swap_map)
		return -ENOMEM;
	init_swap_map_handle(&handle, swap_map);

	error = swap_map_write_page(&handle, (unsigned long)&swsusp_info);
	if (!error)
		error = save_image_metadata(pblist, &handle);
	if (!error)
		error = save_image_data(pblist, &handle, nr_pages);
	if (error)
		goto Free_image_entries;

	swap_map = reverse_swap_map(swap_map);
	error = save_swap_map(swap_map, &start);
	if (error)
		goto Free_map_entries;

	dump_info();
	printk( "S" );
	error = mark_swapfiles(start);
	printk( "|\n" );
	if (error)
		goto Free_map_entries;

Free_swap_map:
	free_swap_map(swap_map);
	return error;

Free_map_entries:
	free_swap_map_entries(swap_map);
Free_image_entries:
	free_image_entries(swap_map);
	goto Free_swap_map;
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
		} else if (size > (image_size * 1024 * 1024) / PAGE_SIZE) {
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

/**
 *	mark_unsafe_pages - mark the pages that cannot be used for storing
 *	the image during resume, because they conflict with the pages that
 *	had been used before suspend
 */

static void mark_unsafe_pages(struct pbe *pblist)
{
	struct zone *zone;
	unsigned long zone_pfn;
	struct pbe *p;

	if (!pblist) /* a sanity check */
		return;

	/* Clear page flags */
	for_each_zone (zone) {
		for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn)
			if (pfn_valid(zone_pfn + zone->zone_start_pfn))
				ClearPageNosaveFree(pfn_to_page(zone_pfn +
					zone->zone_start_pfn));
	}

	/* Mark orig addresses */
	for_each_pbe (p, pblist)
		SetPageNosaveFree(virt_to_page(p->orig_address));

}

static void copy_page_backup_list(struct pbe *dst, struct pbe *src)
{
	/* We assume both lists contain the same number of elements */
	while (src) {
		dst->orig_address = src->orig_address;
		dst = dst->next;
		src = src->next;
	}
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
	bio_get(bio);
	bio->bi_bdev = resume_bdev;
	bio->bi_end_io = end_io;

	if (bio_add_page(bio, virt_to_page(page), PAGE_SIZE, 0) < PAGE_SIZE) {
		printk("swsusp: ERROR: adding page to bio at %ld\n",page_off);
		error = -EFAULT;
		goto Done;
	}

	if (rw == WRITE)
		bio_set_pages_dirty(bio);

	atomic_set(&io_done, 1);
	submit_bio(rw | (1 << BIO_RW_SYNC), bio);
	while (atomic_read(&io_done))
		yield();

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

static inline void release_swap_map_reader(struct swap_map_handle *handle)
{
	if (handle->cur)
		free_page((unsigned long)handle->cur);
	handle->cur = NULL;
}

static inline int get_swap_map_reader(struct swap_map_handle *handle,
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
		release_swap_map_reader(handle);
		return error;
	}
	handle->k = 0;
	return 0;
}

static inline int swap_map_read_page(struct swap_map_handle *handle, void *buf)
{
	unsigned long offset;
	int error;

	if (!handle->cur)
		return -EINVAL;
	offset = swp_offset(handle->cur->entries[handle->k]);
	if (!offset)
		return -EINVAL;
	error = bio_read_page(offset, buf);
	if (error)
		return error;
	if (++handle->k >= MAP_PAGE_SIZE) {
		handle->k = 0;
		offset = swp_offset(handle->cur->next_swap);
		if (!offset)
			release_swap_map_reader(handle);
		else
			error = bio_read_page(offset, handle->cur);
	}
	return error;
}

static int check_header(void)
{
	char *reason = NULL;

	dump_info();
	if (swsusp_info.version_code != LINUX_VERSION_CODE)
		reason = "kernel version";
	if (swsusp_info.num_physpages != num_physpages)
		reason = "memory size";
	if (strcmp(swsusp_info.uts.sysname,system_utsname.sysname))
		reason = "system type";
	if (strcmp(swsusp_info.uts.release,system_utsname.release))
		reason = "kernel release";
	if (strcmp(swsusp_info.uts.version,system_utsname.version))
		reason = "version";
	if (strcmp(swsusp_info.uts.machine,system_utsname.machine))
		reason = "machine";
	if (reason) {
		printk(KERN_ERR "swsusp: Resume mismatch: %s\n", reason);
		return -EPERM;
	}
	return 0;
}

/**
 *	load_image_data - load the image data using the swap map handle
 *	@handle and store them using the page backup list @pblist
 *	(assume there are @nr_pages pages to load)
 */

static int load_image_data(struct pbe *pblist,
                           struct swap_map_handle *handle,
                           unsigned int nr_pages)
{
	int error;
	unsigned int m;
	struct pbe *p;

	if (!pblist)
		return -EINVAL;
	printk("Loading image data pages (%u pages) ...     ", nr_pages);
	m = nr_pages / 100;
	if (!m)
		m = 1;
	nr_pages = 0;
	p = pblist;
	while (p) {
		error = swap_map_read_page(handle, (void *)p->address);
		if (error)
			break;
		p = p->next;
		if (!(nr_pages % m))
			printk("\b\b\b\b%3d%%", nr_pages / m);
		nr_pages++;
	}
	if (!error)
		printk("\b\b\b\bdone\n");
	return error;
}

/**
 *	unpack_orig_addresses - copy the elements of @buf[] (1 page) to
 *	the PBEs in the list starting at @pbe
 */

static inline struct pbe *unpack_orig_addresses(unsigned long *buf,
                                                struct pbe *pbe)
{
	int j;

	for (j = 0; j < PAGE_SIZE / sizeof(long) && pbe; j++) {
		pbe->orig_address = buf[j];
		pbe = pbe->next;
	}
	return pbe;
}

/**
 *	load_image_metadata - load the image metadata using the swap map
 *	handle @handle and put them into the PBEs in the list @pblist
 */

static int load_image_metadata(struct pbe *pblist, struct swap_map_handle *handle)
{
	struct pbe *p;
	unsigned long *buf;
	unsigned int n = 0;
	int error = 0;

	printk("Loading image metadata ... ");
	buf = (unsigned long *)get_zeroed_page(GFP_ATOMIC);
	if (!buf)
		return -ENOMEM;
	p = pblist;
	while (p) {
		error = swap_map_read_page(handle, buf);
		if (error)
			break;
		p = unpack_orig_addresses(buf, p);
		n++;
	}
	free_page((unsigned long)buf);
	if (!error)
		printk("done (%u pages loaded)\n", n);
	return error;
}

int swsusp_read(struct pbe **pblist_ptr)
{
	int error;
	struct pbe *p, *pblist;
	struct swap_map_handle handle;
	unsigned int nr_pages;

	if (IS_ERR(resume_bdev)) {
		pr_debug("swsusp: block device not initialised\n");
		return PTR_ERR(resume_bdev);
	}

	error = get_swap_map_reader(&handle, swsusp_header.image);
	if (!error)
		error = swap_map_read_page(&handle, &swsusp_info);
	if (!error)
		error = check_header();
	if (error)
		return error;
	nr_pages = swsusp_info.image_pages;
	p = alloc_pagedir(nr_pages, GFP_ATOMIC, 0);
	if (!p)
		return -ENOMEM;
	error = load_image_metadata(p, &handle);
	if (!error) {
		mark_unsafe_pages(p);
		pblist = alloc_pagedir(nr_pages, GFP_ATOMIC, 1);
		if (pblist)
			copy_page_backup_list(pblist, p);
		free_pagedir(p);
		if (!pblist)
			error = -ENOMEM;

		/* Allocate memory for the image and read the data from swap */
		if (!error)
			error = alloc_data_pages(pblist, GFP_ATOMIC, 1);
		if (!error) {
			release_eaten_pages();
			error = load_image_data(pblist, &handle, nr_pages);
		}
		if (!error)
			*pblist_ptr = pblist;
	}
	release_swap_map_reader(&handle);

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
