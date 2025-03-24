// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *                   Takashi Iwai <tiwai@suse.de>
 * 
 *  Generic memory allocators
 */

#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/genalloc.h>
#include <linux/highmem.h>
#include <linux/vmalloc.h>
#ifdef CONFIG_X86
#include <asm/set_memory.h>
#endif
#include <sound/memalloc.h>

struct snd_malloc_ops {
	void *(*alloc)(struct snd_dma_buffer *dmab, size_t size);
	void (*free)(struct snd_dma_buffer *dmab);
	dma_addr_t (*get_addr)(struct snd_dma_buffer *dmab, size_t offset);
	struct page *(*get_page)(struct snd_dma_buffer *dmab, size_t offset);
	unsigned int (*get_chunk_size)(struct snd_dma_buffer *dmab,
				       unsigned int ofs, unsigned int size);
	int (*mmap)(struct snd_dma_buffer *dmab, struct vm_area_struct *area);
	void (*sync)(struct snd_dma_buffer *dmab, enum snd_dma_sync_mode mode);
};

#define DEFAULT_GFP \
	(GFP_KERNEL | \
	 __GFP_RETRY_MAYFAIL | /* don't trigger OOM-killer */ \
	 __GFP_NOWARN)   /* no stack trace print - this call is non-critical */

static const struct snd_malloc_ops *snd_dma_get_ops(struct snd_dma_buffer *dmab);

static void *__snd_dma_alloc_pages(struct snd_dma_buffer *dmab, size_t size)
{
	const struct snd_malloc_ops *ops = snd_dma_get_ops(dmab);

	if (WARN_ON_ONCE(!ops || !ops->alloc))
		return NULL;
	return ops->alloc(dmab, size);
}

/**
 * snd_dma_alloc_dir_pages - allocate the buffer area according to the given
 *	type and direction
 * @type: the DMA buffer type
 * @device: the device pointer
 * @dir: DMA direction
 * @size: the buffer size to allocate
 * @dmab: buffer allocation record to store the allocated data
 *
 * Calls the memory-allocator function for the corresponding
 * buffer type.
 *
 * Return: Zero if the buffer with the given size is allocated successfully,
 * otherwise a negative value on error.
 */
int snd_dma_alloc_dir_pages(int type, struct device *device,
			    enum dma_data_direction dir, size_t size,
			    struct snd_dma_buffer *dmab)
{
	if (WARN_ON(!size))
		return -ENXIO;
	if (WARN_ON(!dmab))
		return -ENXIO;

	size = PAGE_ALIGN(size);
	dmab->dev.type = type;
	dmab->dev.dev = device;
	dmab->dev.dir = dir;
	dmab->bytes = 0;
	dmab->addr = 0;
	dmab->private_data = NULL;
	dmab->area = __snd_dma_alloc_pages(dmab, size);
	if (!dmab->area)
		return -ENOMEM;
	dmab->bytes = size;
	return 0;
}
EXPORT_SYMBOL(snd_dma_alloc_dir_pages);

/**
 * snd_dma_alloc_pages_fallback - allocate the buffer area according to the given type with fallback
 * @type: the DMA buffer type
 * @device: the device pointer
 * @size: the buffer size to allocate
 * @dmab: buffer allocation record to store the allocated data
 *
 * Calls the memory-allocator function for the corresponding
 * buffer type.  When no space is left, this function reduces the size and
 * tries to allocate again.  The size actually allocated is stored in
 * res_size argument.
 *
 * Return: Zero if the buffer with the given size is allocated successfully,
 * otherwise a negative value on error.
 */
int snd_dma_alloc_pages_fallback(int type, struct device *device, size_t size,
				 struct snd_dma_buffer *dmab)
{
	int err;

	while ((err = snd_dma_alloc_pages(type, device, size, dmab)) < 0) {
		if (err != -ENOMEM)
			return err;
		if (size <= PAGE_SIZE)
			return -ENOMEM;
		size >>= 1;
		size = PAGE_SIZE << get_order(size);
	}
	if (! dmab->area)
		return -ENOMEM;
	return 0;
}
EXPORT_SYMBOL(snd_dma_alloc_pages_fallback);

