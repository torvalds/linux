/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/device.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/mm.h>

#ifndef ioremap_cache
/* temporary while we convert existing ioremap_cache users to memremap */
__weak void __iomem *ioremap_cache(resource_size_t offset, unsigned long size)
{
	return ioremap(offset, size);
}
#endif

#ifndef arch_memremap_wb
static void *arch_memremap_wb(resource_size_t offset, unsigned long size)
{
	return (__force void *)ioremap_cache(offset, size);
}
#endif

#ifndef arch_memremap_can_ram_remap
static bool arch_memremap_can_ram_remap(resource_size_t offset, size_t size,
					unsigned long flags)
{
	return true;
}
#endif

static void *try_ram_remap(resource_size_t offset, size_t size,
			   unsigned long flags)
{
	unsigned long pfn = PHYS_PFN(offset);

	/* In the simple case just return the existing linear address */
	if (pfn_valid(pfn) && !PageHighMem(pfn_to_page(pfn)) &&
	    arch_memremap_can_ram_remap(offset, size, flags))
		return __va(offset);

	return NULL; /* fallback to arch_memremap_wb */
}

/**
 * memremap() - remap an iomem_resource as cacheable memory
 * @offset: iomem resource start address
 * @size: size of remap
 * @flags: any of MEMREMAP_WB, MEMREMAP_WT, MEMREMAP_WC,
 *		  MEMREMAP_ENC, MEMREMAP_DEC
 *
 * memremap() is "ioremap" for cases where it is known that the resource
 * being mapped does not have i/o side effects and the __iomem
 * annotation is not applicable. In the case of multiple flags, the different
 * mapping types will be attempted in the order listed below until one of
 * them succeeds.
 *
 * MEMREMAP_WB - matches the default mapping for System RAM on
 * the architecture.  This is usually a read-allocate write-back cache.
 * Moreover, if MEMREMAP_WB is specified and the requested remap region is RAM
 * memremap() will bypass establishing a new mapping and instead return
 * a pointer into the direct map.
 *
 * MEMREMAP_WT - establish a mapping whereby writes either bypass the
 * cache or are written through to memory and never exist in a
 * cache-dirty state with respect to program visibility.  Attempts to
 * map System RAM with this mapping type will fail.
 *
 * MEMREMAP_WC - establish a writecombine mapping, whereby writes may
 * be coalesced together (e.g. in the CPU's write buffers), but is otherwise
 * uncached. Attempts to map System RAM with this mapping type will fail.
 */
void *memremap(resource_size_t offset, size_t size, unsigned long flags)
{
	int is_ram = region_intersects(offset, size,
				       IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE);
	void *addr = NULL;

	if (!flags)
		return NULL;

	if (is_ram == REGION_MIXED) {
		WARN_ONCE(1, "memremap attempted on mixed range %pa size: %#lx\n",
				&offset, (unsigned long) size);
		return NULL;
	}

	/* Try all mapping types requested until one returns non-NULL */
	if (flags & MEMREMAP_WB) {
		/*
		 * MEMREMAP_WB is special in that it can be satisfied
		 * from the direct map.  Some archs depend on the
		 * capability of memremap() to autodetect cases where
		 * the requested range is potentially in System RAM.
		 */
		if (is_ram == REGION_INTERSECTS)
			addr = try_ram_remap(offset, size, flags);
		if (!addr)
			addr = arch_memremap_wb(offset, size);
	}

	/*
	 * If we don't have a mapping yet and other request flags are
	 * present then we will be attempting to establish a new virtual
	 * address mapping.  Enforce that this mapping is not aliasing
	 * System RAM.
	 */
	if (!addr && is_ram == REGION_INTERSECTS && flags != MEMREMAP_WB) {
		WARN_ONCE(1, "memremap attempted on ram %pa size: %#lx\n",
				&offset, (unsigned long) size);
		return NULL;
	}

	if (!addr && (flags & MEMREMAP_WT))
		addr = ioremap_wt(offset, size);

	if (!addr && (flags & MEMREMAP_WC))
		addr = ioremap_wc(offset, size);

	return addr;
}
EXPORT_SYMBOL(memremap);

void memunmap(void *addr)
{
	if (is_ioremap_addr(addr))
		iounmap((void __iomem *) addr);
}
EXPORT_SYMBOL(memunmap);

static void devm_memremap_release(struct device *dev, void *res)
{
	memunmap(*(void **)res);
}

static int devm_memremap_match(struct device *dev, void *res, void *match_data)
{
	return *(void **)res == match_data;
}

void *devm_memremap(struct device *dev, resource_size_t offset,
		size_t size, unsigned long flags)
{
	void **ptr, *addr;

	ptr = devres_alloc_node(devm_memremap_release, sizeof(*ptr), GFP_KERNEL,
			dev_to_node(dev));
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	addr = memremap(offset, size, flags);
	if (addr) {
		*ptr = addr;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
		return ERR_PTR(-ENXIO);
	}

	return addr;
}
EXPORT_SYMBOL(devm_memremap);

void devm_memunmap(struct device *dev, void *addr)
{
	WARN_ON(devres_release(dev, devm_memremap_release,
				devm_memremap_match, addr));
}
EXPORT_SYMBOL(devm_memunmap);
