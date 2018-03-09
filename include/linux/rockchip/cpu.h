/*
 * Copyright (C) 2017 Rockchip Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#ifndef __LINUX_ROCKCHIP_CPU_H
#define __LINUX_ROCKCHIP_CPU_H

#include <linux/of.h>

#ifdef CONFIG_ROCKCHIP_CPUINFO

extern unsigned long rockchip_soc_id;

#define ROCKCHIP_CPU_VERION_MASK	0x0000f000
#define ROCKCHIP_CPU_VERION_SHIFT	12

static inline unsigned long rockchip_get_cpu_version(void)
{
	return (rockchip_soc_id & ROCKCHIP_CPU_VERION_MASK)
		>> ROCKCHIP_CPU_VERION_SHIFT;
}

static inline void rockchip_set_cpu_version(unsigned long ver)
{
	rockchip_soc_id &= ~ROCKCHIP_CPU_VERION_MASK;
	rockchip_soc_id |=
		(ver << ROCKCHIP_CPU_VERION_SHIFT) & ROCKCHIP_CPU_VERION_MASK;
}

#else

#define rockchip_soc_id 0

static inline unsigned long rockchip_get_cpu_version(void)
{
	return 0;
}

static inline void rockchip_set_cpu_version(unsigned long ver)
{
}

#endif

#define ROCKCHIP_CPU_MASK       0xffff0000
#define ROCKCHIP_CPU_RK312X     0x31260000
#define ROCKCHIP_CPU_RK3288     0x32880000

#ifdef CONFIG_CPU_RK312X
static inline bool cpu_is_rk312x(void)
{
	if (rockchip_soc_id)
		return (rockchip_soc_id & ROCKCHIP_CPU_MASK) == ROCKCHIP_CPU_RK312X;
	return of_machine_is_compatible("rockchip,rk3126") ||
	       of_machine_is_compatible("rockchip,rk3126b") ||
	       of_machine_is_compatible("rockchip,rk3126c") ||
	       of_machine_is_compatible("rockchip,rk3128");
}
#else
static inline bool cpu_is_rk312x(void) { return false; }
#endif

#ifdef CONFIG_CPU_RK3288
static inline bool cpu_is_rk3288(void)
{
	if (rockchip_soc_id)
		return (rockchip_soc_id & ROCKCHIP_CPU_MASK) == ROCKCHIP_CPU_RK3288;
	return of_machine_is_compatible("rockchip,rk3288") ||
	       of_machine_is_compatible("rockchip,rk3288w");
}
#else
static inline bool cpu_is_rk3288(void) { return false; }
#endif

#define ROCKCHIP_SOC_MASK	(ROCKCHIP_CPU_MASK | 0xff)
#define ROCKCHIP_SOC_RK3126     (ROCKCHIP_CPU_RK312X | 0x00)
#define ROCKCHIP_SOC_RK3126B    (ROCKCHIP_CPU_RK312X | 0x10)
#define ROCKCHIP_SOC_RK3126C    (ROCKCHIP_CPU_RK312X | 0x20)
#define ROCKCHIP_SOC_RK3128     (ROCKCHIP_CPU_RK312X | 0x01)
#define ROCKCHIP_SOC_RK3288     (ROCKCHIP_CPU_RK3288 | 0x00)
#define ROCKCHIP_SOC_RK3288W    (ROCKCHIP_CPU_RK3288 | 0x01)

#define ROCKCHIP_SOC(id, ID) \
static inline bool soc_is_##id(void) \
{ \
	if (rockchip_soc_id) \
		return ((rockchip_soc_id & ROCKCHIP_SOC_MASK) == ROCKCHIP_SOC_ ##ID); \
	return of_machine_is_compatible("rockchip,"#id); \
}

ROCKCHIP_SOC(rk3126, RK3126)
ROCKCHIP_SOC(rk3126b, RK3126B)
ROCKCHIP_SOC(rk3126c, RK3126C)
ROCKCHIP_SOC(rk3128, RK3128)
ROCKCHIP_SOC(rk3288, RK3288)
ROCKCHIP_SOC(rk3288w, RK3288W)

#endif
