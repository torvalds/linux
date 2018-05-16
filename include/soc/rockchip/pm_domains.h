/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SOC_ROCKCHIP_PM_DOMAINS_H
#define __SOC_ROCKCHIP_PM_DOMAINS_H

#include <linux/errno.h>

struct device;

#ifdef CONFIG_ROCKCHIP_PM_DOMAINS
int rockchip_pmu_idle_request(struct device *dev, bool idle);
int rockchip_save_qos(struct device *dev);
int rockchip_restore_qos(struct device *dev);
void rockchip_dump_pmu(void);
#else
static inline int rockchip_pmu_idle_request(struct device *dev, bool idle)
{
	return -ENOTSUPP;
}

static inline int rockchip_save_qos(struct device *dev)
{
	return -ENOTSUPP;
}

static inline int rockchip_restore_qos(struct device *dev)
{
	return -ENOTSUPP;
}

static inline void rockchip_dump_pmu(void)
{
}
#endif

#endif
