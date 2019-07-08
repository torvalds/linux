/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2009 Texas Instruments, Inc
 *
 * Author: Miguel Aguilar <miguel.aguilar@ridgerun.com>
 */

#ifndef DAVINCI_KEYSCAN_H
#define DAVINCI_KEYSCAN_H

#include <linux/io.h>

enum davinci_matrix_types {
	DAVINCI_KEYSCAN_MATRIX_4X4,
	DAVINCI_KEYSCAN_MATRIX_5X3,
};

struct davinci_ks_platform_data {
	int		(*device_enable)(struct device *dev);
	unsigned short	*keymap;
	u32		keymapsize;
	u8		rep:1;
	u8		strobe;
	u8		interval;
	u8		matrix_type;
};

#endif

