/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2012 ARM Limited
 */

#ifndef _LINUX_VEXPRESS_H
#define _LINUX_VEXPRESS_H

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>

#define VEXPRESS_SITE_MB		0
#define VEXPRESS_SITE_DB1		1
#define VEXPRESS_SITE_DB2		2
#define VEXPRESS_SITE_MASTER		0xf

#define VEXPRESS_RES_FUNC(_site, _func)	\
{					\
	.start = (_site),		\
	.end = (_func),			\
	.flags = IORESOURCE_BUS,	\
}

/* Config infrastructure */

void vexpress_config_set_master(u32 site);
u32 vexpress_config_get_master(void);

void vexpress_config_lock(void *arg);
void vexpress_config_unlock(void *arg);

int vexpress_config_get_topo(struct device_node *node, u32 *site,
		u32 *position, u32 *dcc);

/* Config bridge API */

struct vexpress_config_bridge_ops {
	struct regmap * (*regmap_init)(struct device *dev, void *context);
	void (*regmap_exit)(struct regmap *regmap, void *context);
};

struct device *vexpress_config_bridge_register(struct device *parent,
		struct vexpress_config_bridge_ops *ops, void *context);

/* Config regmap API */

struct regmap *devm_regmap_init_vexpress_config(struct device *dev);

/* Platform control */

unsigned int vexpress_get_mci_cardin(struct device *dev);
u32 vexpress_get_procid(int site);
void *vexpress_get_24mhz_clock_base(void);
void vexpress_flags_set(u32 data);

void vexpress_sysreg_early_init(void __iomem *base);
int vexpress_syscfg_device_register(struct platform_device *pdev);

/* Clocks */

void vexpress_clk_init(void __iomem *sp810_base);

#endif
