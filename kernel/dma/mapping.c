// SPDX-License-Identifier: GPL-2.0
/*
 * arch-independent dma-mapping routines
 *
 * Copyright (c) 2006  SUSE Linux Products GmbH
 * Copyright (c) 2006  Tejun Heo <teheo@suse.de>
 */
#include <linux/memblock.h> /* for max_pfn */
#include <linux/acpi.h>
#include <linux/dma-map-ops.h>
#include <linux/export.h>
#include <linux/gfp.h>
#include <linux/kmsan.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "debug.h"
#include "direct.h"

#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE) || \
	defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) || \
	defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL)
bool dma_default_coherent = IS_ENABLED(CONFIG_ARCH_DMA_DEFAULT_COHERENT);
#endif

/*
 * Managed DMA API
 */
struct dma_devres {
	size_t		size;
	void		*vaddr;
	dma_addr_t	dma_handle;
	unsigned long	attrs;
};

static void dmam_release(struct device *dev, void *res)
{
	struct dma_devres *this = res;

	dma_free_attrs(dev, this->size, this->vaddr, this->dma_handle,
			this->attrs);
}

static int dmam_match(struct device *dev, void *res, void *match_data)
{
	struct dma_devres *this = res, *match = match_data;

	if (this->vaddr == match->vaddr) {
		WARN_ON(this->size != match->size ||
			this->dma_handle != match->dma_handle);
		return 1;
	}
	return 0;
}

/**
 * dmam_free_coherent - Managed dma_free_coherent()
 * @dev: Device to free coherent memory for
 * @size: Size of allocation
 * @vaddr: Virtual address of the memory to free
 * @dma_handle: DMA handle of the memory to free
 *
 * Managed dma_free_coherent().
 */
void dmam_free_coherent(struct device *dev, size_t size, void *vaddr,
			dma_addr_t dma_handle)
{
	struct dma_devres match_data = { size, vaddr, dma_handle };

	dma_free_coherent(dev, size, vaddr, dma_handle);
	WARN_ON(devres_destroy(dev, dmam_release, dmam_match, &match_data));
}
EXPORT_SYMBOL(dmam_free_coherent);

/**
 * dmam_alloc_attrs - Managed dma_alloc_attrs()
 * @dev: Device to allocate non_coherent memory for
 * @size: Size of allocation
 * @dma_handle: Out argument for allocated DMA handle
 * @gfp: Allocation flags
 * @attrs: Flags in the DMA_ATTR_* namespace.
 *
 * Managed dma_alloc_attrs().  Memory allocated using this function will be
 * automatically released on driver detach.
 *
 * RETURNS:
 * Pointer to allocated memory on success, NULL on failure.
 */
void *dmam_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs)
{
	struct dma_devres *dr;
	void *vaddr;

	dr = devres_alloc(dmam_release, sizeof(*dr), gfp);
	if (!dr)
		return NULL;

	vaddr = dma_alloc_attrs(dev, size, dma_handle, gfp, attrs);
	if (!vaddr) {
		devres_free(dr);
		return NULL;
	}

	dr->vaddr = vaddr;
	dr->dma_handle = *dma_handle;
	dr->size = size;
	dr->attrs = attrs;

	devres_add(dev, dr);

	return vaddr;
}
EXPORT_SYMBOL(dmam_alloc_attrs);

static bool dma_go_direct(struct device *dev, dma_addr_t mask,
		const struct dma_map_ops *ops)
{
	if (likely(!ops))
		return true;
#ifdef CONFIG_DMA_OPS_BYPASS
	if (dev->dma_ops_bypass)
		return min_not_zero(mask, dev->bus_dma_limit) >=
			    dma_direct_get_required_mask(dev);
#endif
	return false;
}


/*
 * Check if the devices uses a direct mapping for streaming DMA operations.
 * This allows IOMMU drivers to set a bypass mode if the DMA mask is large
 * enough.
 */
static inline bool dma_alloc_direct(struct device *dev,
		const struct dma_map_ops *ops)
{
	return dma_go_direct(dev, dev->coherent_dma_mask, ops);
}

static inline bool dma_map_direct(struct device *dev,
		const struct dma_map_ops *ops)
{
	return dma_go_direct(dev, *dev->dma_mask, ops);
}