/**
 * snd_dma_free_pages - release the allocated buffer
 * @dmab: the buffer allocation record to release
 *
 * Releases the allocated buffer via snd_dma_alloc_pages().
 */
void snd_dma_free_pages(struct snd_dma_buffer *dmab)
{
	const struct snd_malloc_ops *ops = snd_dma_get_ops(dmab);

	if (ops && ops->free)
		ops->free(dmab);
}
EXPORT_SYMBOL(snd_dma_free_pages);

/* called by devres */
static void __snd_release_pages(struct device *dev, void *res)
{
	snd_dma_free_pages(res);
}

/**
 * snd_devm_alloc_dir_pages - allocate the buffer and manage with devres
 * @dev: the device pointer
 * @type: the DMA buffer type
 * @dir: DMA direction
 * @size: the buffer size to allocate
 *
 * Allocate buffer pages depending on the given type and manage using devres.
 * The pages will be released automatically at the device removal.
 *
 * Unlike snd_dma_alloc_pages(), this function requires the real device pointer,
 * hence it can't work with SNDRV_DMA_TYPE_CONTINUOUS or
 * SNDRV_DMA_TYPE_VMALLOC type.
 *
 * Return: the snd_dma_buffer object at success, or NULL if failed
 */
struct snd_dma_buffer *
snd_devm_alloc_dir_pages(struct device *dev, int type,
			 enum dma_data_direction dir, size_t size)
{
	struct snd_dma_buffer *dmab;
	int err;

	if (WARN_ON(type == SNDRV_DMA_TYPE_CONTINUOUS ||
		    type == SNDRV_DMA_TYPE_VMALLOC))
		return NULL;

	dmab = devres_alloc(__snd_release_pages, sizeof(*dmab), GFP_KERNEL);
	if (!dmab)
		return NULL;

	err = snd_dma_alloc_dir_pages(type, dev, dir, size, dmab);
	if (err < 0) {
		devres_free(dmab);
		return NULL;
	}

	devres_add(dev, dmab);
	return dmab;
}
EXPORT_SYMBOL_GPL(snd_devm_alloc_dir_pages);

/**
 * snd_dma_buffer_mmap - perform mmap of the given DMA buffer
 * @dmab: buffer allocation information
 * @area: VM area information
 *
 * Return: zero if successful, or a negative error code
 */
int snd_dma_buffer_mmap(struct snd_dma_buffer *dmab,
			struct vm_area_struct *area)
{
	const struct snd_malloc_ops *ops;

	if (!dmab)
		return -ENOENT;
	ops = snd_dma_get_ops(dmab);
	if (ops && ops->mmap)
		return ops->mmap(dmab, area);
	else
		return -ENOENT;
}
EXPORT_SYMBOL(snd_dma_buffer_mmap);

#ifdef CONFIG_HAS_DMA
/**
 * snd_dma_buffer_sync - sync DMA buffer between CPU and device
 * @dmab: buffer allocation information
 * @mode: sync mode
 */
void snd_dma_buffer_sync(struct snd_dma_buffer *dmab,
			 enum snd_dma_sync_mode mode)
{
	const struct snd_malloc_ops *ops;

	if (!dmab || !dmab->dev.need_sync)
		return;
	ops = snd_dma_get_ops(dmab);
	if (ops && ops->sync)
		ops->sync(dmab, mode);
}
EXPORT_SYMBOL_GPL(snd_dma_buffer_sync);
#endif /* CONFIG_HAS_DMA */

/**
 * snd_sgbuf_get_addr - return the physical address at the corresponding offset
 * @dmab: buffer allocation information
 * @offset: offset in the ring buffer
 *
 * Return: the physical address
 */
dma_addr_t snd_sgbuf_get_addr(struct snd_dma_buffer *dmab, size_t offset)
{
	const struct snd_malloc_ops *ops = snd_dma_get_ops(dmab);

	if (ops && ops->get_addr)
		return ops->get_addr(dmab, offset);
	else
		return dmab->addr + offset;
}
EXPORT_SYMBOL(snd_sgbuf_get_addr);

