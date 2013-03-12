/* Copyright (C) 2010 Google, Inc.
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 * Author: Dima Zavin <dima@android.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _LINUX_MSM_SSBI_H
#define _LINUX_MSM_SSBI_H

#include <linux/types.h>

struct msm_ssbi_slave_info {
	const char	*name;
	void		*platform_data;
};

enum msm_ssbi_controller_type {
	MSM_SBI_CTRL_SSBI = 0,
	MSM_SBI_CTRL_SSBI2,
	MSM_SBI_CTRL_PMIC_ARBITER,
};

struct msm_ssbi_platform_data {
	struct msm_ssbi_slave_info	slave;
	enum msm_ssbi_controller_type controller_type;
};

#ifdef CONFIG_MSM_SSBI
int msm_ssbi_write(struct device *dev, u16 addr, u8 *buf, int len);
int msm_ssbi_read(struct device *dev, u16 addr, u8 *buf, int len);
#else
static inline int msm_ssbi_write(struct device *dev, u16 addr, u8 *buf, int len)
{
	return -ENXIO;
}
static inline int msm_ssbi_read(struct device *dev, u16 addr, u8 *buf, int len)
{
	return -ENXIO;
}
#endif
#endif
