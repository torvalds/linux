// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (c) 2014 The Linux Foundation
 */
#include <linux/dma-direct.h>
#include <linux/dma-noncoherent.h>
#include <linux/dma-contiguous.h>
#include <linux/init.h>
#include <linux/genalloc.h>
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

#ifdef CONFIG_DMA_DIRECT_REMAP
static struct gen_pool *atomic_pool __ro_after_init;

#define DEFAULT_DMA_COHERENT_POOL_SIZE  SZ_256K
static size_t atomic_pool_size __initdata = DEFAULT_DMA_COHERENT_POOL_SIZE;

static int __init early_coherent_pool(char *p)
{
	atomic_pool_size = memparse(p, &p);
	return 0;
}
early_param("coherent_pool", early_coherent_pool);

static gfp_t dma_atomic_pool_gfp(void)
{
	if (IS_ENABLED(CONFIG_ZONE_DMA))
		return GFP_DMA;
	if (IS_ENABLED(CONFIG_ZONE_DMA32))
		return GFP_DMA32;
	return GFP_KERNEL;
}

static int __init dma_atomic_pool_init(void)
{
	unsigned int pool_size_order = get_order(atomic_pool_size);
	unsigned long nr_pages = atomic_pool_size >> PAGE_SHIFT;
	struct page *page;
	void *addr;
	int ret;

	if (dev_get_cma_area(NULL))
		page = dma_alloc_from_contiguous(NULL, nr_pages,
						 pool_size_order, false);
	else
		page = alloc_pages(dma_atomic_pool_gfp(), pool_size_order);
	if (!page)
		goto out;

	arch_dma_prep_coherent(page, atomic_pool_size);

	atomic_pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!atomic_pool)
		goto free_page;

	addr = dma_common_contiguous_remap(page, atomic_pool_size,
					   pgprot_dmacoherent(PAGE_KERNEL),
					   __builtin_return_address(0));
	if (!addr)
		goto destroy_genpool;

	ret = gen_pool_add_virt(atomic_pool, (unsigned long)addr,
				page_to_phys(page), atomic_pool_size, -1);
	if (ret)
		goto remove_mapping;
	gen_pool_set_algo(atomic_pool, gen_pool_first_fit_order_align, NULL);

	pr_info("DMA: preallocated %zu KiB pool for atomic allocations\n",
		atomic_pool_size / 1024);
	return 0;

remove_mapping:
	dma_common_free_remap(addr, atomic_pool_size);
destroy_genpool:
	gen_pool_destroy(atomic_pool);
	atomic_pool = NULL;
free_page:
	if (!dma_release_from_contiguous(NULL, page, nr_pages))
		__free_pages(page, pool_size_order);
out:
	pr_err("DMA: failed to allocate %zu KiB pool for atomic coherent allocation\n",
		atomic_pool_size / 1024);
	return -ENOMEM;
}
postcore_initcall(dma_atomic_pool_init);

bool dma_in_atomic_pool(void *start, size_t size)
{
	if (unlikely(!atomic_pool))
		return false;

	return gen_pool_has_addr(atomic_pool, (unsigned long)start, size);
}

void *dma_alloc_from_pool(size_t size, struct page **ret_page, gfp_t flags)
{
	unsigned long val;
	void *ptr = NULL;

	if (!atomic_pool) {
		WARN(1, "coherent pool not initialised!\n");
		return NULL;
	}

	val = gen_pool_alloc(atomic_pool, size);
	if (val) {
		phys_addr_t phys = gen_pool_virt_to_phys(atomic_pool, val);

		*ret_page = pfn_to_page(__phys_to_pfn(phys));
		ptr = (void *)val;
		memset(ptr, 0, size);
	}

	return ptr;
}

bool dma_free_from_pool(void *start, size_t size)
{
	if (!dma_in_atomic_pool(start, size))
		return false;
	gen_pool_free(atomic_pool, (unsigned long)start, size);
	return true;
}
#endif /* CONFIG_DMA_DIRECT_REMAP */
