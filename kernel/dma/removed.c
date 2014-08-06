// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (c) 2013-2014,2018 The Linux Foundation. All rights reserved.
 *  Copyright (C) 2000-2004 Russell King
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>
#include <linux/highmem.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/iommu.h>
#include <linux/io.h>
#include <linux/vmalloc.h>
#include <linux/sizes.h>
#include <linux/spinlock.h>

struct removed_region {
	phys_addr_t	base;
	int		nr_pages;
	unsigned long	*bitmap;
	struct mutex	lock;
};

#define NO_KERNEL_MAPPING_DUMMY	0x2222

static int dma_init_removed_memory(phys_addr_t phys_addr, size_t size,
				struct removed_region **mem)
{
	struct removed_region *dma_mem = NULL;
	int pages = size >> PAGE_SHIFT;
	int bitmap_size = BITS_TO_LONGS(pages) * sizeof(long);

	dma_mem = kzalloc(sizeof(struct removed_region), GFP_KERNEL);
	if (!dma_mem)
		goto out;
	dma_mem->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!dma_mem->bitmap)
		goto free1_out;

	dma_mem->base = phys_addr;
	dma_mem->nr_pages = pages;
	mutex_init(&dma_mem->lock);

	*mem = dma_mem;

	return 0;

free1_out:
	kfree(dma_mem);
out:
	return -ENOMEM;
}

static int dma_assign_removed_region(struct device *dev,
					struct removed_region *mem)
{
	if (dev->removed_mem)
		return -EBUSY;

	dev->removed_mem = mem;
	return 0;
}

void *removed_alloc(struct device *dev, size_t size, dma_addr_t *handle,
		    gfp_t gfp, unsigned long attrs)
{
	bool no_kernel_mapping = attrs & DMA_ATTR_NO_KERNEL_MAPPING;
	bool skip_zeroing = attrs & DMA_ATTR_SKIP_ZEROING;
	int pageno;
	unsigned long order;
	void __iomem *addr = NULL;
	struct removed_region *dma_mem = dev->removed_mem;
	int nbits;
	unsigned int align;

	if (!gfpflags_allow_blocking(gfp))
		return NULL;

	size = PAGE_ALIGN(size);
	nbits = size >> PAGE_SHIFT;
	order = get_order(size);

	if (order > get_order(SZ_1M))
		order = get_order(SZ_1M);

	align = (1 << order) - 1;


	mutex_lock(&dma_mem->lock);
	pageno = bitmap_find_next_zero_area(dma_mem->bitmap, dma_mem->nr_pages,
						0, nbits, align);

	if (pageno < dma_mem->nr_pages) {
		phys_addr_t base = dma_mem->base + pageno * PAGE_SIZE;
		*handle = base;

		bitmap_set(dma_mem->bitmap, pageno, nbits);

		if (no_kernel_mapping && skip_zeroing) {
			addr = (void *)NO_KERNEL_MAPPING_DUMMY;
			goto out;
		}

		addr = ioremap_wc(base, size);
		if (WARN_ON(!addr)) {
			bitmap_clear(dma_mem->bitmap, pageno, nbits);
		} else {
			if (!skip_zeroing)
				memset_io(addr, 0, size);
			if (no_kernel_mapping) {
				iounmap(addr);
				addr = (void *)NO_KERNEL_MAPPING_DUMMY;
			}
			*handle = base;
		}
	}

out:
	mutex_unlock(&dma_mem->lock);
	return addr;
}


int removed_mmap(struct device *dev, struct vm_area_struct *vma,
		 void *cpu_addr, dma_addr_t dma_addr, size_t size,
		 unsigned long attrs)
{
	return -ENXIO;
}

void removed_free(struct device *dev, size_t size, void *cpu_addr,
		  dma_addr_t handle, unsigned long attrs)
{
	bool no_kernel_mapping = attrs & DMA_ATTR_NO_KERNEL_MAPPING;
	struct removed_region *dma_mem = dev->removed_mem;

	size = PAGE_ALIGN(size);
	if (!no_kernel_mapping)
		iounmap(cpu_addr);
	mutex_lock(&dma_mem->lock);
	bitmap_clear(dma_mem->bitmap, (handle - dma_mem->base) >> PAGE_SHIFT,
				size >> PAGE_SHIFT);
	mutex_unlock(&dma_mem->lock);
}

