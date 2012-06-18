/*
 * include/linux/drv_display.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef _DRV_DISPLAY_COMMON_H_
#define _DRV_DISPLAY_COMMON_H_

#if CONFIG_CHIP_ID == 1120

#include <linux/drv_display_sun3i.h>

#elif CONFIG_CHIP_ID == 1123

#include <linux/drv_display_sun4i.h>

#elif CONFIG_CHIP_ID == 1125

#include <linux/drv_display_sun5i.h>

#else

#error "no chip id defined"

#endif



#endif