dma_addr_t dma_map_page_attrs(struct device *dev, struct page *page,
		size_t offset, size_t size, enum dma_data_direction dir,
		unsigned long attrs)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);
	dma_addr_t addr;

	BUG_ON(!valid_dma_direction(dir));

	if (WARN_ON_ONCE(!dev->dma_mask))
		return DMA_MAPPING_ERROR;

	if (dma_map_direct(dev, ops) ||
	    arch_dma_map_page_direct(dev, page_to_phys(page) + offset + size))
		addr = dma_direct_map_page(dev, page, offset, size, dir, attrs);
	else
		addr = ops->map_page(dev, page, offset, size, dir, attrs);
	kmsan_handle_dma(page, offset, size, dir);
	debug_dma_map_page(dev, page, offset, size, dir, addr, attrs);

	return addr;
}
EXPORT_SYMBOL(dma_map_page_attrs);

void dma_unmap_page_attrs(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir, unsigned long attrs)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	BUG_ON(!valid_dma_direction(dir));
	if (dma_map_direct(dev, ops) ||
	    arch_dma_unmap_page_direct(dev, addr + size))
		dma_direct_unmap_page(dev, addr, size, dir, attrs);
	else if (ops->unmap_page)
		ops->unmap_page(dev, addr, size, dir, attrs);
	debug_dma_unmap_page(dev, addr, size, dir);
}
EXPORT_SYMBOL(dma_unmap_page_attrs);

static int __dma_map_sg_attrs(struct device *dev, struct scatterlist *sg,
	 int nents, enum dma_data_direction dir, unsigned long attrs)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);
	int ents;

	BUG_ON(!valid_dma_direction(dir));

	if (WARN_ON_ONCE(!dev->dma_mask))
		return 0;

	if (dma_map_direct(dev, ops) ||
	    arch_dma_map_sg_direct(dev, sg, nents))
		ents = dma_direct_map_sg(dev, sg, nents, dir, attrs);
	else
		ents = ops->map_sg(dev, sg, nents, dir, attrs);

	if (ents > 0) {
		kmsan_handle_dma_sg(sg, nents, dir);
		debug_dma_map_sg(dev, sg, nents, ents, dir, attrs);
	} else if (WARN_ON_ONCE(ents != -EINVAL && ents != -ENOMEM &&
				ents != -EIO && ents != -EREMOTEIO)) {
		return -EIO;
	}

	return ents;
}

/**
 * dma_map_sg_attrs - Map the given buffer for DMA
 * @dev:	The device for which to perform the DMA operation
 * @sg:		The sg_table object describing the buffer
 * @nents:	Number of entries to map
 * @dir:	DMA direction
 * @attrs:	Optional DMA attributes for the map operation
 *
 * Maps a buffer described by a scatterlist passed in the sg argument with
 * nents segments for the @dir DMA operation by the @dev device.
 *
 * Returns the number of mapped entries (which can be less than nents)
 * on success. Zero is returned for any error.
 *
 * dma_unmap_sg_attrs() should be used to unmap the buffer with the
 * original sg and original nents (not the value returned by this funciton).
 */
unsigned int dma_map_sg_attrs(struct device *dev, struct scatterlist *sg,
		    int nents, enum dma_data_direction dir, unsigned long attrs)
{
	int ret;

	ret = __dma_map_sg_attrs(dev, sg, nents, dir, attrs);
	if (ret < 0)
		return 0;
	return ret;
}
EXPORT_SYMBOL(dma_map_sg_attrs);

/**
 * dma_map_sgtable - Map the given buffer for DMA
 * @dev:	The device for which to perform the DMA operation
 * @sgt:	The sg_table object describing the buffer
 * @dir:	DMA direction
 * @attrs:	Optional DMA attributes for the map operation
 *
 * Maps a buffer described by a scatterlist stored in the given sg_table
 * object for the @dir DMA operation by the @dev device. After success, the
 * ownership for the buffer is transferred to the DMA domain.  One has to
 * call dma_sync_sgtable_for_cpu() or dma_unmap_sgtable() to move the
 * ownership of the buffer back to the CPU domain before touching the
 * buffer by the CPU.
 *
 * Returns 0 on success or a negative error code on error. The following
 * error codes are supported with the given meaning:
 *
 *   -EINVAL		An invalid argument, unaligned access or other error
 *			in usage. Will not succeed if retried.
 *   -ENOMEM		Insufficient resources (like memory or IOVA space) to
 *			complete the mapping. Should succeed if retried later.
 *   -EIO		Legacy error code with an unknown meaning. eg. this is
 *			returned if a lower level call returned
 *			DMA_MAPPING_ERROR.
 *   -EREMOTEIO		The DMA device cannot access P2PDMA memory specified
 *			in the sg_table. This will not succeed if retried.
 */