/**
 * snd_sgbuf_get_page - return the physical page at the corresponding offset
 * @dmab: buffer allocation information
 * @offset: offset in the ring buffer
 *
 * Return: the page pointer
 */
struct page *snd_sgbuf_get_page(struct snd_dma_buffer *dmab, size_t offset)
{
	const struct snd_malloc_ops *ops = snd_dma_get_ops(dmab);

	if (ops && ops->get_page)
		return ops->get_page(dmab, offset);
	else
		return virt_to_page(dmab->area + offset);
}
EXPORT_SYMBOL(snd_sgbuf_get_page);

/**
 * snd_sgbuf_get_chunk_size - compute the max chunk size with continuous pages
 *	on sg-buffer
 * @dmab: buffer allocation information
 * @ofs: offset in the ring buffer
 * @size: the requested size
 *
 * Return: the chunk size
 */
unsigned int snd_sgbuf_get_chunk_size(struct snd_dma_buffer *dmab,
				      unsigned int ofs, unsigned int size)
{
	const struct snd_malloc_ops *ops = snd_dma_get_ops(dmab);

	if (ops && ops->get_chunk_size)
		return ops->get_chunk_size(dmab, ofs, size);
	else
		return size;
}
EXPORT_SYMBOL(snd_sgbuf_get_chunk_size);

/*
 * Continuous pages allocator
 */
static void *do_alloc_pages(struct device *dev, size_t size, dma_addr_t *addr,
			    bool wc)
{
	void *p;
	gfp_t gfp = GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN;

 again:
	p = alloc_pages_exact(size, gfp);
	if (!p)
		return NULL;
	*addr = page_to_phys(virt_to_page(p));
	if (!dev)
		return p;
	if ((*addr + size - 1) & ~dev->coherent_dma_mask) {
		if (IS_ENABLED(CONFIG_ZONE_DMA32) && !(gfp & GFP_DMA32)) {
			gfp |= GFP_DMA32;
			goto again;
		}
		if (IS_ENABLED(CONFIG_ZONE_DMA) && !(gfp & GFP_DMA)) {
			gfp = (gfp & ~GFP_DMA32) | GFP_DMA;
			goto again;
		}
	}
#ifdef CONFIG_X86
	if (wc)
		set_memory_wc((unsigned long)(p), size >> PAGE_SHIFT);
#endif
	return p;
}

static void do_free_pages(void *p, size_t size, bool wc)
{
#ifdef CONFIG_X86
	if (wc)
		set_memory_wb((unsigned long)(p), size >> PAGE_SHIFT);
#endif
	free_pages_exact(p, size);
}


static void *snd_dma_continuous_alloc(struct snd_dma_buffer *dmab, size_t size)
{
	return do_alloc_pages(dmab->dev.dev, size, &dmab->addr, false);
}

static void snd_dma_continuous_free(struct snd_dma_buffer *dmab)
{
	do_free_pages(dmab->area, dmab->bytes, false);
}

static int snd_dma_continuous_mmap(struct snd_dma_buffer *dmab,
				   struct vm_area_struct *area)
{
	return remap_pfn_range(area, area->vm_start,
			       dmab->addr >> PAGE_SHIFT,
			       area->vm_end - area->vm_start,
			       area->vm_page_prot);
}

static const struct snd_malloc_ops snd_dma_continuous_ops = {
	.alloc = snd_dma_continuous_alloc,
	.free = snd_dma_continuous_free,
	.mmap = snd_dma_continuous_mmap,
};

/*
 * VMALLOC allocator
 */
static void *snd_dma_vmalloc_alloc(struct snd_dma_buffer *dmab, size_t size)
{
	return vmalloc(size);
}

static void snd_dma_vmalloc_free(struct snd_dma_buffer *dmab)
{
	vfree(dmab->area);
}

static int snd_dma_vmalloc_mmap(struct snd_dma_buffer *dmab,
				struct vm_area_struct *area)
{
	return remap_vmalloc_range(area, dmab->area, 0);
}

#define get_vmalloc_page_addr(dmab, offset) \
	page_to_phys(vmalloc_to_page((dmab)->area + (offset)))

