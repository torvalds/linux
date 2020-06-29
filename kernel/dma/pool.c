// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2020 Google LLC
 */
#include <linux/debugfs.h>
#include <linux/dma-direct.h>
#include <linux/dma-noncoherent.h>
#include <linux/dma-contiguous.h>
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
	if (IS_ERR_OR_NULL(root))
		return;

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

static int atomic_pool_expand(struct gen_pool *pool, size_t pool_size,
			      gfp_t gfp)
{
	unsigned int order;
	struct page *page;
	void *addr;
	int ret = -ENOMEM;

	/* Cannot allocate larger than MAX_ORDER-1 */
	order = min(get_order(pool_size), MAX_ORDER-1);

	do {
		pool_size = 1 << (PAGE_SHIFT + order);

		if (dev_get_cma_area(NULL))
			page = dma_alloc_from_contiguous(NULL, 1 << order,
							 order, false);
		else
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
	 * shrink so no re-encryption occurs in dma_direct_free_pages().
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
#endif
free_page: __maybe_unused
	if (!dma_release_from_contiguous(NULL, page, 1 << order))
		__free_pages(page, order);
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
	 * sizes to 128KB per 1GB of memory, min 128KB, max MAX_ORDER-1.
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
	if (IS_ENABLED(CONFIG_ZONE_DMA)) {
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
	if (gen_pool_avail(pool) < atomic_pool_size)
		schedule_work(&atomic_pool_work);

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
