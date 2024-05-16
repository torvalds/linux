// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2020 Google LLC
 */
#include <linux/cma.h>
#include <linux/debugfs.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-direct.h>
#include <linux/init.h>
#include <linux/genalloc.h>
#include <linux/set_memory.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

static struct gen_pool *atomic_pool_dma __ro_after_init;
static unsigned long pool_size_dma;
static struct gen_pool *atomic_pool_dma32 __ro_after_init;
static unsigned long pool_size_dma32;
static struct gen_pool *atomic_pool_kernel __ro_after_init;
static unsigned long pool_size_kernel;

/* Size can be defined by the coherent_pool command line */
static size_t atomic_pool_size;

/* Dynamic background expansion when the atomic pool is near capacity */
static struct work_struct atomic_pool_work;

static int __init early_coherent_pool(char *p)
{
	atomic_pool_size = memparse(p, &p);
	return 0;
}
early_param("coherent_pool", early_coherent_pool);

static void __init dma_atomic_pool_debugfs_init(void)
{
	struct dentry *root;

	root = debugfs_create_dir("dma_pools", NULL);
	debugfs_create_ulong("pool_size_dma", 0400, root, &pool_size_dma);
	debugfs_create_ulong("pool_size_dma32", 0400, root, &pool_size_dma32);
	debugfs_create_ulong("pool_size_kernel", 0400, root, &pool_size_kernel);
}

static void dma_atomic_pool_size_add(gfp_t gfp, size_t size)
{
	if (gfp & __GFP_DMA)
		pool_size_dma += size;
	else if (gfp & __GFP_DMA32)
		pool_size_dma32 += size;
	else
		pool_size_kernel += size;
}

static bool cma_in_zone(gfp_t gfp)
{
	unsigned long size;
	phys_addr_t end;
	struct cma *cma;

	cma = dev_get_cma_area(NULL);
	if (!cma)
		return false;

	size = cma_get_size(cma);
	if (!size)
		return false;

	/* CMA can't cross zone boundaries, see cma_activate_area() */
	end = cma_get_base(cma) + size - 1;
	if (IS_ENABLED(CONFIG_ZONE_DMA) && (gfp & GFP_DMA))
		return end <= DMA_BIT_MASK(zone_dma_bits);
	if (IS_ENABLED(CONFIG_ZONE_DMA32) && (gfp & GFP_DMA32))
		return end <= DMA_BIT_MASK(32);
	return true;
}

static int atomic_pool_expand(struct gen_pool *pool, size_t pool_size,
			      gfp_t gfp)
{
	unsigned int order;
	struct page *page = NULL;
	void *addr;
	int ret = -ENOMEM;

	/* Cannot allocate larger than MAX_PAGE_ORDER */
	order = min(get_order(pool_size), MAX_PAGE_ORDER);

	do {
		pool_size = 1 << (PAGE_SHIFT + order);
		if (cma_in_zone(gfp))
			page = dma_alloc_from_contiguous(NULL, 1 << order,
							 order, false);
		if (!page)
			page = alloc_pages(gfp, order);
	} while (!page && order-- > 0);
	if (!page)
		goto out;

	arch_dma_prep_coherent(page, pool_size);

#ifdef CONFIG_DMA_DIRECT_REMAP
	addr = dma_common_contiguous_remap(page, pool_size,
					   pgprot_dmacoherent(PAGE_KERNEL),
					   __builtin_return_address(0));
	if (!addr)
		goto free_page;
#else
	addr = page_to_virt(page);
#endif
	/*
	 * Memory in the atomic DMA pools must be unencrypted, the pools do not
	 * shrink so no re-encryption occurs in dma_direct_free().
	 */
	ret = set_memory_decrypted((unsigned long)page_to_virt(page),
				   1 << order);
	if (ret)
		goto remove_mapping;
	ret = gen_pool_add_virt(pool, (unsigned long)addr, page_to_phys(page),
				pool_size, NUMA_NO_NODE);
	if (ret)
		goto encrypt_mapping;

	dma_atomic_pool_size_add(gfp, pool_size);
	return 0;

encrypt_mapping:
	ret = set_memory_encrypted((unsigned long)page_to_virt(page),
				   1 << order);
	if (WARN_ON_ONCE(ret)) {
		/* Decrypt succeeded but encrypt failed, purposely leak */
		goto out;
	}
remove_mapping:
#ifdef CONFIG_DMA_DIRECT_REMAP
	dma_common_free_remap(addr, pool_size);
free_page:
	__free_pages(page, order);
#endif
out:
	return ret;
}

static void atomic_pool_resize(struct gen_pool *pool, gfp_t gfp)
{
	if (pool && gen_pool_avail(pool) < atomic_pool_size)
		atomic_pool_expand(pool, gen_pool_size(pool), gfp);
}

static void atomic_pool_work_fn(struct work_struct *work)
{
	if (IS_ENABLED(CONFIG_ZONE_DMA))
		atomic_pool_resize(atomic_pool_dma,
				   GFP_KERNEL | GFP_DMA);
	if (IS_ENABLED(CONFIG_ZONE_DMA32))
		atomic_pool_resize(atomic_pool_dma32,
				   GFP_KERNEL | GFP_DMA32);
	atomic_pool_resize(atomic_pool_kernel, GFP_KERNEL);
}

