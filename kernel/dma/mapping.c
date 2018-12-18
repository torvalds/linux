// SPDX-License-Identifier: GPL-2.0
/*
 * arch-independent dma-mapping routines
 *
 * Copyright (c) 2006  SUSE Linux Products GmbH
 * Copyright (c) 2006  Tejun Heo <teheo@suse.de>
 */

#include <linux/acpi.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/gfp.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

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
 * dmam_alloc_coherent - Managed dma_alloc_coherent()
 * @dev: Device to allocate coherent memory for
 * @size: Size of allocation
 * @dma_handle: Out argument for allocated DMA handle
 * @gfp: Allocation flags
 *
 * Managed dma_alloc_coherent().  Memory allocated using this function
 * will be automatically released on driver detach.
 *
 * RETURNS:
 * Pointer to allocated memory on success, NULL on failure.
 */
void *dmam_alloc_coherent(struct device *dev, size_t size,
			   dma_addr_t *dma_handle, gfp_t gfp)
{
	struct dma_devres *dr;
	void *vaddr;

	dr = devres_alloc(dmam_release, sizeof(*dr), gfp);
	if (!dr)
		return NULL;

	vaddr = dma_alloc_coherent(dev, size, dma_handle, gfp);
	if (!vaddr) {
		devres_free(dr);
		return NULL;
	}

	dr->vaddr = vaddr;
	dr->dma_handle = *dma_handle;
	dr->size = size;

	devres_add(dev, dr);

	return vaddr;
}
EXPORT_SYMBOL(dmam_alloc_coherent);

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

#ifdef CONFIG_HAVE_GENERIC_DMA_COHERENT

static void dmam_coherent_decl_release(struct device *dev, void *res)
{
	dma_release_declared_memory(dev);
}

/**
 * dmam_declare_coherent_memory - Managed dma_declare_coherent_memory()
 * @dev: Device to declare coherent memory for
 * @phys_addr: Physical address of coherent memory to be declared
 * @device_addr: Device address of coherent memory to be declared
 * @size: Size of coherent memory to be declared
 * @flags: Flags
 *
 * Managed dma_declare_coherent_memory().
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int dmam_declare_coherent_memory(struct device *dev, phys_addr_t phys_addr,
				 dma_addr_t device_addr, size_t size, int flags)
{
	void *res;
	int rc;

	res = devres_alloc(dmam_coherent_decl_release, 0, GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	rc = dma_declare_coherent_memory(dev, phys_addr, device_addr, size,
					 flags);
	if (!rc)
		devres_add(dev, res);
	else
		devres_free(res);

	return rc;
}
EXPORT_SYMBOL(dmam_declare_coherent_memory);

/**
 * dmam_release_declared_memory - Managed dma_release_declared_memory().
 * @dev: Device to release declared coherent memory for
 *
 * Managed dmam_release_declared_memory().
 */
void dmam_release_declared_memory(struct device *dev)
{
	WARN_ON(devres_destroy(dev, dmam_coherent_decl_release, NULL, NULL));
}
EXPORT_SYMBOL(dmam_release_declared_memory);

#endif

/*
 * Create scatter-list for the already allocated DMA buffer.
 */
int dma_common_get_sgtable(struct device *dev, struct sg_table *sgt,
		 void *cpu_addr, dma_addr_t handle, size_t size)
{
	struct page *page = virt_to_page(cpu_addr);
	int ret;

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (unlikely(ret))
		return ret;

	sg_set_page(sgt->sgl, page, PAGE_ALIGN(size), 0);
	return 0;
}
EXPORT_SYMBOL(dma_common_get_sgtable);

/*
 * Create userspace mapping for the DMA-coherent memory.
 */
int dma_common_mmap(struct device *dev, struct vm_area_struct *vma,
		    void *cpu_addr, dma_addr_t dma_addr, size_t size)
{
	int ret = -ENXIO;
#ifndef CONFIG_ARCH_NO_COHERENT_DMA_MMAP
	unsigned long user_count = vma_pages(vma);
	unsigned long count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long off = vma->vm_pgoff;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (dma_mmap_from_dev_coherent(dev, vma, cpu_addr, size, &ret))
		return ret;

	if (off < count && user_count <= (count - off))
		ret = remap_pfn_range(vma, vma->vm_start,
				      page_to_pfn(virt_to_page(cpu_addr)) + off,
				      user_count << PAGE_SHIFT,
				      vma->vm_page_prot);
#endif	/* !CONFIG_ARCH_NO_COHERENT_DMA_MMAP */

	return ret;
}
EXPORT_SYMBOL(dma_common_mmap);

#ifdef CONFIG_MMU
static struct vm_struct *__dma_common_pages_remap(struct page **pages,
			size_t size, unsigned long vm_flags, pgprot_t prot,
			const void *caller)
{
	struct vm_struct *area;

	area = get_vm_area_caller(size, vm_flags, caller);
	if (!area)
		return NULL;

	if (map_vm_area(area, prot, pages)) {
		vunmap(area->addr);
		return NULL;
	}

	return area;
}

/*
 * remaps an array of PAGE_SIZE pages into another vm_area
 * Cannot be used in non-sleeping contexts
 */
void *dma_common_pages_remap(struct page **pages, size_t size,
			unsigned long vm_flags, pgprot_t prot,
			const void *caller)
{
	struct vm_struct *area;

	area = __dma_common_pages_remap(pages, size, vm_flags, prot, caller);
	if (!area)
		return NULL;

	area->pages = pages;

	return area->addr;
}

/*
 * remaps an allocated contiguous region into another vm_area.
 * Cannot be used in non-sleeping contexts
 */

void *dma_common_contiguous_remap(struct page *page, size_t size,
			unsigned long vm_flags,
			pgprot_t prot, const void *caller)
{
	int i;
	struct page **pages;
	struct vm_struct *area;

	pages = kmalloc(sizeof(struct page *) << get_order(size), GFP_KERNEL);
	if (!pages)
		return NULL;

	for (i = 0; i < (size >> PAGE_SHIFT); i++)
		pages[i] = nth_page(page, i);

	area = __dma_common_pages_remap(pages, size, vm_flags, prot, caller);

	kfree(pages);

	if (!area)
		return NULL;
	return area->addr;
}

/*
 * unmaps a range previously mapped by dma_common_*_remap
 */
void dma_common_free_remap(void *cpu_addr, size_t size, unsigned long vm_flags)
{
	struct vm_struct *area = find_vm_area(cpu_addr);

	if (!area || (area->flags & vm_flags) != vm_flags) {
		WARN(1, "trying to free invalid coherent area: %p\n", cpu_addr);
		return;
	}

	unmap_kernel_range((unsigned long)cpu_addr, PAGE_ALIGN(size));
	vunmap(cpu_addr);
}
#endif

/*
 * enables DMA API use for a device
 */
int dma_configure(struct device *dev)
{
	if (dev->bus->dma_configure)
		return dev->bus->dma_configure(dev);
	return 0;
}

void dma_deconfigure(struct device *dev)
{
	of_dma_deconfigure(dev);
	acpi_dma_deconfigure(dev);
}