static dma_addr_t snd_dma_vmalloc_get_addr(struct snd_dma_buffer *dmab,
					   size_t offset)
{
	return get_vmalloc_page_addr(dmab, offset) + offset % PAGE_SIZE;
}

static struct page *snd_dma_vmalloc_get_page(struct snd_dma_buffer *dmab,
					     size_t offset)
{
	return vmalloc_to_page(dmab->area + offset);
}

static unsigned int
snd_dma_vmalloc_get_chunk_size(struct snd_dma_buffer *dmab,
			       unsigned int ofs, unsigned int size)
{
	unsigned int start, end;
	unsigned long addr;

	start = ALIGN_DOWN(ofs, PAGE_SIZE);
	end = ofs + size - 1; /* the last byte address */
	/* check page continuity */
	addr = get_vmalloc_page_addr(dmab, start);
	for (;;) {
		start += PAGE_SIZE;
		if (start > end)
			break;
		addr += PAGE_SIZE;
		if (get_vmalloc_page_addr(dmab, start) != addr)
			return start - ofs;
	}
	/* ok, all on continuous pages */
	return size;
}

static const struct snd_malloc_ops snd_dma_vmalloc_ops = {
	.alloc = snd_dma_vmalloc_alloc,
	.free = snd_dma_vmalloc_free,
	.mmap = snd_dma_vmalloc_mmap,
	.get_addr = snd_dma_vmalloc_get_addr,
	.get_page = snd_dma_vmalloc_get_page,
	.get_chunk_size = snd_dma_vmalloc_get_chunk_size,
};

#ifdef CONFIG_HAS_DMA
/*
 * IRAM allocator
 */
#ifdef CONFIG_GENERIC_ALLOCATOR
static void *snd_dma_iram_alloc(struct snd_dma_buffer *dmab, size_t size)
{
	struct device *dev = dmab->dev.dev;
	struct gen_pool *pool;
	void *p;

	if (dev->of_node) {
		pool = of_gen_pool_get(dev->of_node, "iram", 0);
		/* Assign the pool into private_data field */
		dmab->private_data = pool;

		p = gen_pool_dma_alloc_align(pool, size, &dmab->addr, PAGE_SIZE);
		if (p)
			return p;
	}

	/* Internal memory might have limited size and no enough space,
	 * so if we fail to malloc, try to fetch memory traditionally.
	 */
	dmab->dev.type = SNDRV_DMA_TYPE_DEV;
	return __snd_dma_alloc_pages(dmab, size);
}

static void snd_dma_iram_free(struct snd_dma_buffer *dmab)
{
	struct gen_pool *pool = dmab->private_data;

	if (pool && dmab->area)
		gen_pool_free(pool, (unsigned long)dmab->area, dmab->bytes);
}

static int snd_dma_iram_mmap(struct snd_dma_buffer *dmab,
			     struct vm_area_struct *area)
{
	area->vm_page_prot = pgprot_writecombine(area->vm_page_prot);
	return remap_pfn_range(area, area->vm_start,
			       dmab->addr >> PAGE_SHIFT,
			       area->vm_end - area->vm_start,
			       area->vm_page_prot);
}

static const struct snd_malloc_ops snd_dma_iram_ops = {
	.alloc = snd_dma_iram_alloc,
	.free = snd_dma_iram_free,
	.mmap = snd_dma_iram_mmap,
};
#endif /* CONFIG_GENERIC_ALLOCATOR */

/*
 * Coherent device pages allocator
 */
static void *snd_dma_dev_alloc(struct snd_dma_buffer *dmab, size_t size)
{
	return dma_alloc_coherent(dmab->dev.dev, size, &dmab->addr, DEFAULT_GFP);
}

static void snd_dma_dev_free(struct snd_dma_buffer *dmab)
{
	dma_free_coherent(dmab->dev.dev, dmab->bytes, dmab->area, dmab->addr);
}

static int snd_dma_dev_mmap(struct snd_dma_buffer *dmab,
			    struct vm_area_struct *area)
{
	return dma_mmap_coherent(dmab->dev.dev, area,
				 dmab->area, dmab->addr, dmab->bytes);
}

static const struct snd_malloc_ops snd_dma_dev_ops = {
	.alloc = snd_dma_dev_alloc,
	.free = snd_dma_dev_free,
	.mmap = snd_dma_dev_mmap,
};

