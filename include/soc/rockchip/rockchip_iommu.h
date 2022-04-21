/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd
 */
#ifndef __SOC_ROCKCHIP_IOMMU_H
#define __SOC_ROCKCHIP_IOMMU_H

struct device;

#if IS_ENABLED(CONFIG_ROCKCHIP_IOMMU)
int rockchip_iommu_enable(struct device *dev);
int rockchip_iommu_disable(struct device *dev);
int rockchip_pagefault_done(struct device *master_dev);
void __iomem *rockchip_get_iommu_base(struct device *master_dev, int idx);
bool rockchip_iommu_is_enabled(struct device *dev);
#else
static inline int rockchip_iommu_enable(struct device *dev)
{
	return -ENODEV;
}
static inline int rockchip_iommu_disable(struct device *dev)
{
	return -ENODEV;
}
static inline int rockchip_pagefault_done(struct device *master_dev)
{
	return 0;
}
static inline void __iomem *rockchip_get_iommu_base(struct device *master_dev, int idx)
{
	return NULL;
}
static inline bool rockchip_iommu_is_enabled(struct device *dev)
{
	return false;
}
#endif

#endif
