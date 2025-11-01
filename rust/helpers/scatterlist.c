// SPDX-License-Identifier: GPL-2.0

#include <linux/dma-direction.h>

dma_addr_t rust_helper_sg_dma_address(struct scatterlist *sg)
{
	return sg_dma_address(sg);
}

unsigned int rust_helper_sg_dma_len(struct scatterlist *sg)
{
	return sg_dma_len(sg);
}

struct scatterlist *rust_helper_sg_next(struct scatterlist *sg)
{
	return sg_next(sg);
}

void rust_helper_dma_unmap_sgtable(struct device *dev, struct sg_table *sgt,
				   enum dma_data_direction dir, unsigned long attrs)
{
	return dma_unmap_sgtable(dev, sgt, dir, attrs);
}