int dma_map_sgtable(struct device *dev, struct sg_table *sgt,
		    enum dma_data_direction dir, unsigned long attrs)
{
	int nents;

	nents = __dma_map_sg_attrs(dev, sgt->sgl, sgt->orig_nents, dir, attrs);
	if (nents < 0)
		return nents;
	sgt->nents = nents;
	return 0;
}
EXPORT_SYMBOL_GPL(dma_map_sgtable);

void dma_unmap_sg_attrs(struct device *dev, struct scatterlist *sg,
				      int nents, enum dma_data_direction dir,
				      unsigned long attrs)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	BUG_ON(!valid_dma_direction(dir));
	debug_dma_unmap_sg(dev, sg, nents, dir);
	if (dma_map_direct(dev, ops) ||
	    arch_dma_unmap_sg_direct(dev, sg, nents))
		dma_direct_unmap_sg(dev, sg, nents, dir, attrs);
	else if (ops->unmap_sg)
		ops->unmap_sg(dev, sg, nents, dir, attrs);
}
EXPORT_SYMBOL(dma_unmap_sg_attrs);

dma_addr_t dma_map_resource(struct device *dev, phys_addr_t phys_addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);
	dma_addr_t addr = DMA_MAPPING_ERROR;

	BUG_ON(!valid_dma_direction(dir));

	if (WARN_ON_ONCE(!dev->dma_mask))
		return DMA_MAPPING_ERROR;

	if (dma_map_direct(dev, ops))
		addr = dma_direct_map_resource(dev, phys_addr, size, dir, attrs);
	else if (ops->map_resource)
		addr = ops->map_resource(dev, phys_addr, size, dir, attrs);

	debug_dma_map_resource(dev, phys_addr, size, dir, addr, attrs);
	return addr;
}
EXPORT_SYMBOL(dma_map_resource);

void dma_unmap_resource(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir, unsigned long attrs)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	BUG_ON(!valid_dma_direction(dir));
	if (!dma_map_direct(dev, ops) && ops->unmap_resource)
		ops->unmap_resource(dev, addr, size, dir, attrs);
	debug_dma_unmap_resource(dev, addr, size, dir);
}
EXPORT_SYMBOL(dma_unmap_resource);

void dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	BUG_ON(!valid_dma_direction(dir));
	if (dma_map_direct(dev, ops))
		dma_direct_sync_single_for_cpu(dev, addr, size, dir);
	else if (ops->sync_single_for_cpu)
		ops->sync_single_for_cpu(dev, addr, size, dir);
	debug_dma_sync_single_for_cpu(dev, addr, size, dir);
}
EXPORT_SYMBOL(dma_sync_single_for_cpu);

void dma_sync_single_for_device(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	BUG_ON(!valid_dma_direction(dir));
	if (dma_map_direct(dev, ops))
		dma_direct_sync_single_for_device(dev, addr, size, dir);
	else if (ops->sync_single_for_device)
		ops->sync_single_for_device(dev, addr, size, dir);
	debug_dma_sync_single_for_device(dev, addr, size, dir);
}
EXPORT_SYMBOL(dma_sync_single_for_device);

void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
		    int nelems, enum dma_data_direction dir)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	BUG_ON(!valid_dma_direction(dir));
	if (dma_map_direct(dev, ops))
		dma_direct_sync_sg_for_cpu(dev, sg, nelems, dir);
	else if (ops->sync_sg_for_cpu)
		ops->sync_sg_for_cpu(dev, sg, nelems, dir);
	debug_dma_sync_sg_for_cpu(dev, sg, nelems, dir);
}
EXPORT_SYMBOL(dma_sync_sg_for_cpu);

void dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
		       int nelems, enum dma_data_direction dir)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	BUG_ON(!valid_dma_direction(dir));
	if (dma_map_direct(dev, ops))
		dma_direct_sync_sg_for_device(dev, sg, nelems, dir);
	else if (ops->sync_sg_for_device)
		ops->sync_sg_for_device(dev, sg, nelems, dir);
	debug_dma_sync_sg_for_device(dev, sg, nelems, dir);
}
EXPORT_SYMBOL(dma_sync_sg_for_device);

/*
 * The whole dma_get_sgtable() idea is fundamentally unsafe - it seems
 * that the intention is to allow exporting memory allocated via the
 * coherent DMA APIs through the dma_buf API, which only accepts a
 * scattertable.  This presents a couple of problems:
 * 1. Not all memory allocated via the coherent DMA APIs is backed by
 *    a struct page
 * 2. Passing coherent DMA memory into the streaming APIs is not allowed
 *    as we will try to flush the memory through a different alias to that
 *    actually being used (and the flushes are redundant.)
 */
int dma_get_sgtable_attrs(struct device *dev, struct sg_table *sgt,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	if (dma_alloc_direct(dev, ops))
		return dma_direct_get_sgtable(dev, sgt, cpu_addr, dma_addr,
				size, attrs);
	if (!ops->get_sgtable)
		return -ENXIO;
	return ops->get_sgtable(dev, sgt, cpu_addr, dma_addr, size, attrs);
}
EXPORT_SYMBOL(dma_get_sgtable_attrs);

#ifdef CONFIG_MMU
/*
 * Return the page attributes used for mapping dma_alloc_* memory, either in
 * kernel space if remapping is needed, or to userspace through dma_mmap_*.
 */
pgprot_t dma_pgprot(struct device *dev, pgprot_t prot, unsigned long attrs)
{
	if (dev_is_dma_coherent(dev))
		return prot;
#ifdef CONFIG_ARCH_HAS_DMA_WRITE_COMBINE
	if (attrs & DMA_ATTR_WRITE_COMBINE)
		return pgprot_writecombine(prot);
#endif
	return pgprot_dmacoherent(prot);
}
#endif /* CONFIG_MMU */

/**
 * dma_can_mmap - check if a given device supports dma_mmap_*
 * @dev: device to check
 *
 * Returns %true if @dev supports dma_mmap_coherent() and dma_mmap_attrs() to
 * map DMA allocations to userspace.
 */
bool dma_can_mmap(struct device *dev)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	if (dma_alloc_direct(dev, ops))
		return dma_direct_can_mmap(dev);
	return ops->mmap != NULL;
}
EXPORT_SYMBOL_GPL(dma_can_mmap);

/**
 * dma_mmap_attrs - map a coherent DMA allocation into user space
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @vma: vm_area_struct describing requested user mapping
 * @cpu_addr: kernel CPU-view address returned from dma_alloc_attrs
 * @dma_addr: device-view address returned from dma_alloc_attrs
 * @size: size of memory originally requested in dma_alloc_attrs
 * @attrs: attributes of mapping properties requested in dma_alloc_attrs
 *
 * Map a coherent DMA buffer previously allocated by dma_alloc_attrs into user
 * space.  The coherent DMA buffer must not be freed by the driver until the
 * user space mapping has been released.
 */
int dma_mmap_attrs(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	if (dma_alloc_direct(dev, ops))
		return dma_direct_mmap(dev, vma, cpu_addr, dma_addr, size,
				attrs);
	if (!ops->mmap)
		return -ENXIO;
	return ops->mmap(dev, vma, cpu_addr, dma_addr, size, attrs);
}
EXPORT_SYMBOL(dma_mmap_attrs);

u64 dma_get_required_mask(struct device *dev)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	if (dma_alloc_direct(dev, ops))
		return dma_direct_get_required_mask(dev);
	if (ops->get_required_mask)
		return ops->get_required_mask(dev);

	/*
	 * We require every DMA ops implementation to at least support a 32-bit
	 * DMA mask (and use bounce buffering if that isn't supported in
	 * hardware).  As the direct mapping code has its own routine to
	 * actually report an optimal mask we default to 32-bit here as that
	 * is the right thing for most IOMMUs, and at least not actively
	 * harmful in general.
	 */
	return DMA_BIT_MASK(32);
}
EXPORT_SYMBOL_GPL(dma_get_required_mask);

