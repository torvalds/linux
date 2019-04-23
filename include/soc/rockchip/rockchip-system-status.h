/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (C) 2019, Fuzhou Rockchip Electronics Co., Ltd
 */

#ifndef __SOC_ROCKCHIP_SYSTEM_STATUS_H
#define __SOC_ROCKCHIP_SYSTEM_STATUS_H

#ifdef CONFIG_ROCKCHIP_SYSTEM_MONITOR
int rockchip_register_system_status_notifier(struct notifier_block *nb);
int rockchip_unregister_system_status_notifier(struct notifier_block *nb);
void rockchip_set_system_status(unsigned long status);
void rockchip_clear_system_status(unsigned long status);
unsigned long rockchip_get_system_status(void);
int rockchip_add_system_status_interface(struct device *dev);
void rockchip_update_system_status(const char *buf);
#else
static inline int
rockchip_register_system_status_notifier(struct notifier_block *nb)
{
	return -ENOTSUPP;
};

static inline int
rockchip_unregister_system_status_notifier(struct notifier_block *nb)
{
	return -ENOTSUPP;
};

static inline void rockchip_set_system_status(unsigned long status)
{
};

static inline void rockchip_clear_system_status(unsigned long status)
{
};

static inline unsigned long rockchip_get_system_status(void)
{
	return 0;
};

static inline int rockchip_add_system_status_interface(struct device *dev)
{
	return -ENOTSUPP;
};

static inline void rockchip_update_system_status(const char *buf)
{
};
#endif /* CONFIG_ROCKCHIP_SYSTEM_MONITOR */

#endif
