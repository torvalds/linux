/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XEN_ARM_PAGE_COHERENT_H
#define _XEN_ARM_PAGE_COHERENT_H

#include <linux/dma-mapping.h>
#include <asm/page.h>

static inline void *xen_alloc_coherent_pages(struct device *hwdev, size_t size,
		dma_addr_t *dma_handle, gfp_t flags, unsigned long attrs)
{
	return dma_direct_alloc(hwdev, size, dma_handle, flags, attrs);
}

static inline void xen_free_coherent_pages(struct device *hwdev, size_t size,
		void *cpu_addr, dma_addr_t dma_handle, unsigned long attrs)
{
	dma_direct_free(hwdev, size, cpu_addr, dma_handle, attrs);
}

#endif /* _XEN_ARM_PAGE_COHERENT_H */
