/*
 * OF helpers for External connector (extcon) framework
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 * Kishon Vijay Abraham I <kishon@ti.com>
 *
 * Copyright (C) 2013 Samsung Electronics
 * Chanwoo Choi <cw00.choi@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_OF_EXTCON_H
#define __LINUX_OF_EXTCON_H

#include <linux/err.h>

#if IS_ENABLED(CONFIG_OF_EXTCON)
extern struct extcon_dev
	*of_extcon_get_extcon_dev(struct device *dev, int index);
#else
static inline struct extcon_dev
	*of_extcon_get_extcon_dev(struct device *dev, int index)
{
	return ERR_PTR(-ENOSYS);
}
#endif /* CONFIG_OF_EXTCON */
#endif /* __LINUX_OF_EXTCON_H */
