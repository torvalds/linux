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

#ifdef CONFIG_ARM_ROCKCHIP_DMC_DEVFREQ
void rockchip_dmcfreq_lock(void);
void rockchip_dmcfreq_unlock(void);
int rockchip_dmcfreq_wait_complete(void);
int rockchip_dmcfreq_vop_bandwidth_request(struct devfreq *devfreq,
					   unsigned int bw_mbyte);
void rockchip_dmcfreq_vop_bandwidth_update(struct devfreq *devfreq,
					   unsigned int bw_mbyte);

#else
static inline void rockchip_dmcfreq_lock(void)
{
}

static inline void rockchip_dmcfreq_unlock(void)
{
}

static inline int rockchip_dmcfreq_wait_complete(void)
{
	return 0;
}

static inline int
rockchip_dmcfreq_vop_bandwidth_request(struct devfreq *devfreq,
				       unsigned int bw_mbyte)
{
	return 0;
}

static inline void
rockchip_dmcfreq_vop_bandwidth_update(struct devfreq *devfreq,
				      unsigned int bw_mbyte)
{
}
#endif

#endif
