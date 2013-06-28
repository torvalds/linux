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

#define VEXPRESS_SITE_MB		0
#define VEXPRESS_SITE_DB1		1
#define VEXPRESS_SITE_DB2		2
#define VEXPRESS_SITE_MASTER		0xf

#define VEXPRESS_CONFIG_STATUS_DONE	0
#define VEXPRESS_CONFIG_STATUS_WAIT	1

#define VEXPRESS_GPIO_MMC_CARDIN	0
#define VEXPRESS_GPIO_MMC_WPROT		1
#define VEXPRESS_GPIO_FLASH_WPn		2
#define VEXPRESS_GPIO_LED0		3
#define VEXPRESS_GPIO_LED1		4
#define VEXPRESS_GPIO_LED2		5
#define VEXPRESS_GPIO_LED3		6
#define VEXPRESS_GPIO_LED4		7
#define VEXPRESS_GPIO_LED5		8
#define VEXPRESS_GPIO_LED6		9
#define VEXPRESS_GPIO_LED7		10

#define VEXPRESS_RES_FUNC(_site, _func)	\
{					\
	.start = (_site),		\
	.end = (_func),			\
	.flags = IORESOURCE_BUS,	\
}

/* Config bridge API */

/**
 * struct vexpress_config_bridge_info - description of the platform
 * configuration infrastructure bridge.
 *
 * @name:	Bridge name
 *
 * @func_get:	Obtains pointer to a configuration function for a given
 *		device or a Device Tree node, to be used with @func_put
 *		and @func_exec. The node pointer should take precedence
 *		over device pointer when both are passed.
 *
 * @func_put:	Tells the bridge that the function will not be used any
 *		more, so all allocated resources can be released.
 *
 * @func_exec:	Executes a configuration function read or write operation.
 *		The offset selects a 32 bit word of the value accessed.
 *		Must return VEXPRESS_CONFIG_STATUS_DONE when operation
 *		is finished immediately, VEXPRESS_CONFIG_STATUS_WAIT when
 *		will be completed in some time or negative value in case
 *		of error.
 */
struct vexpress_config_bridge_info {
	const char *name;
	void *(*func_get)(struct device *dev, struct device_node *node);
	void (*func_put)(void *func);
	int (*func_exec)(void *func, int offset, bool write, u32 *data);
};

struct vexpress_config_bridge;

struct vexpress_config_bridge *vexpress_config_bridge_register(
		struct device_node *node,
		struct vexpress_config_bridge_info *info);
void vexpress_config_bridge_unregister(struct vexpress_config_bridge *bridge);

void vexpress_config_complete(struct vexpress_config_bridge *bridge,
		int status);

/* Config function API */

struct vexpress_config_func;

struct vexpress_config_func *__vexpress_config_func_get(struct device *dev,
		struct device_node *node);
#define vexpress_config_func_get_by_dev(dev) \
		__vexpress_config_func_get(dev, NULL)
#define vexpress_config_func_get_by_node(node) \
		__vexpress_config_func_get(NULL, node)
void vexpress_config_func_put(struct vexpress_config_func *func);

/* Both may sleep! */
int vexpress_config_read(struct vexpress_config_func *func, int offset,
		u32 *data);
int vexpress_config_write(struct vexpress_config_func *func, int offset,
		u32 data);

/* Platform control */

u32 vexpress_get_procid(int site);
u32 vexpress_get_hbi(int site);
void *vexpress_get_24mhz_clock_base(void);
void vexpress_flags_set(u32 data);

#define vexpress_get_site_by_node(node) __vexpress_get_site(NULL, node)
#define vexpress_get_site_by_dev(dev) __vexpress_get_site(dev, NULL)
unsigned __vexpress_get_site(struct device *dev, struct device_node *node);

void vexpress_sysreg_early_init(void __iomem *base);
void vexpress_sysreg_of_early_init(void);

/* Clocks */

struct clk *vexpress_osc_setup(struct device *dev);
void vexpress_osc_of_setup(struct device_node *node);

void vexpress_clk_init(void __iomem *sp810_base);
void vexpress_clk_of_init(void);

#endif
