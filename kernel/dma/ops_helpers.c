// SPDX-License-Identifier: GPL-2.0
/*
 * Helpers for DMA ops implementations.  These generally rely on the fact that
 * the allocated memory contains normal pages in the direct kernel mapping.
 */
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
