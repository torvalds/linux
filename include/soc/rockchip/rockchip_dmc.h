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

/* for lcdc_type */
#define SCREEN_NULL		0
#define SCREEN_RGB		1
#define SCREEN_LVDS		2
#define SCREEN_DUAL_LVDS	3
#define SCREEN_MCU		4
#define SCREEN_TVOUT		5
#define SCREEN_HDMI		6
#define SCREEN_MIPI		7
#define SCREEN_DUAL_MIPI	8
#define SCREEN_EDP		9
#define SCREEN_TVOUT_TEST	10
#define SCREEN_LVDS_10BIT	11
#define SCREEN_DUAL_LVDS_10BIT	12
#define SCREEN_DP		13

#ifdef CONFIG_ARM_ROCKCHIP_DMC_DEVFREQ
void rockchip_dmcfreq_lock(void);
void rockchip_dmcfreq_lock_nested(void);
void rockchip_dmcfreq_unlock(void);
int rockchip_dmcfreq_wait_complete(void);
int rockchip_dmcfreq_vop_bandwidth_request(struct devfreq *devfreq,
					   unsigned int bw_mbyte);
void rockchip_dmcfreq_vop_bandwidth_update(struct devfreq *devfreq,
					   unsigned int bw_mbyte,
					   unsigned int plane_num);

#else
static inline void rockchip_dmcfreq_lock(void)
{
}

static inline void rockchip_dmcfreq_lock_nested(void)
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
				      unsigned int bw_mbyte,
				      unsigned int plane_num)
{
}
#endif

#endif
