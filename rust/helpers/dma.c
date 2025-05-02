// SPDX-License-Identifier: GPL-2.0

#include <linux/dma-mapping.h>

void *rust_helper_dma_alloc_attrs(struct device *dev, size_t size,
				  dma_addr_t *dma_handle, gfp_t flag,
				  unsigned long attrs)
{
	return dma_alloc_attrs(dev, size, dma_handle, flag, attrs);
}

void rust_helper_dma_free_attrs(struct device *dev, size_t size, void *cpu_addr,
				dma_addr_t dma_handle, unsigned long attrs)
{
	dma_free_attrs(dev, size, cpu_addr, dma_handle, attrs);
}