void *dma_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t flag, unsigned long attrs)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);
	void *cpu_addr;

	WARN_ON_ONCE(!dev->coherent_dma_mask);

	/*
	 * DMA allocations can never be turned back into a page pointer, so
	 * requesting compound pages doesn't make sense (and can't even be
	 * supported at all by various backends).
	 */
	if (WARN_ON_ONCE(flag & __GFP_COMP))
		return NULL;

	if (dma_alloc_from_dev_coherent(dev, size, dma_handle, &cpu_addr))
		return cpu_addr;

	/* let the implementation decide on the zone to allocate from: */
	flag &= ~(__GFP_DMA | __GFP_DMA32 | __GFP_HIGHMEM);

	if (dma_alloc_direct(dev, ops))
		cpu_addr = dma_direct_alloc(dev, size, dma_handle, flag, attrs);
	else if (ops->alloc)
		cpu_addr = ops->alloc(dev, size, dma_handle, flag, attrs);
	else
		return NULL;

	debug_dma_alloc_coherent(dev, size, *dma_handle, cpu_addr, attrs);
	return cpu_addr;
}
EXPORT_SYMBOL(dma_alloc_attrs);

void dma_free_attrs(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_handle, unsigned long attrs)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	if (dma_release_from_dev_coherent(dev, get_order(size), cpu_addr))
		return;
	/*
	 * On non-coherent platforms which implement DMA-coherent buffers via
	 * non-cacheable remaps, ops->free() may call vunmap(). Thus getting
	 * this far in IRQ context is a) at risk of a BUG_ON() or trying to
	 * sleep on some machines, and b) an indication that the driver is
	 * probably misusing the coherent API anyway.
	 */
	WARN_ON(irqs_disabled());

	if (!cpu_addr)
		return;

	debug_dma_free_coherent(dev, size, cpu_addr, dma_handle);
	if (dma_alloc_direct(dev, ops))
		dma_direct_free(dev, size, cpu_addr, dma_handle, attrs);
	else if (ops->free)
		ops->free(dev, size, cpu_addr, dma_handle, attrs);
}
EXPORT_SYMBOL(dma_free_attrs);

static struct page *__dma_alloc_pages(struct device *dev, size_t size,
		dma_addr_t *dma_handle, enum dma_data_direction dir, gfp_t gfp)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	if (WARN_ON_ONCE(!dev->coherent_dma_mask))
		return NULL;
	if (WARN_ON_ONCE(gfp & (__GFP_DMA | __GFP_DMA32 | __GFP_HIGHMEM)))
		return NULL;
	if (WARN_ON_ONCE(gfp & __GFP_COMP))
		return NULL;

	size = PAGE_ALIGN(size);
	if (dma_alloc_direct(dev, ops))
		return dma_direct_alloc_pages(dev, size, dma_handle, dir, gfp);
	if (!ops->alloc_pages)
		return NULL;
	return ops->alloc_pages(dev, size, dma_handle, dir, gfp);
}

struct page *dma_alloc_pages(struct device *dev, size_t size,
		dma_addr_t *dma_handle, enum dma_data_direction dir, gfp_t gfp)
{
	struct page *page = __dma_alloc_pages(dev, size, dma_handle, dir, gfp);

	if (page)
		debug_dma_map_page(dev, page, 0, size, dir, *dma_handle, 0);
	return page;
}
EXPORT_SYMBOL_GPL(dma_alloc_pages);

static void __dma_free_pages(struct device *dev, size_t size, struct page *page,
		dma_addr_t dma_handle, enum dma_data_direction dir)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	size = PAGE_ALIGN(size);
	if (dma_alloc_direct(dev, ops))
		dma_direct_free_pages(dev, size, page, dma_handle, dir);
	else if (ops->free_pages)
		ops->free_pages(dev, size, page, dma_handle, dir);
}

void dma_free_pages(struct device *dev, size_t size, struct page *page,
		dma_addr_t dma_handle, enum dma_data_direction dir)
{
	debug_dma_unmap_page(dev, dma_handle, size, dir);
	__dma_free_pages(dev, size, page, dma_handle, dir);
}
EXPORT_SYMBOL_GPL(dma_free_pages);

