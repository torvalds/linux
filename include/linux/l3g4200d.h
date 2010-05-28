/*
 * Copyright (C) 2010 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#ifndef __L3G4200D_H__
#define __L3G4200D_H__

#include <linux/ioctl.h>  /* For IOCTL macros */

#define L3G4200D_NAME	"l3g4200d"

#define L3G4200D_IOCTL_BASE 77
/** The following define the IOCTL command values via the ioctl macros */
#define L3G4200D_IOCTL_SET_DELAY	_IOW(L3G4200D_IOCTL_BASE, 0, int)
#define L3G4200D_IOCTL_GET_DELAY	_IOR(L3G4200D_IOCTL_BASE, 1, int)
#define L3G4200D_IOCTL_SET_ENABLE	_IOW(L3G4200D_IOCTL_BASE, 2, int)
#define L3G4200D_IOCTL_GET_ENABLE	_IOR(L3G4200D_IOCTL_BASE, 3, int)

#ifdef __KERNEL__

struct l3g4200d_platform_data {
	int poll_interval;
	int min_interval;

	u8 ctrl_reg_1;
	u8 ctrl_reg_2;
	u8 ctrl_reg_3;
	u8 ctrl_reg_4;
	u8 ctrl_reg_5;

	u8 int_config;
	u8 int_source;

	u8 int_th_x_h;
	u8 int_th_x_l;
	u8 int_th_y_h;
	u8 int_th_y_l;
	u8 int_th_z_h;
	u8 int_th_z_l;
	u8 int_duration;

	u8 g_range;

	u8 axis_map_x;
	u8 axis_map_y;
	u8 axis_map_z;

	u8 negate_x;
	u8 negate_y;
	u8 negate_z;

	void (*exit)(void);
	int (*power_on)(void);
	int (*power_off)(void);

};
#endif /* __KERNEL__ */

#endif  /* __L3G4200D_H__ */

