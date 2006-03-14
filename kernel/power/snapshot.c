/*
 * linux/kernel/power/snapshot.c
 *
 * This file provide system snapshot/restore functionality.
 *
 * Copyright (C) 1998-2005 Pavel Machek <pavel@suse.cz>
 *
 * This file is released under the GPLv2, and is based on swsusp.c.
 *
 */


#include <linux/module.h>
#include <linux/mm.h>
#include <linux/suspend.h>
#include <linux/smp_lock.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/bootmem.h>
#include <linux/syscalls.h>
#include <linux/console.h>
#include <linux/highmem.h>

#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/io.h>

#include "power.h"

struct pbe *pagedir_nosave;
unsigned int nr_copy_pages;

#ifdef CONFIG_HIGHMEM
unsigned int count_highmem_pages(void)
{
	struct zone *zone;
	unsigned long zone_pfn;
	unsigned int n = 0;

	for_each_zone (zone)
		if (is_highmem(zone)) {
			mark_free_pages(zone);
			for (zone_pfn = 0; zone_pfn < zone->spanned_pages; zone_pfn++) {
				struct page *page;
				unsigned long pfn = zone_pfn + zone->zone_start_pfn;
				if (!pfn_valid(pfn))
					continue;
				page = pfn_to_page(pfn);
				if (PageReserved(page))
					continue;
				if (PageNosaveFree(page))
					continue;
				n++;
			}
		}
	return n;
}

struct highmem_page {
	char *data;
	struct page *page;
	struct highmem_page *next;
};

static struct highmem_page *highmem_copy;

static int save_highmem_zone(struct zone *zone)
{
	unsigned long zone_pfn;
	mark_free_pages(zone);
	for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn) {
		struct page *page;
		struct highmem_page *save;
		void *kaddr;
		unsigned long pfn = zone_pfn + zone->zone_start_pfn;

		if (!(pfn%1000))
			printk(".");
		if (!pfn_valid(pfn))
			continue;
		page = pfn_to_page(pfn);
		/*
		 * This condition results from rvmalloc() sans vmalloc_32()
		 * and architectural memory reservations. This should be
		 * corrected eventually when the cases giving rise to this
		 * are better understood.
		 */
		if (PageReserved(page))
			continue;
		BUG_ON(PageNosave(page));
		if (PageNosaveFree(page))
			continue;
		save = kmalloc(sizeof(struct highmem_page), GFP_ATOMIC);
		if (!save)
			return -ENOMEM;
		save->next = highmem_copy;
		save->page = page;
		save->data = (void *) get_zeroed_page(GFP_ATOMIC);
		if (!save->data) {
			kfree(save);
			return -ENOMEM;
		}
		kaddr = kmap_atomic(page, KM_USER0);
		memcpy(save->data, kaddr, PAGE_SIZE);
		kunmap_atomic(kaddr, KM_USER0);
		highmem_copy = save;
	}
	return 0;
}

int save_highmem(void)
{
	struct zone *zone;
	int res = 0;

	pr_debug("swsusp: Saving Highmem\n");
	for_each_zone (zone) {
		if (is_highmem(zone))
			res = save_highmem_zone(zone);
		if (res)
			return res;
	}
	return 0;
}

int restore_highmem(void)
{
	printk("swsusp: Restoring Highmem\n");
	while (highmem_copy) {
		struct highmem_page *save = highmem_copy;
		void *kaddr;
		highmem_copy = save->next;

		kaddr = kmap_atomic(save->page, KM_USER0);
		memcpy(kaddr, save->data, PAGE_SIZE);
		kunmap_atomic(kaddr, KM_USER0);
		free_page((long) save->data);
		kfree(save);
	}
	return 0;
}
#endif

static int pfn_is_nosave(unsigned long pfn)
{
	unsigned long nosave_begin_pfn = __pa(&__nosave_begin) >> PAGE_SHIFT;
	unsigned long nosave_end_pfn = PAGE_ALIGN(__pa(&__nosave_end)) >> PAGE_SHIFT;
	return (pfn >= nosave_begin_pfn) && (pfn < nosave_end_pfn);
}

/**
 *	saveable - Determine whether a page should be cloned or not.
 *	@pfn:	The page
 *
 *	We save a page if it's Reserved, and not in the range of pages
 *	statically defined as 'unsaveable', or if it isn't reserved, and
 *	isn't part of a free chunk of pages.
 */

static int saveable(struct zone *zone, unsigned long *zone_pfn)
{
	unsigned long pfn = *zone_pfn + zone->zone_start_pfn;
	struct page *page;

	if (!pfn_valid(pfn))
		return 0;

	page = pfn_to_page(pfn);
	BUG_ON(PageReserved(page) && PageNosave(page));
	if (PageNosave(page))
		return 0;
	if (PageReserved(page) && pfn_is_nosave(pfn))
		return 0;
	if (PageNosaveFree(page))
		return 0;

	return 1;
}

