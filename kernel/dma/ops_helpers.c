// SPDX-License-Identifier: GPL-2.0
/*
 * Helpers for DMA ops implementations.  These generally rely on the fact that
 * the allocated memory contains normal pages in the direct kernel mapping.
 */
#include <linux/dma-map-ops.h>
#include <linux/dma-noncoherent.h>

/*
 * Create scatter-list for the already allocated DMA buffer.
 */
int dma_common_get_sgtable(struct device *dev, struct sg_table *sgt,
		 void *cpu_addr, dma_addr_t dma_addr, size_t size,
		 unsigned long attrs)
{
	struct page *page = virt_to_page(cpu_addr);
	int ret;

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (!ret)
		sg_set_page(sgt->sgl, page, PAGE_ALIGN(size), 0);
	return ret;
}

/*
 * Create userspace mapping for the DMA-coherent memory.
 */
int dma_common_mmap(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs)
{
#ifdef CONFIG_MMU
	unsigned long user_count = vma_pages(vma);
	unsigned long count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long off = vma->vm_pgoff;
	int ret = -ENXIO;

	vma->vm_page_prot = dma_pgprot(dev, vma->vm_page_prot, attrs);

	if (dma_mmap_from_dev_coherent(dev, vma, cpu_addr, size, &ret))
		return ret;

	if (off >= count || user_count > count - off)
		return -ENXIO;

	return remap_pfn_range(vma, vma->vm_start,
			page_to_pfn(virt_to_page(cpu_addr)) + vma->vm_pgoff,
			user_count << PAGE_SHIFT, vma->vm_page_prot);
#else
	return -ENXIO;
#endif /* CONFIG_MMU */
}

struct page *dma_common_alloc_pages(struct device *dev, size_t size,
		dma_addr_t *dma_handle, enum dma_data_direction dir, gfp_t gfp)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);
	struct page *page;

	page = dma_alloc_contiguous(dev, size, gfp);
	if (!page)
		page = alloc_pages_node(dev_to_node(dev), gfp, get_order(size));
	if (!page)
		return NULL;

	*dma_handle = ops->map_page(dev, page, 0, size, dir,
				    DMA_ATTR_SKIP_CPU_SYNC);
	if (*dma_handle == DMA_MAPPING_ERROR) {
		dma_free_contiguous(dev, page, size);
		return NULL;
	}

	memset(page_address(page), 0, size);
	return page;
}

void dma_common_free_pages(struct device *dev, size_t size, struct page *page,
		dma_addr_t dma_handle, enum dma_data_direction dir)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);

	if (ops->unmap_page)
		ops->unmap_page(dev, dma_handle, size, dir,
				DMA_ATTR_SKIP_CPU_SYNC);
	dma_free_contiguous(dev, page, size);
}
