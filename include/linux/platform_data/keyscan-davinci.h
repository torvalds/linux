/*
 * Copyright (C) 2009 Texas Instruments, Inc
 *
 * Author: Miguel Aguilar <miguel.aguilar@ridgerun.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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