static dma_addr_t removed_map_page(struct device *dev, struct page *page,
			unsigned long offset, size_t size,
			enum dma_data_direction dir,
			unsigned long attrs)
{
	return ~(dma_addr_t)0;
}

static void removed_unmap_page(struct device *dev, dma_addr_t dma_handle,
		size_t size, enum dma_data_direction dir,
		unsigned long attrs)
{
}

static int removed_map_sg(struct device *dev, struct scatterlist *sg,
			int nents, enum dma_data_direction dir,
			unsigned long attrs)
{
	return 0;
}

static void removed_unmap_sg(struct device *dev,
			struct scatterlist *sg, int nents,
			enum dma_data_direction dir,
			unsigned long attrs)
{
}

static void removed_sync_single_for_cpu(struct device *dev,
			dma_addr_t dma_handle, size_t size,
			enum dma_data_direction dir)
{
}

void removed_sync_single_for_device(struct device *dev,
			dma_addr_t dma_handle, size_t size,
			enum dma_data_direction dir)
{
}

void removed_sync_sg_for_cpu(struct device *dev,
			struct scatterlist *sg, int nents,
			enum dma_data_direction dir)
{
}

void removed_sync_sg_for_device(struct device *dev,
			struct scatterlist *sg, int nents,
			enum dma_data_direction dir)
{
}

static void __iomem *removed_remap(struct device *dev, void *cpu_addr,
			dma_addr_t handle, size_t size, unsigned long attrs)
{
	return ioremap_wc(handle, size);
}

void removed_unremap(struct device *dev, void *remapped_address, size_t size)
{
	iounmap(remapped_address);
}

const struct dma_map_ops removed_dma_ops = {
	.alloc			= removed_alloc,
	.free			= removed_free,
	.mmap			= removed_mmap,
	.map_page		= removed_map_page,
	.unmap_page		= removed_unmap_page,
	.map_sg			= removed_map_sg,
	.unmap_sg		= removed_unmap_sg,
	.sync_single_for_cpu	= removed_sync_single_for_cpu,
	.sync_single_for_device	= removed_sync_single_for_device,
	.sync_sg_for_cpu	= removed_sync_sg_for_cpu,
	.sync_sg_for_device	= removed_sync_sg_for_device,
	.remap			= removed_remap,
	.unremap		= removed_unremap,
};
EXPORT_SYMBOL(removed_dma_ops);

#ifdef CONFIG_OF_RESERVED_MEM
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>

static int rmem_dma_device_init(struct reserved_mem *rmem, struct device *dev)
{
	struct removed_region *mem = rmem->priv;

	if (!mem && dma_init_removed_memory(rmem->base, rmem->size, &mem)) {
		pr_info("Reserved memory: failed to init DMA memory pool at %pa, size %ld MiB\n",
			&rmem->base, (unsigned long)rmem->size / SZ_1M);
		return -EINVAL;
	}
	set_dma_ops(dev, &removed_dma_ops);
	rmem->priv = mem;
	dma_assign_removed_region(dev, mem);
	return 0;
}

static void rmem_dma_device_release(struct reserved_mem *rmem,
					struct device *dev)
{
	dev->dma_mem = NULL;
}

static const struct reserved_mem_ops removed_mem_ops = {
	.device_init    = rmem_dma_device_init,
	.device_release = rmem_dma_device_release,
};

static int __init removed_dma_setup(struct reserved_mem *rmem)
{
	rmem->ops = &removed_mem_ops;
	pr_info("Removed memory: created DMA memory pool at %pa, size %ld MiB\n",
		&rmem->base, (unsigned long)rmem->size / SZ_1M);
	return 0;
}
RESERVEDMEM_OF_DECLARE(dma, "removed-dma-pool", removed_dma_setup);
#endif
