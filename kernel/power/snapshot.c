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


#include <linux/version.h>
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
static unsigned int nr_copy_pages;
static unsigned int nr_meta_pages;
static unsigned long *buffer;

struct arch_saveable_page {
	unsigned long start;
	unsigned long end;
	char *data;
	struct arch_saveable_page *next;
};
static struct arch_saveable_page *arch_pages;

int swsusp_add_arch_pages(unsigned long start, unsigned long end)
{
	struct arch_saveable_page *tmp;

	while (start < end) {
		tmp = kzalloc(sizeof(struct arch_saveable_page), GFP_KERNEL);
		if (!tmp)
			return -ENOMEM;
		tmp->start = start;
		tmp->end = ((start >> PAGE_SHIFT) + 1) << PAGE_SHIFT;
		if (tmp->end > end)
			tmp->end = end;
		tmp->next = arch_pages;
		start = tmp->end;
		arch_pages = tmp;
	}
	return 0;
}

static unsigned int count_arch_pages(void)
{
	unsigned int count = 0;
	struct arch_saveable_page *tmp = arch_pages;
	while (tmp) {
		count++;
		tmp = tmp->next;
	}
	return count;
}

static int save_arch_mem(void)
{
	char *kaddr;
	struct arch_saveable_page *tmp = arch_pages;
	int offset;

	pr_debug("swsusp: Saving arch specific memory");
	while (tmp) {
		tmp->data = (char *)__get_free_page(GFP_ATOMIC);
		if (!tmp->data)
			return -ENOMEM;
		offset = tmp->start - (tmp->start & PAGE_MASK);
		/* arch pages might haven't a 'struct page' */
		kaddr = kmap_atomic_pfn(tmp->start >> PAGE_SHIFT, KM_USER0);
		memcpy(tmp->data + offset, kaddr + offset,
			tmp->end - tmp->start);
		kunmap_atomic(kaddr, KM_USER0);

		tmp = tmp->next;
	}
	return 0;
}

static int restore_arch_mem(void)
{
	char *kaddr;
	struct arch_saveable_page *tmp = arch_pages;
	int offset;

	while (tmp) {
		if (!tmp->data)
			continue;
		offset = tmp->start - (tmp->start & PAGE_MASK);
		kaddr = kmap_atomic_pfn(tmp->start >> PAGE_SHIFT, KM_USER0);
		memcpy(kaddr + offset, tmp->data + offset,
			tmp->end - tmp->start);
		kunmap_atomic(kaddr, KM_USER0);
		free_page((long)tmp->data);
		tmp->data = NULL;
		tmp = tmp->next;
	}
	return 0;
}

#ifdef CONFIG_HIGHMEM
static unsigned int count_highmem_pages(void)
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

		if (!(pfn%10000))
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

static int save_highmem(void)
{
	struct zone *zone;
	int res = 0;

	pr_debug("swsusp: Saving Highmem");
	drain_local_pages();
	for_each_zone (zone) {
		if (is_highmem(zone))
			res = save_highmem_zone(zone);
		if (res)
			return res;
	}
	printk("\n");
	return 0;
}

static int restore_highmem(void)
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
#else
static inline unsigned int count_highmem_pages(void) {return 0;}
static inline int save_highmem(void) {return 0;}
static inline int restore_highmem(void) {return 0;}
#endif

unsigned int count_special_pages(void)
{
	return count_arch_pages() + count_highmem_pages();
}

int save_special_mem(void)
{
	int ret;
	ret = save_arch_mem();
	if (!ret)
		ret = save_highmem();
	return ret;
}

int restore_special_mem(void)
{
	int ret;
	ret = restore_arch_mem();
	if (!ret)
		ret = restore_highmem();
	return ret;
}

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