/*
 * Write-combined pages
 */
#ifdef CONFIG_SND_DMA_SGBUF
/* x86-specific allocations */
static void *snd_dma_wc_alloc(struct snd_dma_buffer *dmab, size_t size)
{
	void *p = do_alloc_pages(dmab->dev.dev, size, &dmab->addr, true);

	if (!p)
		return NULL;
	dmab->addr = dma_map_single(dmab->dev.dev, p, size, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dmab->dev.dev, dmab->addr)) {
		do_free_pages(dmab->area, size, true);
		return NULL;
	}
	return p;
}

static void snd_dma_wc_free(struct snd_dma_buffer *dmab)
{
	dma_unmap_single(dmab->dev.dev, dmab->addr, dmab->bytes,
			 DMA_BIDIRECTIONAL);
	do_free_pages(dmab->area, dmab->bytes, true);
}

static int snd_dma_wc_mmap(struct snd_dma_buffer *dmab,
			   struct vm_area_struct *area)
{
	area->vm_page_prot = pgprot_writecombine(area->vm_page_prot);
	return dma_mmap_coherent(dmab->dev.dev, area,
				 dmab->area, dmab->addr, dmab->bytes);
}
#else
static void *snd_dma_wc_alloc(struct snd_dma_buffer *dmab, size_t size)
{
	return dma_alloc_wc(dmab->dev.dev, size, &dmab->addr, DEFAULT_GFP);
}

static void snd_dma_wc_free(struct snd_dma_buffer *dmab)
{
	dma_free_wc(dmab->dev.dev, dmab->bytes, dmab->area, dmab->addr);
}

static int snd_dma_wc_mmap(struct snd_dma_buffer *dmab,
			   struct vm_area_struct *area)
{
	return dma_mmap_wc(dmab->dev.dev, area,
			   dmab->area, dmab->addr, dmab->bytes);
}
#endif

static const struct snd_malloc_ops snd_dma_wc_ops = {
	.alloc = snd_dma_wc_alloc,
	.free = snd_dma_wc_free,
	.mmap = snd_dma_wc_mmap,
};

/*
 * Non-contiguous pages allocator
 */
static void *snd_dma_noncontig_alloc(struct snd_dma_buffer *dmab, size_t size)
{
	struct sg_table *sgt;
	void *p;

	sgt = dma_alloc_noncontiguous(dmab->dev.dev, size, dmab->dev.dir,
				      DEFAULT_GFP, 0);
	if (!sgt)
		return NULL;

	dmab->dev.need_sync = dma_need_sync(dmab->dev.dev,
					    sg_dma_address(sgt->sgl));
	p = dma_vmap_noncontiguous(dmab->dev.dev, size, sgt);
	if (p) {
		dmab->private_data = sgt;
		/* store the first page address for convenience */
		dmab->addr = snd_sgbuf_get_addr(dmab, 0);
	} else {
		dma_free_noncontiguous(dmab->dev.dev, size, sgt, dmab->dev.dir);
	}
	return p;
}

static void snd_dma_noncontig_free(struct snd_dma_buffer *dmab)
{
	dma_vunmap_noncontiguous(dmab->dev.dev, dmab->area);
	dma_free_noncontiguous(dmab->dev.dev, dmab->bytes, dmab->private_data,
			       dmab->dev.dir);
}

static int snd_dma_noncontig_mmap(struct snd_dma_buffer *dmab,
				  struct vm_area_struct *area)
{
	return dma_mmap_noncontiguous(dmab->dev.dev, area,
				      dmab->bytes, dmab->private_data);
}

static void snd_dma_noncontig_sync(struct snd_dma_buffer *dmab,
				   enum snd_dma_sync_mode mode)
{
	if (mode == SNDRV_DMA_SYNC_CPU) {
		if (dmab->dev.dir == DMA_TO_DEVICE)
			return;
		invalidate_kernel_vmap_range(dmab->area, dmab->bytes);
		dma_sync_sgtable_for_cpu(dmab->dev.dev, dmab->private_data,
					 dmab->dev.dir);
	} else {
		if (dmab->dev.dir == DMA_FROM_DEVICE)
			return;
		flush_kernel_vmap_range(dmab->area, dmab->bytes);
		dma_sync_sgtable_for_device(dmab->dev.dev, dmab->private_data,
					    dmab->dev.dir);
	}
}

