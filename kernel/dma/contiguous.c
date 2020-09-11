// SPDX-License-Identifier: GPL-2.0+
/*
 * Contiguous Memory Allocator for DMA mapping framework
 * Copyright (c) 2010-2011 by Samsung Electronics.
 * Written by:
 *	Marek Szyprowski <m.szyprowski@samsung.com>
 *	Michal Nazarewicz <mina86@mina86.com>
 *
 * Contiguous Memory Allocator
 *
 *   The Contiguous Memory Allocator (CMA) makes it possible to
 *   allocate big contiguous chunks of memory after the system has
 *   booted.
 *
 * Why is it needed?
 *
 *   Various devices on embedded systems have no scatter-getter and/or
 *   IO map support and require contiguous blocks of memory to
 *   operate.  They include devices such as cameras, hardware video
 *   coders, etc.
 *
 *   Such devices often require big memory buffers (a full HD frame
 *   is, for instance, more then 2 mega pixels large, i.e. more than 6
 *   MB of memory), which makes mechanisms such as kmalloc() or
 *   alloc_page() ineffective.
 *
 *   At the same time, a solution where a big memory region is
 *   reserved for a device is suboptimal since often more memory is
 *   reserved then strictly required and, moreover, the memory is
 *   inaccessible to page system even if device drivers don't use it.
 *
 *   CMA tries to solve this issue by operating on memory regions
 *   where only movable pages can be allocated from.  This way, kernel
 *   can use the memory for pagecache and when device driver requests
 *   it, allocated pages can be migrated.
 */

#define pr_fmt(fmt) "cma: " fmt

#ifdef CONFIG_CMA_DEBUG
#ifndef DEBUG
#  define DEBUG
#endif
#endif

#include <asm/page.h>
#include <asm/dma-contiguous.h>

#include <linux/memblock.h>
#include <linux/err.h>
#include <linux/sizes.h>
#include <linux/dma-map-ops.h>
#include <linux/cma.h>

#ifdef CONFIG_CMA_SIZE_MBYTES
#define CMA_SIZE_MBYTES CONFIG_CMA_SIZE_MBYTES
#else
#define CMA_SIZE_MBYTES 0
#endif

struct cma *dma_contiguous_default_area;

/*
 * Default global CMA area size can be defined in kernel's .config.
 * This is useful mainly for distro maintainers to create a kernel
 * that works correctly for most supported systems.
 * The size can be set in bytes or as a percentage of the total memory
 * in the system.
 *
 * Users, who want to set the size of global CMA area for their system
 * should use cma= kernel parameter.
 */
static const phys_addr_t size_bytes __initconst =
	(phys_addr_t)CMA_SIZE_MBYTES * SZ_1M;
static phys_addr_t  size_cmdline __initdata = -1;
static phys_addr_t base_cmdline __initdata;
static phys_addr_t limit_cmdline __initdata;

static int __init early_cma(char *p)
{
	if (!p) {
		pr_err("Config string not provided\n");
		return -EINVAL;
	}

	size_cmdline = memparse(p, &p);
	if (*p != '@')
		return 0;
	base_cmdline = memparse(p + 1, &p);
	if (*p != '-') {
		limit_cmdline = base_cmdline + size_cmdline;
		return 0;
	}
	limit_cmdline = memparse(p + 1, &p);

	return 0;
}
early_param("cma", early_cma);

#ifdef CONFIG_DMA_PERNUMA_CMA

static struct cma *dma_contiguous_pernuma_area[MAX_NUMNODES];
static phys_addr_t pernuma_size_bytes __initdata;

static int __init early_cma_pernuma(char *p)
{
	pernuma_size_bytes = memparse(p, &p);
	return 0;
}
early_param("cma_pernuma", early_cma_pernuma);
#endif

#ifdef CONFIG_CMA_SIZE_PERCENTAGE

static phys_addr_t __init __maybe_unused cma_early_percent_memory(void)
{
	struct memblock_region *reg;
	unsigned long total_pages = 0;

	/*
	 * We cannot use memblock_phys_mem_size() here, because
	 * memblock_analyze() has not been called yet.
	 */
	for_each_memblock(memory, reg)
		total_pages += memblock_region_memory_end_pfn(reg) -
			       memblock_region_memory_base_pfn(reg);

	return (total_pages * CONFIG_CMA_SIZE_PERCENTAGE / 100) << PAGE_SHIFT;
}

#else

static inline __maybe_unused phys_addr_t cma_early_percent_memory(void)
{
	return 0;
}

#endif

#ifdef CONFIG_DMA_PERNUMA_CMA
void __init dma_pernuma_cma_reserve(void)
{
	int nid;

	if (!pernuma_size_bytes)
		return;

	for_each_online_node(nid) {
		int ret;
		char name[CMA_MAX_NAME];
		struct cma **cma = &dma_contiguous_pernuma_area[nid];

		snprintf(name, sizeof(name), "pernuma%d", nid);
		ret = cma_declare_contiguous_nid(0, pernuma_size_bytes, 0, 0,
						 0, false, name, cma, nid);
		if (ret) {
			pr_warn("%s: reservation failed: err %d, node %d", __func__,
				ret, nid);
			continue;
		}

		pr_debug("%s: reserved %llu MiB on node %d\n", __func__,
			(unsigned long long)pernuma_size_bytes / SZ_1M, nid);
	}
}
#endif