static void free_pagedir(struct pbe *pblist, int clear_nosave_free)
{
	struct pbe *pbe;

	while (pblist) {
		pbe = (pblist + PB_PAGE_SKIP)->next;
		ClearPageNosave(virt_to_page(pblist));
		if (clear_nosave_free)
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

static unsigned int unsafe_pages;

/**
 *	@safe_needed - on resume, for storing the PBE list and the image,
 *	we can only use memory pages that do not conflict with the pages
 *	used before suspend.
 *
 *	The unsafe pages are marked with the PG_nosave_free flag
 *	and we count them using unsafe_pages
 */

static inline void *alloc_image_page(gfp_t gfp_mask, int safe_needed)
{
	void *res;

	res = (void *)get_zeroed_page(gfp_mask);
	if (safe_needed)
		while (res && PageNosaveFree(virt_to_page(res))) {
			/* The page is unsafe, mark it for swsusp_free() */
			SetPageNosave(virt_to_page(res));
			unsafe_pages++;
			res = (void *)get_zeroed_page(gfp_mask);
		}
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

static struct pbe *alloc_pagedir(unsigned int nr_pages, gfp_t gfp_mask,
				 int safe_needed)
{
	unsigned int num;
	struct pbe *pblist, *pbe;

	if (!nr_pages)
		return NULL;

	pblist = alloc_image_page(gfp_mask, safe_needed);
	/* FIXME: rewrite this ugly loop */
	for (pbe = pblist, num = PBES_PER_PAGE; pbe && num < nr_pages;
        		pbe = pbe->next, num += PBES_PER_PAGE) {
		pbe += PB_PAGE_SKIP;
		pbe->next = alloc_image_page(gfp_mask, safe_needed);
	}
	if (!pbe) { /* get_zeroed_page() failed */
		free_pagedir(pblist, 1);
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
	nr_copy_pages = 0;
	nr_meta_pages = 0;
	pagedir_nosave = NULL;
	buffer = NULL;
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

static int alloc_data_pages(struct pbe *pblist, gfp_t gfp_mask, int safe_needed)
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
	nr_meta_pages = (nr_pages * sizeof(long) + PAGE_SIZE - 1) >> PAGE_SHIFT;

	printk("swsusp: critical section/: done (%d pages copied)\n", nr_pages);
	return 0;
}

static void init_header(struct swsusp_info *info)
{
	memset(info, 0, sizeof(struct swsusp_info));
	info->version_code = LINUX_VERSION_CODE;
	info->num_physpages = num_physpages;
	memcpy(&info->uts, &system_utsname, sizeof(system_utsname));
	info->cpus = num_online_cpus();
	info->image_pages = nr_copy_pages;
	info->pages = nr_copy_pages + nr_meta_pages + 1;
	info->size = info->pages;
	info->size <<= PAGE_SHIFT;
}

/**
 *	pack_orig_addresses - the .orig_address fields of the PBEs from the
 *	list starting at @pbe are stored in the array @buf[] (1 page)
 */

static inline struct pbe *pack_orig_addresses(unsigned long *buf, struct pbe *pbe)
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
 *	snapshot_read_next - used for reading the system memory snapshot.
 *
 *	On the first call to it @handle should point to a zeroed
 *	snapshot_handle structure.  The structure gets updated and a pointer
 *	to it should be passed to this function every next time.
 *
 *	The @count parameter should contain the number of bytes the caller
 *	wants to read from the snapshot.  It must not be zero.
 *
 *	On success the function returns a positive number.  Then, the caller
 *	is allowed to read up to the returned number of bytes from the memory
 *	location computed by the data_of() macro.  The number returned
 *	may be smaller than @count, but this only happens if the read would
 *	cross a page boundary otherwise.
 *
 *	The function returns 0 to indicate the end of data stream condition,
 *	and a negative number is returned on error.  In such cases the
 *	structure pointed to by @handle is not updated and should not be used
 *	any more.
 */

int snapshot_read_next(struct snapshot_handle *handle, size_t count)
{
	if (handle->page > nr_meta_pages + nr_copy_pages)
		return 0;
	if (!buffer) {
		/* This makes the buffer be freed by swsusp_free() */
		buffer = alloc_image_page(GFP_ATOMIC, 0);
		if (!buffer)
			return -ENOMEM;
	}
	if (!handle->offset) {
		init_header((struct swsusp_info *)buffer);
		handle->buffer = buffer;
		handle->pbe = pagedir_nosave;
	}
	if (handle->prev < handle->page) {
		if (handle->page <= nr_meta_pages) {
			handle->pbe = pack_orig_addresses(buffer, handle->pbe);
			if (!handle->pbe)
				handle->pbe = pagedir_nosave;
		} else {
			handle->buffer = (void *)handle->pbe->address;
			handle->pbe = handle->pbe->next;
		}
		handle->prev = handle->page;
	}
	handle->buf_offset = handle->page_offset;
	if (handle->page_offset + count >= PAGE_SIZE) {
		count = PAGE_SIZE - handle->page_offset;
		handle->page_offset = 0;
		handle->page++;
	} else {
		handle->page_offset += count;
	}
	handle->offset += count;
	return count;
}

/**
 *	mark_unsafe_pages - mark the pages that cannot be used for storing
 *	the image during resume, because they conflict with the pages that
 *	had been used before suspend
 */

static int mark_unsafe_pages(struct pbe *pblist)
{
	struct zone *zone;
	unsigned long zone_pfn;
	struct pbe *p;

	if (!pblist) /* a sanity check */
		return -EINVAL;

	/* Clear page flags */
	for_each_zone (zone) {
		for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn)
			if (pfn_valid(zone_pfn + zone->zone_start_pfn))
				ClearPageNosaveFree(pfn_to_page(zone_pfn +
					zone->zone_start_pfn));
	}

	/* Mark orig addresses */
	for_each_pbe (p, pblist) {
		if (virt_addr_valid(p->orig_address))
			SetPageNosaveFree(virt_to_page(p->orig_address));
		else
			return -EFAULT;
	}

	unsafe_pages = 0;

	return 0;
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

static int check_header(struct swsusp_info *info)
{
	char *reason = NULL;

	if (info->version_code != LINUX_VERSION_CODE)
		reason = "kernel version";
	if (info->num_physpages != num_physpages)
		reason = "memory size";
	if (strcmp(info->uts.sysname,system_utsname.sysname))
		reason = "system type";
	if (strcmp(info->uts.release,system_utsname.release))
		reason = "kernel release";
	if (strcmp(info->uts.version,system_utsname.version))
		reason = "version";
	if (strcmp(info->uts.machine,system_utsname.machine))
		reason = "machine";
	if (reason) {
		printk(KERN_ERR "swsusp: Resume mismatch: %s\n", reason);
		return -EPERM;
	}
	return 0;
}

/**
 *	load header - check the image header and copy data from it
 */

static int load_header(struct snapshot_handle *handle,
                              struct swsusp_info *info)
{
	int error;
	struct pbe *pblist;

	error = check_header(info);
	if (!error) {
		pblist = alloc_pagedir(info->image_pages, GFP_ATOMIC, 0);
		if (!pblist)
			return -ENOMEM;
		pagedir_nosave = pblist;
		handle->pbe = pblist;
		nr_copy_pages = info->image_pages;
		nr_meta_pages = info->pages - info->image_pages - 1;
	}
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
 *	prepare_image - use metadata contained in the PBE list
 *	pointed to by pagedir_nosave to mark the pages that will
 *	be overwritten in the process of restoring the system
 *	memory state from the image ("unsafe" pages) and allocate
 *	memory for the image
 *
 *	The idea is to allocate the PBE list first and then
 *	allocate as many pages as it's needed for the image data,
 *	but not to assign these pages to the PBEs initially.
 *	Instead, we just mark them as allocated and create a list
 *	of "safe" which will be used later
 */

struct safe_page {
	struct safe_page *next;
	char padding[PAGE_SIZE - sizeof(void *)];
};

static struct safe_page *safe_pages;

static int prepare_image(struct snapshot_handle *handle)
{
	int error = 0;
	unsigned int nr_pages = nr_copy_pages;
	struct pbe *p, *pblist = NULL;

	p = pagedir_nosave;
	error = mark_unsafe_pages(p);
	if (!error) {
		pblist = alloc_pagedir(nr_pages, GFP_ATOMIC, 1);
		if (pblist)
			copy_page_backup_list(pblist, p);
		free_pagedir(p, 0);
		if (!pblist)
			error = -ENOMEM;
	}
	safe_pages = NULL;
	if (!error && nr_pages > unsafe_pages) {
		nr_pages -= unsafe_pages;
		while (nr_pages--) {
			struct safe_page *ptr;

			ptr = (struct safe_page *)get_zeroed_page(GFP_ATOMIC);
			if (!ptr) {
				error = -ENOMEM;
				break;
			}
			if (!PageNosaveFree(virt_to_page(ptr))) {
				/* The page is "safe", add it to the list */
				ptr->next = safe_pages;
				safe_pages = ptr;
			}
			/* Mark the page as allocated */
			SetPageNosave(virt_to_page(ptr));
			SetPageNosaveFree(virt_to_page(ptr));
		}
	}
	if (!error) {
		pagedir_nosave = pblist;
	} else {
		handle->pbe = NULL;
		swsusp_free();
	}
	return error;
}

static void *get_buffer(struct snapshot_handle *handle)
{
	struct pbe *pbe = handle->pbe, *last = handle->last_pbe;
	struct page *page = virt_to_page(pbe->orig_address);

	if (PageNosave(page) && PageNosaveFree(page)) {
		/*
		 * We have allocated the "original" page frame and we can
		 * use it directly to store the read page
		 */
		pbe->address = 0;
		if (last && last->next)
			last->next = NULL;
		return (void *)pbe->orig_address;
	}
	/*
	 * The "original" page frame has not been allocated and we have to
	 * use a "safe" page frame to store the read page
	 */
	pbe->address = (unsigned long)safe_pages;
	safe_pages = safe_pages->next;
	if (last)
		last->next = pbe;
	handle->last_pbe = pbe;
	return (void *)pbe->address;
}

/**
 *	snapshot_write_next - used for writing the system memory snapshot.
 *
 *	On the first call to it @handle should point to a zeroed
 *	snapshot_handle structure.  The structure gets updated and a pointer
 *	to it should be passed to this function every next time.
 *
 *	The @count parameter should contain the number of bytes the caller
 *	wants to write to the image.  It must not be zero.
 *
 *	On success the function returns a positive number.  Then, the caller
 *	is allowed to write up to the returned number of bytes to the memory
 *	location computed by the data_of() macro.  The number returned
 *	may be smaller than @count, but this only happens if the write would
 *	cross a page boundary otherwise.
 *
 *	The function returns 0 to indicate the "end of file" condition,
 *	and a negative number is returned on error.  In such cases the
 *	structure pointed to by @handle is not updated and should not be used
 *	any more.
 */

int snapshot_write_next(struct snapshot_handle *handle, size_t count)
{
	int error = 0;

	if (handle->prev && handle->page > nr_meta_pages + nr_copy_pages)
		return 0;
	if (!buffer) {
		/* This makes the buffer be freed by swsusp_free() */
		buffer = alloc_image_page(GFP_ATOMIC, 0);
		if (!buffer)
			return -ENOMEM;
	}
	if (!handle->offset)
		handle->buffer = buffer;
	if (handle->prev < handle->page) {
		if (!handle->prev) {
			error = load_header(handle, (struct swsusp_info *)buffer);
			if (error)
				return error;
		} else if (handle->prev <= nr_meta_pages) {
			handle->pbe = unpack_orig_addresses(buffer, handle->pbe);
			if (!handle->pbe) {
				error = prepare_image(handle);
				if (error)
					return error;
				handle->pbe = pagedir_nosave;
				handle->last_pbe = NULL;
				handle->buffer = get_buffer(handle);
			}
		} else {
			handle->pbe = handle->pbe->next;
			handle->buffer = get_buffer(handle);
		}
		handle->prev = handle->page;
	}
	handle->buf_offset = handle->page_offset;
	if (handle->page_offset + count >= PAGE_SIZE) {
		count = PAGE_SIZE - handle->page_offset;
		handle->page_offset = 0;
		handle->page++;
	} else {
		handle->page_offset += count;
	}
	handle->offset += count;
	return count;
}

int snapshot_image_loaded(struct snapshot_handle *handle)
{
	return !(!handle->pbe || handle->pbe->next || !nr_copy_pages ||
		handle->page <= nr_meta_pages + nr_copy_pages);
}
