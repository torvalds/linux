/*
 * Platform device data for Xilinx Framebuffer device
 *
 * Copyright 2007 Secret Lab Technologies Ltd.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __XILINXFB_H__
#define __XILINXFB_H__

#include <linux/types.h>

/* ML300/403 reference design framebuffer driver platform data struct */
struct xilinxfb_platform_data {
	u32 rotate_screen;
	u32 screen_height_mm;
	u32 screen_width_mm;
};

#endif  /* __XILINXFB_H__ */