/**
 * dma_contiguous_reserve() - reserve area(s) for contiguous memory handling
 * @limit: End address of the reserved memory (optional, 0 for any).
 *
 * This function reserves memory from early allocator. It should be
 * called by arch specific code once the early allocator (memblock or bootmem)
 * has been activated and all other subsystems have already allocated/reserved
 * memory.
 */
void __init dma_contiguous_reserve(phys_addr_t limit)
{
	phys_addr_t selected_size = 0;
	phys_addr_t selected_base = 0;
	phys_addr_t selected_limit = limit;
	bool fixed = false;

	pr_debug("%s(limit %08lx)\n", __func__, (unsigned long)limit);

	if (size_cmdline != -1) {
		selected_size = size_cmdline;
		selected_base = base_cmdline;
		selected_limit = min_not_zero(limit_cmdline, limit);
		if (base_cmdline + size_cmdline == limit_cmdline)
			fixed = true;
	} else {
#ifdef CONFIG_CMA_SIZE_SEL_MBYTES
		selected_size = size_bytes;
#elif defined(CONFIG_CMA_SIZE_SEL_PERCENTAGE)
		selected_size = cma_early_percent_memory();
#elif defined(CONFIG_CMA_SIZE_SEL_MIN)
		selected_size = min(size_bytes, cma_early_percent_memory());
#elif defined(CONFIG_CMA_SIZE_SEL_MAX)
		selected_size = max(size_bytes, cma_early_percent_memory());
#endif
	}

	if (selected_size && !dma_contiguous_default_area) {
		pr_debug("%s: reserving %ld MiB for global area\n", __func__,
			 (unsigned long)selected_size / SZ_1M);

		dma_contiguous_reserve_area(selected_size, selected_base,
					    selected_limit,
					    &dma_contiguous_default_area,
					    fixed);
	}
}

/**
 * dma_contiguous_reserve_area() - reserve custom contiguous area
 * @size: Size of the reserved area (in bytes),
 * @base: Base address of the reserved area optional, use 0 for any
 * @limit: End address of the reserved memory (optional, 0 for any).
 * @res_cma: Pointer to store the created cma region.
 * @fixed: hint about where to place the reserved area
 *
 * This function reserves memory from early allocator. It should be
 * called by arch specific code once the early allocator (memblock or bootmem)
 * has been activated and all other subsystems have already allocated/reserved
 * memory. This function allows to create custom reserved areas for specific
 * devices.
 *
 * If @fixed is true, reserve contiguous area at exactly @base.  If false,
 * reserve in range from @base to @limit.
 */
int __init dma_contiguous_reserve_area(phys_addr_t size, phys_addr_t base,
				       phys_addr_t limit, struct cma **res_cma,
				       bool fixed)
{
	int ret;

	ret = cma_declare_contiguous(base, size, limit, 0, 0, fixed,
					"reserved", res_cma);
	if (ret)
		return ret;

	/* Architecture specific contiguous memory fixup. */
	dma_contiguous_early_fixup(cma_get_base(*res_cma),
				cma_get_size(*res_cma));

	return 0;
}

/**
 * dma_alloc_from_contiguous() - allocate pages from contiguous area
 * @dev:   Pointer to device for which the allocation is performed.
 * @count: Requested number of pages.
 * @align: Requested alignment of pages (in PAGE_SIZE order).
 * @no_warn: Avoid printing message about failed allocation.
 *
 * This function allocates memory buffer for specified device. It uses
 * device specific contiguous memory area if available or the default
 * global one. Requires architecture specific dev_get_cma_area() helper
 * function.
 */
struct page *dma_alloc_from_contiguous(struct device *dev, size_t count,
				       unsigned int align, bool no_warn)
{
	if (align > CONFIG_CMA_ALIGNMENT)
		align = CONFIG_CMA_ALIGNMENT;

	return cma_alloc(dev_get_cma_area(dev), count, align, no_warn);
}

/**
 * dma_release_from_contiguous() - release allocated pages
 * @dev:   Pointer to device for which the pages were allocated.
 * @pages: Allocated pages.
 * @count: Number of allocated pages.
 *
 * This function releases memory allocated by dma_alloc_from_contiguous().
 * It returns false when provided pages do not belong to contiguous area and
 * true otherwise.
 */
bool dma_release_from_contiguous(struct device *dev, struct page *pages,
				 int count)
{
	return cma_release(dev_get_cma_area(dev), pages, count);
}

static struct page *cma_alloc_aligned(struct cma *cma, size_t size, gfp_t gfp)
{
	unsigned int align = min(get_order(size), CONFIG_CMA_ALIGNMENT);

	return cma_alloc(cma, size >> PAGE_SHIFT, align, gfp & __GFP_NOWARN);
}