static inline void snd_dma_noncontig_iter_set(struct snd_dma_buffer *dmab,
					      struct sg_page_iter *piter,
					      size_t offset)
{
	struct sg_table *sgt = dmab->private_data;

	__sg_page_iter_start(piter, sgt->sgl, sgt->orig_nents,
			     offset >> PAGE_SHIFT);
}

static dma_addr_t snd_dma_noncontig_get_addr(struct snd_dma_buffer *dmab,
					     size_t offset)
{
	struct sg_dma_page_iter iter;

	snd_dma_noncontig_iter_set(dmab, &iter.base, offset);
	__sg_page_iter_dma_next(&iter);
	return sg_page_iter_dma_address(&iter) + offset % PAGE_SIZE;
}

static struct page *snd_dma_noncontig_get_page(struct snd_dma_buffer *dmab,
					       size_t offset)
{
	struct sg_page_iter iter;

	snd_dma_noncontig_iter_set(dmab, &iter, offset);
	__sg_page_iter_next(&iter);
	return sg_page_iter_page(&iter);
}

static unsigned int
snd_dma_noncontig_get_chunk_size(struct snd_dma_buffer *dmab,
				 unsigned int ofs, unsigned int size)
{
	struct sg_dma_page_iter iter;
	unsigned int start, end;
	unsigned long addr;

	start = ALIGN_DOWN(ofs, PAGE_SIZE);
	end = ofs + size - 1; /* the last byte address */
	snd_dma_noncontig_iter_set(dmab, &iter.base, start);
	if (!__sg_page_iter_dma_next(&iter))
		return 0;
	/* check page continuity */
	addr = sg_page_iter_dma_address(&iter);
	for (;;) {
		start += PAGE_SIZE;
		if (start > end)
			break;
		addr += PAGE_SIZE;
		if (!__sg_page_iter_dma_next(&iter) ||
		    sg_page_iter_dma_address(&iter) != addr)
			return start - ofs;
	}
	/* ok, all on continuous pages */
	return size;
}

static const struct snd_malloc_ops snd_dma_noncontig_ops = {
	.alloc = snd_dma_noncontig_alloc,
	.free = snd_dma_noncontig_free,
	.mmap = snd_dma_noncontig_mmap,
	.sync = snd_dma_noncontig_sync,
	.get_addr = snd_dma_noncontig_get_addr,
	.get_page = snd_dma_noncontig_get_page,
	.get_chunk_size = snd_dma_noncontig_get_chunk_size,
};

#ifdef CONFIG_SND_DMA_SGBUF
/* Fallback SG-buffer allocations for x86 */
struct snd_dma_sg_fallback {
	struct sg_table sgt; /* used by get_addr - must be the first item */
	size_t count;
	struct page **pages;
	unsigned int *npages;
};

static void __snd_dma_sg_fallback_free(struct snd_dma_buffer *dmab,
				       struct snd_dma_sg_fallback *sgbuf)
{
	bool wc = dmab->dev.type == SNDRV_DMA_TYPE_DEV_WC_SG;
	size_t i, size;

	if (sgbuf->pages && sgbuf->npages) {
		i = 0;
		while (i < sgbuf->count) {
			size = sgbuf->npages[i];
			if (!size)
				break;
			do_free_pages(page_address(sgbuf->pages[i]),
				      size << PAGE_SHIFT, wc);
			i += size;
		}
	}
	kvfree(sgbuf->pages);
	kvfree(sgbuf->npages);
	kfree(sgbuf);
}