static __init struct gen_pool *__dma_atomic_pool_init(size_t pool_size,
						      gfp_t gfp)
{
	struct gen_pool *pool;
	int ret;

	pool = gen_pool_create(PAGE_SHIFT, NUMA_NO_NODE);
	if (!pool)
		return NULL;

	gen_pool_set_algo(pool, gen_pool_first_fit_order_align, NULL);

	ret = atomic_pool_expand(pool, pool_size, gfp);
	if (ret) {
		gen_pool_destroy(pool);
		pr_err("DMA: failed to allocate %zu KiB %pGg pool for atomic allocation\n",
		       pool_size >> 10, &gfp);
		return NULL;
	}

	pr_info("DMA: preallocated %zu KiB %pGg pool for atomic allocations\n",
		gen_pool_size(pool) >> 10, &gfp);
	return pool;
}

static int __init dma_atomic_pool_init(void)
{
	int ret = 0;

	/*
	 * If coherent_pool was not used on the command line, default the pool
	 * sizes to 128KB per 1GB of memory, min 128KB, max MAX_PAGE_ORDER.
	 */
	if (!atomic_pool_size) {
		unsigned long pages = totalram_pages() / (SZ_1G / SZ_128K);
		pages = min_t(unsigned long, pages, MAX_ORDER_NR_PAGES);
		atomic_pool_size = max_t(size_t, pages << PAGE_SHIFT, SZ_128K);
	}
	INIT_WORK(&atomic_pool_work, atomic_pool_work_fn);

	atomic_pool_kernel = __dma_atomic_pool_init(atomic_pool_size,
						    GFP_KERNEL);
	if (!atomic_pool_kernel)
		ret = -ENOMEM;
	if (has_managed_dma()) {
		atomic_pool_dma = __dma_atomic_pool_init(atomic_pool_size,
						GFP_KERNEL | GFP_DMA);
		if (!atomic_pool_dma)
			ret = -ENOMEM;
	}
	if (IS_ENABLED(CONFIG_ZONE_DMA32)) {
		atomic_pool_dma32 = __dma_atomic_pool_init(atomic_pool_size,
						GFP_KERNEL | GFP_DMA32);
		if (!atomic_pool_dma32)
			ret = -ENOMEM;
	}

	dma_atomic_pool_debugfs_init();
	return ret;
}
postcore_initcall(dma_atomic_pool_init);

static inline struct gen_pool *dma_guess_pool(struct gen_pool *prev, gfp_t gfp)
{
	if (prev == NULL) {
		if (IS_ENABLED(CONFIG_ZONE_DMA32) && (gfp & GFP_DMA32))
			return atomic_pool_dma32;
		if (atomic_pool_dma && (gfp & GFP_DMA))
			return atomic_pool_dma;
		return atomic_pool_kernel;
	}
	if (prev == atomic_pool_kernel)
		return atomic_pool_dma32 ? atomic_pool_dma32 : atomic_pool_dma;
	if (prev == atomic_pool_dma32)
		return atomic_pool_dma;
	return NULL;
}

static struct page *__dma_alloc_from_pool(struct device *dev, size_t size,
		struct gen_pool *pool, void **cpu_addr,
		bool (*phys_addr_ok)(struct device *, phys_addr_t, size_t))
{
	unsigned long addr;
	phys_addr_t phys;

	addr = gen_pool_alloc(pool, size);
	if (!addr)
		return NULL;

	phys = gen_pool_virt_to_phys(pool, addr);
	if (phys_addr_ok && !phys_addr_ok(dev, phys, size)) {
		gen_pool_free(pool, addr, size);
		return NULL;
	}

	if (gen_pool_avail(pool) < atomic_pool_size)
		schedule_work(&atomic_pool_work);

	*cpu_addr = (void *)addr;
	memset(*cpu_addr, 0, size);
	return pfn_to_page(__phys_to_pfn(phys));
}

struct page *dma_alloc_from_pool(struct device *dev, size_t size,
		void **cpu_addr, gfp_t gfp,
		bool (*phys_addr_ok)(struct device *, phys_addr_t, size_t))
{
	struct gen_pool *pool = NULL;
	struct page *page;

	while ((pool = dma_guess_pool(pool, gfp))) {
		page = __dma_alloc_from_pool(dev, size, pool, cpu_addr,
					     phys_addr_ok);
		if (page)
			return page;
	}

	WARN(1, "Failed to get suitable pool for %s\n", dev_name(dev));
	return NULL;
}

bool dma_free_from_pool(struct device *dev, void *start, size_t size)
{
	struct gen_pool *pool = NULL;

	while ((pool = dma_guess_pool(pool, 0))) {
		if (!gen_pool_has_addr(pool, (unsigned long)start, size))
			continue;
		gen_pool_free(pool, (unsigned long)start, size);
		return true;
	}

	return false;
}