int dma_mmap_pages(struct device *dev, struct vm_area_struct *vma,
		size_t size, struct page *page)
{
	unsigned long count = PAGE_ALIGN(size) >> PAGE_SHIFT;

	if (vma->vm_pgoff >= count || vma_pages(vma) > count - vma->vm_pgoff)
		return -ENXIO;
	return remap_pfn_range(vma, vma->vm_start,
			       page_to_pfn(page) + vma->vm_pgoff,
			       vma_pages(vma) << PAGE_SHIFT, vma->vm_page_prot);
}
EXPORT_SYMBOL_GPL(dma_mmap_pages);

static struct sg_table *alloc_single_sgt(struct device *dev, size_t size,
		enum dma_data_direction dir, gfp_t gfp)
{
	struct sg_table *sgt;
	struct page *page;

	sgt = kmalloc(sizeof(*sgt), gfp);
	if (!sgt)
		return NULL;
	if (sg_alloc_table(sgt, 1, gfp))
		goto out_free_sgt;
	page = __dma_alloc_pages(dev, size, &sgt->sgl->dma_address, dir, gfp);
	if (!page)
		goto out_free_table;
	sg_set_page(sgt->sgl, page, PAGE_ALIGN(size), 0);
	sg_dma_len(sgt->sgl) = sgt->sgl->length;
	return sgt;
out_free_table:
	sg_free_table(sgt);
out_free_sgt:
	kfree(sgt);
	return NULL;
}

struct sg_table *dma_alloc_noncontiguous(struct device *dev, size_t size,
		enum dma_data_direction dir, gfp_t gfp, unsigned long attrs)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);
	struct sg_table *sgt;

	if (WARN_ON_ONCE(attrs & ~DMA_ATTR_ALLOC_SINGLE_PAGES))
		return NULL;
	if (WARN_ON_ONCE(gfp & __GFP_COMP))
		return NULL;

	if (ops && ops->alloc_noncontiguous)
		sgt = ops->alloc_noncontiguous(dev, size, dir, gfp, attrs);
	else
		sgt = alloc_single_sgt(dev, size, dir, gfp);

	if (sgt) {
		sgt->nents = 1;
		debug_dma_map_sg(dev, sgt->sgl, sgt->orig_nents, 1, dir, attrs);
	}
	return sgt;
}
EXPORT_SYMBOL_GPL(dma_alloc_noncontiguous);

static void free_single_sgt(struct device *dev, size_t size,
		struct sg_table *sgt, enum dma_data_direction dir)
{
	__dma_free_pages(dev, size, sg_page(sgt->sgl), sgt->sgl->dma_address,
			 dir);
	sg_free_table(sgt);
	kfree(sgt);
}

void dma_free_noncontiguous(struct device *dev, size_t size,
		struct sg_table *sgt, enum dma_data_direction dir)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	debug_dma_unmap_sg(dev, sgt->sgl, sgt->orig_nents, dir);
	if (ops && ops->free_noncontiguous)
		ops->free_noncontiguous(dev, size, sgt, dir);
	else
		free_single_sgt(dev, size, sgt, dir);
}
EXPORT_SYMBOL_GPL(dma_free_noncontiguous);

void *dma_vmap_noncontiguous(struct device *dev, size_t size,
		struct sg_table *sgt)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);
	unsigned long count = PAGE_ALIGN(size) >> PAGE_SHIFT;

	if (ops && ops->alloc_noncontiguous)
		return vmap(sgt_handle(sgt)->pages, count, VM_MAP, PAGE_KERNEL);
	return page_address(sg_page(sgt->sgl));
}
EXPORT_SYMBOL_GPL(dma_vmap_noncontiguous);

void dma_vunmap_noncontiguous(struct device *dev, void *vaddr)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	if (ops && ops->alloc_noncontiguous)
		vunmap(vaddr);
}
EXPORT_SYMBOL_GPL(dma_vunmap_noncontiguous);

