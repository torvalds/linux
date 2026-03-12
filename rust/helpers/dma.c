// SPDX-License-Identifier: GPL-2.0

#include <linux/dma-mapping.h>

__rust_helper void *rust_helper_dma_alloc_attrs(struct device *dev, size_t size,
						dma_addr_t *dma_handle,
						gfp_t flag, unsigned long attrs)
{
	return dma_alloc_attrs(dev, size, dma_handle, flag, attrs);
}

__rust_helper void rust_helper_dma_free_attrs(struct device *dev, size_t size,
					      void *cpu_addr,
					      dma_addr_t dma_handle,
					      unsigned long attrs)
{
	dma_free_attrs(dev, size, cpu_addr, dma_handle, attrs);
}

__rust_helper int rust_helper_dma_set_mask_and_coherent(struct device *dev,
							u64 mask)
{
	return dma_set_mask_and_coherent(dev, mask);
}

__rust_helper int rust_helper_dma_set_mask(struct device *dev, u64 mask)
{
	return dma_set_mask(dev, mask);
}

__rust_helper int rust_helper_dma_set_coherent_mask(struct device *dev, u64 mask)
{
	return dma_set_coherent_mask(dev, mask);
}

__rust_helper int rust_helper_dma_map_sgtable(struct device *dev, struct sg_table *sgt,
					      enum dma_data_direction dir, unsigned long attrs)
{
	return dma_map_sgtable(dev, sgt, dir, attrs);
}

__rust_helper size_t rust_helper_dma_max_mapping_size(struct device *dev)
{
	return dma_max_mapping_size(dev);
}

__rust_helper void rust_helper_dma_set_max_seg_size(struct device *dev,
						    unsigned int size)
{
	dma_set_max_seg_size(dev, size);
}
