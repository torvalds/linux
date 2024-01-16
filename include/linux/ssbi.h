/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2010 Google, Inc.
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 * Author: Dima Zavin <dima@android.com>
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