unsigned int count_data_pages(void)
{
	struct zone *zone;
	unsigned long zone_pfn;
	unsigned int n = 0;

	for_each_zone (zone) {
		if (is_highmem(zone))
			continue;
		mark_free_pages(zone);
		for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn)
			n += saveable(zone, &zone_pfn);
	}
	return n;
}

static void copy_data_pages(struct pbe *pblist)
{
	struct zone *zone;
	unsigned long zone_pfn;
	struct pbe *pbe, *p;

	pbe = pblist;
	for_each_zone (zone) {
		if (is_highmem(zone))
			continue;
		mark_free_pages(zone);
		/* This is necessary for swsusp_free() */
		for_each_pb_page (p, pblist)
			SetPageNosaveFree(virt_to_page(p));
		for_each_pbe (p, pblist)
			SetPageNosaveFree(virt_to_page(p->address));
		for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn) {
			if (saveable(zone, &zone_pfn)) {
				struct page *page;
				page = pfn_to_page(zone_pfn + zone->zone_start_pfn);
				BUG_ON(!pbe);
				pbe->orig_address = (unsigned long)page_address(page);
				/* copy_page is not usable for copying task structs. */
				memcpy((void *)pbe->address, (void *)pbe->orig_address, PAGE_SIZE);
				pbe = pbe->next;
			}
		}
	}
	BUG_ON(pbe);
}


/**
 *	free_pagedir - free pages allocated with alloc_pagedir()
 */

void free_pagedir(struct pbe *pblist)
{
	struct pbe *pbe;

	while (pblist) {
		pbe = (pblist + PB_PAGE_SKIP)->next;
		ClearPageNosave(virt_to_page(pblist));
		ClearPageNosaveFree(virt_to_page(pblist));
		free_page((unsigned long)pblist);
		pblist = pbe;
	}
}

/**
 *	fill_pb_page - Create a list of PBEs on a given memory page
 */

static inline void fill_pb_page(struct pbe *pbpage)
{
	struct pbe *p;

	p = pbpage;
	pbpage += PB_PAGE_SKIP;
	do
		p->next = p + 1;
	while (++p < pbpage);
}

/**
 *	create_pbe_list - Create a list of PBEs on top of a given chain
 *	of memory pages allocated with alloc_pagedir()
 */

static inline void create_pbe_list(struct pbe *pblist, unsigned int nr_pages)
{
	struct pbe *pbpage, *p;
	unsigned int num = PBES_PER_PAGE;

	for_each_pb_page (pbpage, pblist) {
		if (num >= nr_pages)
			break;

		fill_pb_page(pbpage);
		num += PBES_PER_PAGE;
	}
	if (pbpage) {
		for (num -= PBES_PER_PAGE - 1, p = pbpage; num < nr_pages; p++, num++)
			p->next = p + 1;
		p->next = NULL;
	}
}

/**
 *	On resume it is necessary to trace and eventually free the unsafe
 *	pages that have been allocated, because they are needed for I/O
 *	(on x86-64 we likely will "eat" these pages once again while
 *	creating the temporary page translation tables)
 */

struct eaten_page {
	struct eaten_page *next;
	char padding[PAGE_SIZE - sizeof(void *)];
};

static struct eaten_page *eaten_pages = NULL;

void release_eaten_pages(void)
{
	struct eaten_page *p, *q;

	p = eaten_pages;
	while (p) {
		q = p->next;
		/* We don't want swsusp_free() to free this page again */
		ClearPageNosave(virt_to_page(p));
		free_page((unsigned long)p);
		p = q;
	}
	eaten_pages = NULL;
}

/**
 *	@safe_needed - on resume, for storing the PBE list and the image,
 *	we can only use memory pages that do not conflict with the pages
 *	which had been used before suspend.
 *
 *	The unsafe pages are marked with the PG_nosave_free flag
 *
 *	Allocated but unusable (ie eaten) memory pages should be marked
 *	so that swsusp_free() can release them
 */

static inline void *alloc_image_page(gfp_t gfp_mask, int safe_needed)
{
	void *res;

	if (safe_needed)
		do {
			res = (void *)get_zeroed_page(gfp_mask);
			if (res && PageNosaveFree(virt_to_page(res))) {
				/* This is for swsusp_free() */
				SetPageNosave(virt_to_page(res));
				((struct eaten_page *)res)->next = eaten_pages;
				eaten_pages = res;
			}
		} while (res && PageNosaveFree(virt_to_page(res)));
	else
		res = (void *)get_zeroed_page(gfp_mask);
	if (res) {
		SetPageNosave(virt_to_page(res));
		SetPageNosaveFree(virt_to_page(res));
	}
	return res;
}

unsigned long get_safe_page(gfp_t gfp_mask)
{
	return (unsigned long)alloc_image_page(gfp_mask, 1);
}

