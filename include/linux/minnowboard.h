/*
 * MinnowBoard Linux platform driver
 * Copyright (c) 2013, Intel Corporation.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Author: Darren Hart <dvhart@linux.intel.com>
 */

#ifndef _LINUX_MINNOWBOARD_H
#define _LINUX_MINNOWBOARD_H

#if defined(CONFIG_MINNOWBOARD) || defined(CONFIG_MINNOWBOARD_MODULE)
bool minnow_detect(void);
bool minnow_lvds_detect(void);
int minnow_hwid(void);
void minnow_phy_reset(void);
#else
#define minnow_detect() (false)
#define minnow_lvds_detect() (false)
#define minnow_hwid() (-1)
#define minnow_phy_reset() do { } while (0)
#endif /* MINNOWBOARD */

#endif /* _LINUX_MINNOWBOARD_H */
