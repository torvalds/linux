/*
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#ifndef __SOC_ROCKCHIP_DMC_H
#define __SOC_ROCKCHIP_DMC_H

#include <linux/devfreq.h>

#ifdef ROCKCHIP_PM_DOMAINS
int rockchip_pm_register_notify_to_dmc(struct devfreq *devfreq);
#else
static inline int rockchip_pm_register_notify_to_dmc(struct devfreq *devfreq)
{
	return 0;
}
#endif

#ifdef CONFIG_DRM_ROCKCHIP
int rockchip_drm_register_notifier_to_dmc(struct devfreq *devfreq);
#else
static inline int rockchip_drm_register_notifier_to_dmc(struct devfreq *devfreq)
{
	return 0;
}
#endif

#ifdef CONFIG_ARM_ROCKCHIP_DMC_DEVFREQ
int rockchip_dmcfreq_vop_bandwidth_request(unsigned int bw_mbyte);
void rockchip_dmcfreq_vop_bandwidth_update(unsigned int bw_mbyte);
#else
int rockchip_dmcfreq_vop_bandwidth_request(unsigned int bw_mbyte)
{
	return 0;
}

void rockchip_dmcfreq_vop_bandwidth_update(unsigned int bw_mbyte)
{
}
#endif

#endif
