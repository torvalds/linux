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
#include <linux/cc_platform.h>

struct dma_gen_pool {
	bool cc_shared;
	struct gen_pool *pool;
};

static struct dma_gen_pool atomic_pool_dma __ro_after_init;
static unsigned long pool_size_dma;
static struct dma_gen_pool atomic_pool_dma32 __ro_after_init;
static unsigned long pool_size_dma32;
static struct dma_gen_pool atomic_pool_kernel __ro_after_init;
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
		return end <= zone_dma_limit;
	if (IS_ENABLED(CONFIG_ZONE_DMA32) && (gfp & GFP_DMA32))
		return end <= max(DMA_BIT_MASK(32), zone_dma_limit);
	return true;
}

static int atomic_pool_expand(struct dma_gen_pool *dma_pool, size_t pool_size,
			      gfp_t gfp)
{
	unsigned int order;
	struct page *page = NULL;
	bool leak_pages = false;
	void *addr;
	int ret = -ENOMEM;
	pgprot_t prot __maybe_unused;

	/* Cannot allocate larger than MAX_PAGE_ORDER */
	order = min(get_order(pool_size), MAX_PAGE_ORDER);

	do {
		pool_size = 1 << (PAGE_SHIFT + order);
		if (cma_in_zone(gfp))
			page = dma_alloc_from_contiguous(NULL, 1 << order,
							 order, false);
		if (!page)
			page = alloc_pages(gfp | __GFP_NOWARN, order);
	} while (!page && order-- > 0);
	if (!page)
		goto out;

	arch_dma_prep_coherent(page, pool_size);

#ifdef CONFIG_DMA_DIRECT_REMAP
	if (dma_pool->cc_shared)
		prot = pgprot_decrypted(pgprot_dmacoherent(PAGE_KERNEL));
	else
		prot = pgprot_dmacoherent(PAGE_KERNEL);

	addr = dma_common_contiguous_remap(page, pool_size, prot,
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
	if (dma_pool->cc_shared) {
		ret = set_memory_decrypted((unsigned long)page_to_virt(page),
					   1 << order);
		if (ret) {
			leak_pages = true;
			goto remove_mapping;
		}
	}

	ret = gen_pool_add_virt(dma_pool->pool, (unsigned long)addr,
				page_to_phys(page), pool_size, NUMA_NO_NODE);
	if (ret)
		goto encrypt_mapping;

	dma_atomic_pool_size_add(gfp, pool_size);
	return 0;

encrypt_mapping:
	if (dma_pool->cc_shared &&
	    set_memory_encrypted((unsigned long)page_to_virt(page), 1 << order))
		leak_pages = true;

remove_mapping:
#ifdef CONFIG_DMA_DIRECT_REMAP
	dma_common_free_remap(addr, pool_size);
free_page:
#endif
	if (!leak_pages)
		__free_pages(page, order);
out:
	return ret;
}

static void atomic_pool_resize(struct dma_gen_pool *dma_pool, gfp_t gfp)
{
	if (dma_pool->pool && gen_pool_avail(dma_pool->pool) < atomic_pool_size)
		atomic_pool_expand(dma_pool, gen_pool_size(dma_pool->pool), gfp);
}

static void atomic_pool_work_fn(struct work_struct *work)
{
	if (IS_ENABLED(CONFIG_ZONE_DMA))
		atomic_pool_resize(&atomic_pool_dma,
				   GFP_KERNEL | GFP_DMA);
	if (IS_ENABLED(CONFIG_ZONE_DMA32))
		atomic_pool_resize(&atomic_pool_dma32,
				   GFP_KERNEL | GFP_DMA32);
	atomic_pool_resize(&atomic_pool_kernel, GFP_KERNEL);
}

static __init struct dma_gen_pool *__dma_atomic_pool_init(struct dma_gen_pool *dma_pool,
		size_t pool_size, gfp_t gfp)
{
	int ret;

	dma_pool->pool = gen_pool_create(PAGE_SHIFT, NUMA_NO_NODE);
	if (!dma_pool->pool)
		return NULL;

	gen_pool_set_algo(dma_pool->pool, gen_pool_first_fit_order_align, NULL);

	/* if platform is using memory encryption atomic pools are by default shared. */
	if (cc_platform_has(CC_ATTR_MEM_ENCRYPT))
		dma_pool->cc_shared = true;
	else
		dma_pool->cc_shared = false;

	ret = atomic_pool_expand(dma_pool, pool_size, gfp);
	if (ret) {
		gen_pool_destroy(dma_pool->pool);
		dma_pool->pool = NULL;
		pr_err("DMA: failed to allocate %zu KiB %pGg pool for atomic allocation\n",
		       pool_size >> 10, &gfp);
		return NULL;
	}

	pr_info("DMA: preallocated %zu KiB %pGg pool for atomic allocations\n",
		gen_pool_size(dma_pool->pool) >> 10, &gfp);
	return dma_pool;
}

#ifdef CONFIG_ZONE_DMA32
#define has_managed_dma32 has_managed_zone(ZONE_DMA32)
#else
#define has_managed_dma32 false
#endif

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

	/* All memory might be in the DMA zone(s) to begin with */
	if (has_managed_zone(ZONE_NORMAL)) {
		__dma_atomic_pool_init(&atomic_pool_kernel, atomic_pool_size, GFP_KERNEL);
		if (!atomic_pool_kernel.pool)
			ret = -ENOMEM;
	}

	if (has_managed_dma()) {
		__dma_atomic_pool_init(&atomic_pool_dma, atomic_pool_size,
				       GFP_KERNEL | GFP_DMA);
		if (!atomic_pool_dma.pool)
			ret = -ENOMEM;
	}

	if (has_managed_dma32) {
		__dma_atomic_pool_init(&atomic_pool_dma32, atomic_pool_size,
				       GFP_KERNEL | GFP_DMA32);
		if (!atomic_pool_dma32.pool)
			ret = -ENOMEM;
	}

	dma_atomic_pool_debugfs_init();
	return ret;
}
postcore_initcall(dma_atomic_pool_init);

