// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014 The Linux Foundation
 */
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

struct page **dma_common_find_pages(void *cpu_addr)
{
	struct vm_struct *area = find_vm_area(cpu_addr);

	if (!area || area->flags != VM_DMA_COHERENT)
		return NULL;
	return area->pages;
}

static struct vm_struct *__dma_common_pages_remap(struct page **pages,
			size_t size, pgprot_t prot, const void *caller)
{
	struct vm_struct *area;

	area = get_vm_area_caller(size, VM_DMA_COHERENT, caller);
	if (!area)
		return NULL;

	if (map_vm_area(area, prot, pages)) {
		vunmap(area->addr);
		return NULL;
	}

	return area;
}

/*
 * Remaps an array of PAGE_SIZE pages into another vm_area.
 * Cannot be used in non-sleeping contexts
 */
void *dma_common_pages_remap(struct page **pages, size_t size,
			 pgprot_t prot, const void *caller)
{
	struct vm_struct *area;

	area = __dma_common_pages_remap(pages, size, prot, caller);
	if (!area)
		return NULL;

	area->pages = pages;

	return area->addr;
}

/*
 * Remaps an allocated contiguous region into another vm_area.
 * Cannot be used in non-sleeping contexts
 */
void *dma_common_contiguous_remap(struct page *page, size_t size,
			pgprot_t prot, const void *caller)
{
	int i;
	struct page **pages;
	struct vm_struct *area;

	pages = kmalloc(sizeof(struct page *) << get_order(size), GFP_KERNEL);
	if (!pages)
		return NULL;

	for (i = 0; i < (size >> PAGE_SHIFT); i++)
		pages[i] = nth_page(page, i);

	area = __dma_common_pages_remap(pages, size, prot, caller);

	kfree(pages);

	if (!area)
		return NULL;
	return area->addr;
}

/*
 * Unmaps a range previously mapped by dma_common_*_remap
 */
void dma_common_free_remap(void *cpu_addr, size_t size)
{
	struct vm_struct *area = find_vm_area(cpu_addr);

	if (!area || area->flags != VM_DMA_COHERENT) {
		WARN(1, "trying to free invalid coherent area: %p\n", cpu_addr);
		return;
	}

	unmap_kernel_range((unsigned long)cpu_addr, PAGE_ALIGN(size));
	vunmap(cpu_addr);
}
