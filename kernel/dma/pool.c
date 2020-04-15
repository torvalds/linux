// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2020 Google LLC
 */
#include <linux/dma-direct.h>
#include <linux/dma-noncoherent.h>
#include <linux/dma-contiguous.h>
#include <linux/init.h>
#include <linux/genalloc.h>
#include <linux/slab.h>

static struct gen_pool *atomic_pool_dma __ro_after_init;
static struct gen_pool *atomic_pool_dma32 __ro_after_init;
static struct gen_pool *atomic_pool_kernel __ro_after_init;

#define DEFAULT_DMA_COHERENT_POOL_SIZE  SZ_256K
static size_t atomic_pool_size __initdata = DEFAULT_DMA_COHERENT_POOL_SIZE;

static int __init early_coherent_pool(char *p)
{
	atomic_pool_size = memparse(p, &p);
	return 0;
}
early_param("coherent_pool", early_coherent_pool);

static int __init __dma_atomic_pool_init(struct gen_pool **pool,
					 size_t pool_size, gfp_t gfp)
{
	const unsigned int order = get_order(pool_size);
	const unsigned long nr_pages = pool_size >> PAGE_SHIFT;
	struct page *page;
	void *addr;
	int ret;

	if (dev_get_cma_area(NULL))
		page = dma_alloc_from_contiguous(NULL, nr_pages, order, false);
	else
		page = alloc_pages(gfp, order);
	if (!page)
		goto out;

	arch_dma_prep_coherent(page, pool_size);

	*pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!*pool)
		goto free_page;

	addr = dma_common_contiguous_remap(page, pool_size,
					   pgprot_dmacoherent(PAGE_KERNEL),
					   __builtin_return_address(0));
	if (!addr)
		goto destroy_genpool;

	ret = gen_pool_add_virt(*pool, (unsigned long)addr, page_to_phys(page),
				pool_size, -1);
	if (ret)
		goto remove_mapping;
	gen_pool_set_algo(*pool, gen_pool_first_fit_order_align, NULL);

	pr_info("DMA: preallocated %zu KiB %pGg pool for atomic allocations\n",
		pool_size >> 10, &gfp);
	return 0;

remove_mapping:
	dma_common_free_remap(addr, pool_size);
destroy_genpool:
	gen_pool_destroy(*pool);
	*pool = NULL;
free_page:
	if (!dma_release_from_contiguous(NULL, page, nr_pages))
		__free_pages(page, order);
out:
	pr_err("DMA: failed to allocate %zu KiB %pGg pool for atomic allocation\n",
	       pool_size >> 10, &gfp);
	return -ENOMEM;
}

static int __init dma_atomic_pool_init(void)
{
	int ret = 0;
	int err;

	ret = __dma_atomic_pool_init(&atomic_pool_kernel, atomic_pool_size,
				     GFP_KERNEL);
	if (IS_ENABLED(CONFIG_ZONE_DMA)) {
		err = __dma_atomic_pool_init(&atomic_pool_dma,
					     atomic_pool_size, GFP_DMA);
		if (!ret && err)
			ret = err;
	}
	if (IS_ENABLED(CONFIG_ZONE_DMA32)) {
		err = __dma_atomic_pool_init(&atomic_pool_dma32,
					     atomic_pool_size, GFP_DMA32);
		if (!ret && err)
			ret = err;
	}
	return ret;
}
postcore_initcall(dma_atomic_pool_init);

static inline struct gen_pool *dev_to_pool(struct device *dev)
{
	u64 phys_mask;
	gfp_t gfp;

	gfp = dma_direct_optimal_gfp_mask(dev, dev->coherent_dma_mask,
					  &phys_mask);
	if (IS_ENABLED(CONFIG_ZONE_DMA) && gfp == GFP_DMA)
		return atomic_pool_dma;
	if (IS_ENABLED(CONFIG_ZONE_DMA32) && gfp == GFP_DMA32)
		return atomic_pool_dma32;
	return atomic_pool_kernel;
}

static bool dma_in_atomic_pool(struct device *dev, void *start, size_t size)
{
	struct gen_pool *pool = dev_to_pool(dev);

	if (unlikely(!pool))
		return false;
	return gen_pool_has_addr(pool, (unsigned long)start, size);
}

void *dma_alloc_from_pool(struct device *dev, size_t size,
			  struct page **ret_page, gfp_t flags)
{
	struct gen_pool *pool = dev_to_pool(dev);
	unsigned long val;
	void *ptr = NULL;

	if (!pool) {
		WARN(1, "%pGg atomic pool not initialised!\n", &flags);
		return NULL;
	}

	val = gen_pool_alloc(pool, size);
	if (val) {
		phys_addr_t phys = gen_pool_virt_to_phys(pool, val);

		*ret_page = pfn_to_page(__phys_to_pfn(phys));
		ptr = (void *)val;
		memset(ptr, 0, size);
	}

	return ptr;
}

bool dma_free_from_pool(struct device *dev, void *start, size_t size)
{
	struct gen_pool *pool = dev_to_pool(dev);

	if (!dma_in_atomic_pool(dev, start, size))
		return false;
	gen_pool_free(pool, (unsigned long)start, size);
	return true;
}
