/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2016, 2018, 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _LINUX_MSM_DMA_IOMMU_MAPPING_H
#define _LINUX_MSM_DMA_IOMMU_MAPPING_H

#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/qcom-dma-mapping.h>

#if IS_ENABLED(CONFIG_QCOM_LAZY_MAPPING)
/*
 * This function is not taking a reference to the dma_buf here. It is expected
 * that clients hold reference to the dma_buf until they are done with mapping
 * and unmapping.
 */
int msm_dma_map_sg_attrs(struct device *dev, struct scatterlist *sg, int nents,
		   enum dma_data_direction dir, struct dma_buf *dma_buf,
		   unsigned long attrs);

/*
 * This function takes an extra reference to the dma_buf.
 * What this means is that calling msm_dma_unmap_sg will not result in buffer's
 * iommu mapping being removed, which means that subsequent calls to lazy map
 * will simply re-use the existing iommu mapping.
 * The iommu unmapping of the buffer will occur when the ION buffer is
 * destroyed.
 * Using lazy mapping can provide a performance benefit because subsequent
 * mappings are faster.
 *
 * The limitation of using this API are that all subsequent iommu mappings
 * must be the same as the original mapping, ie they must map the same part of
 * the buffer with the same dma data direction. Also there can't be multiple
 * mappings of different parts of the buffer.
 */
static inline int msm_dma_map_sg_lazy(struct device *dev,
			       struct scatterlist *sg, int nents,
			       enum dma_data_direction dir,
			       struct dma_buf *dma_buf)
{
	return msm_dma_map_sg_attrs(dev, sg, nents, dir, dma_buf, 0);
}

static inline int msm_dma_map_sg(struct device *dev, struct scatterlist *sg,
				  int nents, enum dma_data_direction dir,
				  struct dma_buf *dma_buf)
{
	unsigned long attrs;

	attrs = DMA_ATTR_NO_DELAYED_UNMAP;
	return msm_dma_map_sg_attrs(dev, sg, nents, dir, dma_buf, attrs);
}

void msm_dma_unmap_sg_attrs(struct device *dev, struct scatterlist *sgl,
			    int nents, enum dma_data_direction dir,
			    struct dma_buf *dma_buf, unsigned long attrs);

static inline int msm_dma_map_sgtable(struct device *dev, struct sg_table *sgt,
				      enum dma_data_direction dir,
				      struct dma_buf *dma_buf, unsigned long attrs)
{
	int nents;

	nents = msm_dma_map_sg_attrs(dev, sgt->sgl, sgt->orig_nents, dir, dma_buf, attrs);
	if (nents < 0)
		return nents;
	else if (unlikely(nents == 0))
		return -EINVAL;

	sgt->nents = nents;
	return 0;
}

static inline void msm_dma_unmap_sgtable(struct device *dev, struct sg_table *sgt,
					 enum dma_data_direction dir,
					 struct dma_buf *dma_buf, unsigned long attrs)
{
	msm_dma_unmap_sg_attrs(dev, sgt->sgl, sgt->nents, dir, dma_buf, attrs);
}

int msm_dma_unmap_all_for_dev(struct device *dev);

/*
 * Below is private function only to be called by framework (ION) and not by
 * clients.
 */
void msm_dma_buf_freed(void *buffer);

#else /*CONFIG_QCOM_LAZY_MAPPING*/

static inline int msm_dma_map_sg_attrs(struct device *dev,
			struct scatterlist *sg, int nents,
			enum dma_data_direction dir, struct dma_buf *dma_buf,
			unsigned long attrs)
{
	return -EINVAL;
}

static inline void
msm_dma_unmap_sg_attrs(struct device *dev, struct scatterlist *sgl,
		       int nents, enum dma_data_direction dir,
		       struct dma_buf *dma_buf, unsigned long attrs)
{
}

static inline int msm_dma_map_sg_lazy(struct device *dev,
			       struct scatterlist *sg, int nents,
			       enum dma_data_direction dir,
			       struct dma_buf *dma_buf)
{
	return -EINVAL;
}

static inline int msm_dma_map_sg(struct device *dev, struct scatterlist *sg,
				  int nents, enum dma_data_direction dir,
				  struct dma_buf *dma_buf)
{
	return -EINVAL;
}

static inline int msm_dma_map_sgtable(struct device *dev, struct sg_table *sgt,
				      enum dma_data_direction dir,
				      struct dma_buf *dma_buf, unsigned long attrs)
{
	return -EINVAL;
}

static inline void msm_dma_unmap_sgtable(struct device *dev, struct sg_table *sgt,
					 enum dma_data_direction dir,
					 struct dma_buf *dma_buf, unsigned long attrs)
{
}

static inline int msm_dma_unmap_all_for_dev(struct device *dev)
{
	return 0;
}

static inline void msm_dma_buf_freed(void *buffer) {}
#endif /*CONFIG_QCOM_LAZY_MAPPING*/

#endif