int dma_mmap_noncontiguous(struct device *dev, struct vm_area_struct *vma,
		size_t size, struct sg_table *sgt)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	if (ops && ops->alloc_noncontiguous) {
		unsigned long count = PAGE_ALIGN(size) >> PAGE_SHIFT;

		if (vma->vm_pgoff >= count ||
		    vma_pages(vma) > count - vma->vm_pgoff)
			return -ENXIO;
		return vm_map_pages(vma, sgt_handle(sgt)->pages, count);
	}
	return dma_mmap_pages(dev, vma, size, sg_page(sgt->sgl));
}
EXPORT_SYMBOL_GPL(dma_mmap_noncontiguous);

static int dma_supported(struct device *dev, u64 mask)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	/*
	 * ->dma_supported sets the bypass flag, so we must always call
	 * into the method here unless the device is truly direct mapped.
	 */
	if (!ops)
		return dma_direct_supported(dev, mask);
	if (!ops->dma_supported)
		return 1;
	return ops->dma_supported(dev, mask);
}

bool dma_pci_p2pdma_supported(struct device *dev)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	/* if ops is not set, dma direct will be used which supports P2PDMA */
	if (!ops)
		return true;

	/*
	 * Note: dma_ops_bypass is not checked here because P2PDMA should
	 * not be used with dma mapping ops that do not have support even
	 * if the specific device is bypassing them.
	 */

	return ops->flags & DMA_F_PCI_P2PDMA_SUPPORTED;
}
EXPORT_SYMBOL_GPL(dma_pci_p2pdma_supported);

int dma_set_mask(struct device *dev, u64 mask)
{
	/*
	 * Truncate the mask to the actually supported dma_addr_t width to
	 * avoid generating unsupportable addresses.
	 */
	mask = (dma_addr_t)mask;

	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	arch_dma_set_mask(dev, mask);
	*dev->dma_mask = mask;
	return 0;
}
EXPORT_SYMBOL(dma_set_mask);

int dma_set_coherent_mask(struct device *dev, u64 mask)
{
	/*
	 * Truncate the mask to the actually supported dma_addr_t width to
	 * avoid generating unsupportable addresses.
	 */
	mask = (dma_addr_t)mask;

	if (!dma_supported(dev, mask))
		return -EIO;

	dev->coherent_dma_mask = mask;
	return 0;
}
EXPORT_SYMBOL(dma_set_coherent_mask);

/**
 * dma_addressing_limited - return if the device is addressing limited
 * @dev:	device to check
 *
 * Return %true if the devices DMA mask is too small to address all memory in
 * the system, else %false.  Lack of addressing bits is the prime reason for
 * bounce buffering, but might not be the only one.
 */
bool dma_addressing_limited(struct device *dev)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	if (min_not_zero(dma_get_mask(dev), dev->bus_dma_limit) <
			 dma_get_required_mask(dev))
		return true;

	if (unlikely(ops))
		return false;
	return !dma_direct_all_ram_mapped(dev);
}
EXPORT_SYMBOL_GPL(dma_addressing_limited);

size_t dma_max_mapping_size(struct device *dev)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);
	size_t size = SIZE_MAX;

	if (dma_map_direct(dev, ops))
		size = dma_direct_max_mapping_size(dev);
	else if (ops && ops->max_mapping_size)
		size = ops->max_mapping_size(dev);

	return size;
}
EXPORT_SYMBOL_GPL(dma_max_mapping_size);

size_t dma_opt_mapping_size(struct device *dev)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);
	size_t size = SIZE_MAX;

	if (ops && ops->opt_mapping_size)
		size = ops->opt_mapping_size();

	return min(dma_max_mapping_size(dev), size);
}
EXPORT_SYMBOL_GPL(dma_opt_mapping_size);

bool dma_need_sync(struct device *dev, dma_addr_t dma_addr)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	if (dma_map_direct(dev, ops))
		return dma_direct_need_sync(dev, dma_addr);
	return ops->sync_single_for_cpu || ops->sync_single_for_device;
}
EXPORT_SYMBOL_GPL(dma_need_sync);

unsigned long dma_get_merge_boundary(struct device *dev)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	if (!ops || !ops->get_merge_boundary)
		return 0;	/* can't merge */

	return ops->get_merge_boundary(dev);
}
EXPORT_SYMBOL_GPL(dma_get_merge_boundary);
