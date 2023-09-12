/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Rockchip AMP support.
 *
 * Copyright (c) 2023 Rockchip Electronics Co. Ltd.
 * Author: Tony Xie <tony.xie@rock-chips.com>
 */

#ifndef _ROCKCHIP_AMP
#define _ROCKCHIP_AMP

#include <linux/irqchip/arm-gic-common.h>

#if IS_REACHABLE(CONFIG_ROCKCHIP_AMP)
void rockchip_amp_get_gic_info(u32 spis_num, enum gic_type gic_version);
int rockchip_amp_check_amp_irq(u32 irq);
u32 rockchip_amp_get_irq_prio(u32 irq);
u32 rockchip_amp_get_irq_cpumask(u32 irq);
u64 rockchip_amp_get_irq_aff(u32 irq);
int rockchip_amp_need_init_amp_irq(u32 irq);
#else
static inline void rockchip_amp_get_gic_info(u32 spis_num,
					     enum gic_type gic_version)
{
}

static inline int rockchip_amp_check_amp_irq(u32 irq)
{
	return 0;
}

static inline u32 rockchip_amp_get_irq_prio(u32 irq)
{
	return GICD_INT_DEF_PRI;
}

static inline u32 rockchip_amp_get_irq_cpumask(u32 irq)
{
	return 0;
}

static inline int rockchip_amp_need_init_amp_irq(u32 irq)
{
	return 0;
}

#endif /* CONFIG_ROCKCHIP_AMP */
#endif /* _ROCKCHIP_AMP */
