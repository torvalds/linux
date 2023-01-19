/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __SMMU_PROXY_CALLBACKS_H_
#define __SMMU_PROXY_CALLBACKS_H_

typedef int (*smmu_proxy_map_sgtable)(struct device *client_dev, struct sg_table *table,
				      struct dma_buf *dmabuf);

typedef void (*smmu_proxy_unmap_sgtable)(struct device *client_dev, struct sg_table *table,
					 struct dma_buf *dmabuf);

struct smmu_proxy_callbacks {
	smmu_proxy_map_sgtable map_sgtable;
	smmu_proxy_unmap_sgtable unmap_sgtable;
};

int qti_smmu_proxy_register_callbacks(smmu_proxy_map_sgtable map_sgtable_fn_ptr,
				      smmu_proxy_unmap_sgtable unmap_sgtable_fn_ptr);

#endif /* __SMMU_PROXY_CALLBACKS_H_ */