/**
 * dma_alloc_contiguous() - allocate contiguous pages
 * @dev:   Pointer to device for which the allocation is performed.
 * @size:  Requested allocation size.
 * @gfp:   Allocation flags.
 *
 * tries to use device specific contiguous memory area if available, or it
 * tries to use per-numa cma, if the allocation fails, it will fallback to
 * try default global one.
 *
 * Note that it bypass one-page size of allocations from the per-numa and
 * global area as the addresses within one page are always contiguous, so
 * there is no need to waste CMA pages for that kind; it also helps reduce
 * fragmentations.
 */
struct page *dma_alloc_contiguous(struct device *dev, size_t size, gfp_t gfp)
{
#ifdef CONFIG_DMA_PERNUMA_CMA
	int nid = dev_to_node(dev);
#endif

	/* CMA can be used only in the context which permits sleeping */
	if (!gfpflags_allow_blocking(gfp))
		return NULL;
	if (dev->cma_area)
		return cma_alloc_aligned(dev->cma_area, size, gfp);
	if (size <= PAGE_SIZE)
		return NULL;

#ifdef CONFIG_DMA_PERNUMA_CMA
	if (nid != NUMA_NO_NODE && !(gfp & (GFP_DMA | GFP_DMA32))) {
		struct cma *cma = dma_contiguous_pernuma_area[nid];
		struct page *page;

		if (cma) {
			page = cma_alloc_aligned(cma, size, gfp);
			if (page)
				return page;
		}
	}
#endif
	if (!dma_contiguous_default_area)
		return NULL;

	return cma_alloc_aligned(dma_contiguous_default_area, size, gfp);
}

/**
 * dma_free_contiguous() - release allocated pages
 * @dev:   Pointer to device for which the pages were allocated.
 * @page:  Pointer to the allocated pages.
 * @size:  Size of allocated pages.
 *
 * This function releases memory allocated by dma_alloc_contiguous(). As the
 * cma_release returns false when provided pages do not belong to contiguous
 * area and true otherwise, this function then does a fallback __free_pages()
 * upon a false-return.
 */
void dma_free_contiguous(struct device *dev, struct page *page, size_t size)
{
	unsigned int count = PAGE_ALIGN(size) >> PAGE_SHIFT;

	/* if dev has its own cma, free page from there */
	if (dev->cma_area) {
		if (cma_release(dev->cma_area, page, count))
			return;
	} else {
		/*
		 * otherwise, page is from either per-numa cma or default cma
		 */
#ifdef CONFIG_DMA_PERNUMA_CMA
		if (cma_release(dma_contiguous_pernuma_area[page_to_nid(page)],
					page, count))
			return;
#endif
		if (cma_release(dma_contiguous_default_area, page, count))
			return;
	}

	/* not in any cma, free from buddy */
	__free_pages(page, get_order(size));
}

/*
 * Support for reserved memory regions defined in device tree
 */
#ifdef CONFIG_OF_RESERVED_MEM
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>

#undef pr_fmt
#define pr_fmt(fmt) fmt

static int rmem_cma_device_init(struct reserved_mem *rmem, struct device *dev)
{
	dev->cma_area = rmem->priv;
	return 0;
}

static void rmem_cma_device_release(struct reserved_mem *rmem,
				    struct device *dev)
{
	dev->cma_area = NULL;
}

static const struct reserved_mem_ops rmem_cma_ops = {
	.device_init	= rmem_cma_device_init,
	.device_release = rmem_cma_device_release,
};

static int __init rmem_cma_setup(struct reserved_mem *rmem)
{
	phys_addr_t align = PAGE_SIZE << max(MAX_ORDER - 1, pageblock_order);
	phys_addr_t mask = align - 1;
	unsigned long node = rmem->fdt_node;
	bool default_cma = of_get_flat_dt_prop(node, "linux,cma-default", NULL);
	struct cma *cma;
	int err;

	if (size_cmdline != -1 && default_cma) {
		pr_info("Reserved memory: bypass %s node, using cmdline CMA params instead\n",
			rmem->name);
		return -EBUSY;
	}

	if (!of_get_flat_dt_prop(node, "reusable", NULL) ||
	    of_get_flat_dt_prop(node, "no-map", NULL))
		return -EINVAL;

	if ((rmem->base & mask) || (rmem->size & mask)) {
		pr_err("Reserved memory: incorrect alignment of CMA region\n");
		return -EINVAL;
	}

	err = cma_init_reserved_mem(rmem->base, rmem->size, 0, rmem->name, &cma);
	if (err) {
		pr_err("Reserved memory: unable to setup CMA region\n");
		return err;
	}
	/* Architecture specific contiguous memory fixup. */
	dma_contiguous_early_fixup(rmem->base, rmem->size);

	if (default_cma)
		dma_contiguous_default_area = cma;

	rmem->ops = &rmem_cma_ops;
	rmem->priv = cma;

	pr_info("Reserved memory: created CMA memory pool at %pa, size %ld MiB\n",
		&rmem->base, (unsigned long)rmem->size / SZ_1M);

	return 0;
}
RESERVEDMEM_OF_DECLARE(cma, "shared-dma-pool", rmem_cma_setup);
#endif