/* fallback manual S/G buffer allocations */
static void *snd_dma_sg_fallback_alloc(struct snd_dma_buffer *dmab, size_t size)
{
	bool wc = dmab->dev.type == SNDRV_DMA_TYPE_DEV_WC_SG;
	struct snd_dma_sg_fallback *sgbuf;
	struct page **pagep, *curp;
	size_t chunk;
	dma_addr_t addr;
	unsigned int idx, npages;
	void *p;

	sgbuf = kzalloc(sizeof(*sgbuf), GFP_KERNEL);
	if (!sgbuf)
		return NULL;
	size = PAGE_ALIGN(size);
	sgbuf->count = size >> PAGE_SHIFT;
	sgbuf->pages = kvcalloc(sgbuf->count, sizeof(*sgbuf->pages), GFP_KERNEL);
	sgbuf->npages = kvcalloc(sgbuf->count, sizeof(*sgbuf->npages), GFP_KERNEL);
	if (!sgbuf->pages || !sgbuf->npages)
		goto error;

	pagep = sgbuf->pages;
	chunk = size;
	idx = 0;
	while (size > 0) {
		chunk = min(size, chunk);
		p = do_alloc_pages(dmab->dev.dev, chunk, &addr, wc);
		if (!p) {
			if (chunk <= PAGE_SIZE)
				goto error;
			chunk >>= 1;
			chunk = PAGE_SIZE << get_order(chunk);
			continue;
		}

		size -= chunk;
		/* fill pages */
		npages = chunk >> PAGE_SHIFT;
		sgbuf->npages[idx] = npages;
		idx += npages;
		curp = virt_to_page(p);
		while (npages--)
			*pagep++ = curp++;
	}

	if (sg_alloc_table_from_pages(&sgbuf->sgt, sgbuf->pages, sgbuf->count,
				      0, sgbuf->count << PAGE_SHIFT, GFP_KERNEL))
		goto error;

	if (dma_map_sgtable(dmab->dev.dev, &sgbuf->sgt, DMA_BIDIRECTIONAL, 0))
		goto error_dma_map;

	p = vmap(sgbuf->pages, sgbuf->count, VM_MAP, PAGE_KERNEL);
	if (!p)
		goto error_vmap;

	dmab->private_data = sgbuf;
	/* store the first page address for convenience */
	dmab->addr = snd_sgbuf_get_addr(dmab, 0);
	return p;

 error_vmap:
	dma_unmap_sgtable(dmab->dev.dev, &sgbuf->sgt, DMA_BIDIRECTIONAL, 0);
 error_dma_map:
	sg_free_table(&sgbuf->sgt);
 error:
	__snd_dma_sg_fallback_free(dmab, sgbuf);
	return NULL;
}

static void snd_dma_sg_fallback_free(struct snd_dma_buffer *dmab)
{
	struct snd_dma_sg_fallback *sgbuf = dmab->private_data;

	vunmap(dmab->area);
	dma_unmap_sgtable(dmab->dev.dev, &sgbuf->sgt, DMA_BIDIRECTIONAL, 0);
	sg_free_table(&sgbuf->sgt);
	__snd_dma_sg_fallback_free(dmab, dmab->private_data);
}

static int snd_dma_sg_fallback_mmap(struct snd_dma_buffer *dmab,
				    struct vm_area_struct *area)
{
	struct snd_dma_sg_fallback *sgbuf = dmab->private_data;

	if (dmab->dev.type == SNDRV_DMA_TYPE_DEV_WC_SG)
		area->vm_page_prot = pgprot_writecombine(area->vm_page_prot);
	return vm_map_pages(area, sgbuf->pages, sgbuf->count);
}

static void *snd_dma_sg_alloc(struct snd_dma_buffer *dmab, size_t size)
{
	int type = dmab->dev.type;
	void *p;

	/* try the standard DMA API allocation at first */
	if (type == SNDRV_DMA_TYPE_DEV_WC_SG)
		dmab->dev.type = SNDRV_DMA_TYPE_DEV_WC;
	else
		dmab->dev.type = SNDRV_DMA_TYPE_DEV;
	p = __snd_dma_alloc_pages(dmab, size);
	if (p)
		return p;

	dmab->dev.type = type; /* restore the type */
	return snd_dma_sg_fallback_alloc(dmab, size);
}

