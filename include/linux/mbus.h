/*
 * Marvell MBUS common definitions.
 *
 * Copyright (C) 2008 Marvell Semiconductor
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __LINUX_MBUS_H
#define __LINUX_MBUS_H

struct mbus_dram_target_info
{
	/*
	 * The 4-bit MBUS target ID of the DRAM controller.
	 */
	u8		mbus_dram_target_id;

	/*
	 * The base address, size, and MBUS attribute ID for each
	 * of the possible DRAM chip selects.  Peripherals are
	 * required to support at least 4 decode windows.
	 */
	int		num_cs;
	struct mbus_dram_window {
		u8	cs_index;
		u8	mbus_attr;
		u32	base;
		u32	size;
	} cs[4];
};


#endif