static inline struct dma_gen_pool *__dma_guess_pool(struct dma_gen_pool *first,
		struct dma_gen_pool *second, struct dma_gen_pool *third)
{
	if (first->pool)
		return first;
	if (second && second->pool)
		return second;
	if (third && third->pool)
		return third;
	return NULL;
}

static inline struct dma_gen_pool *dma_guess_pool(struct dma_gen_pool *prev,
		gfp_t gfp)
{
	if (!prev) {
		if (gfp & GFP_DMA)
			return __dma_guess_pool(&atomic_pool_dma,
						&atomic_pool_dma32,
						&atomic_pool_kernel);

		if (gfp & GFP_DMA32)
			return __dma_guess_pool(&atomic_pool_dma32,
						&atomic_pool_dma,
						&atomic_pool_kernel);

		return __dma_guess_pool(&atomic_pool_kernel,
					&atomic_pool_dma32,
					&atomic_pool_dma);
	}

	if (prev == &atomic_pool_kernel)
		return __dma_guess_pool(&atomic_pool_dma32,
					&atomic_pool_dma, NULL);

	if (prev == &atomic_pool_dma32)
		return __dma_guess_pool(&atomic_pool_dma, NULL, NULL);

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
		void **cpu_addr, gfp_t gfp, unsigned long attrs,
		bool (*phys_addr_ok)(struct device *, phys_addr_t, size_t))
{
	struct dma_gen_pool *dma_pool = NULL;
	struct page *page;
	bool pool_found = false;

	while ((dma_pool = dma_guess_pool(dma_pool, gfp))) {

		if (dma_pool->cc_shared != !!(attrs & __DMA_ATTR_ALLOC_CC_SHARED))
			continue;

		pool_found = true;
		page = __dma_alloc_from_pool(dev, size, dma_pool->pool, cpu_addr,
					     phys_addr_ok);
		if (page)
			return page;
	}

	if (pool_found)
		WARN(!(gfp & __GFP_NOWARN), "DMA pool exhausted for %s\n", dev_name(dev));
	else
		WARN(1, "Failed to get suitable pool for %s\n", dev_name(dev));
	return NULL;
}

bool dma_free_from_pool(struct device *dev, void *start, size_t size)
{
	struct dma_gen_pool *dma_pool = NULL;

	while ((dma_pool = dma_guess_pool(dma_pool, 0))) {

		if (!gen_pool_has_addr(dma_pool->pool, (unsigned long)start, size))
			continue;

		gen_pool_free(dma_pool->pool, (unsigned long)start, size);
		return true;
	}

	return false;
}

struct dma_pool_phys_match {
	phys_addr_t phys;
	size_t size;
	unsigned long addr;
	bool found;
};

static void dma_pool_find_phys(struct gen_pool *pool, struct gen_pool_chunk *chunk,
			       void *data)
{
	struct dma_pool_phys_match *match = data;
	phys_addr_t end = match->phys + match->size - 1;
	phys_addr_t chunk_end;

	if (match->found)
		return;

	chunk_end = chunk->phys_addr + (chunk->end_addr - chunk->start_addr);
	if (match->phys < chunk->phys_addr || end > chunk_end)
		return;

	match->addr = chunk->start_addr + (match->phys - chunk->phys_addr);
	match->found = true;
}

static bool dma_free_from_pool_phys(struct dma_gen_pool *dma_pool, phys_addr_t phys,
				    size_t size)
{
	struct dma_pool_phys_match match = {
		.phys = phys,
		.size = size,
	};

	gen_pool_for_each_chunk(dma_pool->pool, dma_pool_find_phys, &match);
	if (!match.found)
		return false;

	gen_pool_free(dma_pool->pool, match.addr, size);
	return true;
}

/*
 * FIXME: We could avoid this by storing the remapped virtual address in
 * struct page and using that for lookup.
 */
bool dma_free_from_pool_page(struct device *dev, struct page *page, size_t size)
{
	struct dma_gen_pool *dma_pool = NULL;
	phys_addr_t phys = page_to_phys(page);

	if (!IS_ENABLED(CONFIG_DMA_DIRECT_REMAP))
		return dma_free_from_pool(dev, page_address(page), size);

	while ((dma_pool = dma_guess_pool(dma_pool, 0))) {
		if (dma_free_from_pool_phys(dma_pool, phys, size))
			return true;
	}

	return false;
}