/**
 *	alloc_pagedir - Allocate the page directory.
 *
 *	First, determine exactly how many pages we need and
 *	allocate them.
 *
 *	We arrange the pages in a chain: each page is an array of PBES_PER_PAGE
 *	struct pbe elements (pbes) and the last element in the page points
 *	to the next page.
 *
 *	On each page we set up a list of struct_pbe elements.
 */

struct pbe *alloc_pagedir(unsigned int nr_pages, gfp_t gfp_mask, int safe_needed)
{
	unsigned int num;
	struct pbe *pblist, *pbe;

	if (!nr_pages)
		return NULL;

	pr_debug("alloc_pagedir(): nr_pages = %d\n", nr_pages);
	pblist = alloc_image_page(gfp_mask, safe_needed);
	/* FIXME: rewrite this ugly loop */
	for (pbe = pblist, num = PBES_PER_PAGE; pbe && num < nr_pages;
        		pbe = pbe->next, num += PBES_PER_PAGE) {
		pbe += PB_PAGE_SKIP;
		pbe->next = alloc_image_page(gfp_mask, safe_needed);
	}
	if (!pbe) { /* get_zeroed_page() failed */
		free_pagedir(pblist);
		pblist = NULL;
        } else
        	create_pbe_list(pblist, nr_pages);
	return pblist;
}

/**
 * Free pages we allocated for suspend. Suspend pages are alocated
 * before atomic copy, so we need to free them after resume.
 */

void swsusp_free(void)
{
	struct zone *zone;
	unsigned long zone_pfn;

	for_each_zone(zone) {
		for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn)
			if (pfn_valid(zone_pfn + zone->zone_start_pfn)) {
				struct page *page;
				page = pfn_to_page(zone_pfn + zone->zone_start_pfn);
				if (PageNosave(page) && PageNosaveFree(page)) {
					ClearPageNosave(page);
					ClearPageNosaveFree(page);
					free_page((long) page_address(page));
				}
			}
	}
}


/**
 *	enough_free_mem - Make sure we enough free memory to snapshot.
 *
 *	Returns TRUE or FALSE after checking the number of available
 *	free pages.
 */

static int enough_free_mem(unsigned int nr_pages)
{
	struct zone *zone;
	unsigned int n = 0;

	for_each_zone (zone)
		if (!is_highmem(zone))
			n += zone->free_pages;
	pr_debug("swsusp: available memory: %u pages\n", n);
	return n > (nr_pages + PAGES_FOR_IO +
		(nr_pages + PBES_PER_PAGE - 1) / PBES_PER_PAGE);
}

int alloc_data_pages(struct pbe *pblist, gfp_t gfp_mask, int safe_needed)
{
	struct pbe *p;

	for_each_pbe (p, pblist) {
		p->address = (unsigned long)alloc_image_page(gfp_mask, safe_needed);
		if (!p->address)
			return -ENOMEM;
	}
	return 0;
}

static struct pbe *swsusp_alloc(unsigned int nr_pages)
{
	struct pbe *pblist;

	if (!(pblist = alloc_pagedir(nr_pages, GFP_ATOMIC | __GFP_COLD, 0))) {
		printk(KERN_ERR "suspend: Allocating pagedir failed.\n");
		return NULL;
	}

	if (alloc_data_pages(pblist, GFP_ATOMIC | __GFP_COLD, 0)) {
		printk(KERN_ERR "suspend: Allocating image pages failed.\n");
		swsusp_free();
		return NULL;
	}

	return pblist;
}

asmlinkage int swsusp_save(void)
{
	unsigned int nr_pages;

	pr_debug("swsusp: critical section: \n");

	drain_local_pages();
	nr_pages = count_data_pages();
	printk("swsusp: Need to copy %u pages\n", nr_pages);

	pr_debug("swsusp: pages needed: %u + %lu + %u, free: %u\n",
		 nr_pages,
		 (nr_pages + PBES_PER_PAGE - 1) / PBES_PER_PAGE,
		 PAGES_FOR_IO, nr_free_pages());

	if (!enough_free_mem(nr_pages)) {
		printk(KERN_ERR "swsusp: Not enough free memory\n");
		return -ENOMEM;
	}

	pagedir_nosave = swsusp_alloc(nr_pages);
	if (!pagedir_nosave)
		return -ENOMEM;

	/* During allocating of suspend pagedir, new cold pages may appear.
	 * Kill them.
	 */
	drain_local_pages();
	copy_data_pages(pagedir_nosave);

	/*
	 * End of critical section. From now on, we can write to memory,
	 * but we should not touch disk. This specially means we must _not_
	 * touch swap space! Except we must write out our image of course.
	 */

	nr_copy_pages = nr_pages;

	printk("swsusp: critical section/: done (%d pages copied)\n", nr_pages);
	return 0;
}
