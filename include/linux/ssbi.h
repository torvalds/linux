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

#ifndef _LINUX_SSBI_H
#define _LINUX_SSBI_H

#include <linux/types.h>

int ssbi_write(struct device *dev, u16 addr, const u8 *buf, int len);
int ssbi_read(struct device *dev, u16 addr, u8 *buf, int len);

static inline int
ssbi_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	int ret;
	u8 v;

	ret = ssbi_read(context, reg, &v, 1);
	if (!ret)
		*val = v;

	return ret;
}

static inline int
ssbi_reg_write(void *context, unsigned int reg, unsigned int val)
{
	u8 v = val;
	return ssbi_write(context, reg, &v, 1);
}

#endif