static const struct snd_malloc_ops snd_dma_sg_ops = {
	.alloc = snd_dma_sg_alloc,
	.free = snd_dma_sg_fallback_free,
	.mmap = snd_dma_sg_fallback_mmap,
	/* reuse noncontig helper */
	.get_addr = snd_dma_noncontig_get_addr,
	/* reuse vmalloc helpers */
	.get_page = snd_dma_vmalloc_get_page,
	.get_chunk_size = snd_dma_vmalloc_get_chunk_size,
};
#endif /* CONFIG_SND_DMA_SGBUF */

/*
 * Non-coherent pages allocator
 */
static void *snd_dma_noncoherent_alloc(struct snd_dma_buffer *dmab, size_t size)
{
	void *p;

	p = dma_alloc_noncoherent(dmab->dev.dev, size, &dmab->addr,
				  dmab->dev.dir, DEFAULT_GFP);
	if (p)
		dmab->dev.need_sync = dma_need_sync(dmab->dev.dev, dmab->addr);
	return p;
}

static void snd_dma_noncoherent_free(struct snd_dma_buffer *dmab)
{
	dma_free_noncoherent(dmab->dev.dev, dmab->bytes, dmab->area,
			     dmab->addr, dmab->dev.dir);
}

static int snd_dma_noncoherent_mmap(struct snd_dma_buffer *dmab,
				    struct vm_area_struct *area)
{
	area->vm_page_prot = vm_get_page_prot(area->vm_flags);
	return dma_mmap_pages(dmab->dev.dev, area,
			      area->vm_end - area->vm_start,
			      virt_to_page(dmab->area));
}

static void snd_dma_noncoherent_sync(struct snd_dma_buffer *dmab,
				     enum snd_dma_sync_mode mode)
{
	if (mode == SNDRV_DMA_SYNC_CPU) {
		if (dmab->dev.dir != DMA_TO_DEVICE)
			dma_sync_single_for_cpu(dmab->dev.dev, dmab->addr,
						dmab->bytes, dmab->dev.dir);
	} else {
		if (dmab->dev.dir != DMA_FROM_DEVICE)
			dma_sync_single_for_device(dmab->dev.dev, dmab->addr,
						   dmab->bytes, dmab->dev.dir);
	}
}

static const struct snd_malloc_ops snd_dma_noncoherent_ops = {
	.alloc = snd_dma_noncoherent_alloc,
	.free = snd_dma_noncoherent_free,
	.mmap = snd_dma_noncoherent_mmap,
	.sync = snd_dma_noncoherent_sync,
};

#endif /* CONFIG_HAS_DMA */

/*
 * Entry points
 */
static const struct snd_malloc_ops *snd_dma_ops[] = {
	[SNDRV_DMA_TYPE_CONTINUOUS] = &snd_dma_continuous_ops,
	[SNDRV_DMA_TYPE_VMALLOC] = &snd_dma_vmalloc_ops,
#ifdef CONFIG_HAS_DMA
	[SNDRV_DMA_TYPE_DEV] = &snd_dma_dev_ops,
	[SNDRV_DMA_TYPE_DEV_WC] = &snd_dma_wc_ops,
	[SNDRV_DMA_TYPE_NONCONTIG] = &snd_dma_noncontig_ops,
	[SNDRV_DMA_TYPE_NONCOHERENT] = &snd_dma_noncoherent_ops,
#ifdef CONFIG_SND_DMA_SGBUF
	[SNDRV_DMA_TYPE_DEV_SG] = &snd_dma_sg_ops,
	[SNDRV_DMA_TYPE_DEV_WC_SG] = &snd_dma_sg_ops,
#endif
#ifdef CONFIG_GENERIC_ALLOCATOR
	[SNDRV_DMA_TYPE_DEV_IRAM] = &snd_dma_iram_ops,
#endif /* CONFIG_GENERIC_ALLOCATOR */
#endif /* CONFIG_HAS_DMA */
};

static const struct snd_malloc_ops *snd_dma_get_ops(struct snd_dma_buffer *dmab)
{
	if (WARN_ON_ONCE(!dmab))
		return NULL;
	if (WARN_ON_ONCE(dmab->dev.type <= SNDRV_DMA_TYPE_UNKNOWN ||
			 dmab->dev.type >= ARRAY_SIZE(snd_dma_ops)))
		return NULL;
	return snd_dma_ops[dmab->dev.type];
}
