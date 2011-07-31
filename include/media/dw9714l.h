/*
 * Copyright (C) 2010 Motorola, Inc.
 *
 * Contributors:
 *      Andrei Warkentin <andreiw@motorola.com>
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

#ifndef __DW9714L_H__
#define __DW9714L_H__

#include <linux/ioctl.h>  /* For IOCTL macros */

#define DW9714L_IOCTL_GET_CONFIG   _IOR('o', 1, struct dw9714l_config)
#define DW9714L_IOCTL_SET_CAL      _IOW('o', 2, struct dw9714l_cal)
#define DW9714L_IOCTL_SET_POSITION _IOW('o', 3, u32)

enum dw9714l_mode {
	MODE_DIRECT,
	MODE_LSC,
	MODE_DLC,
	MODE_INVALID
};

struct dw9714l_config
{
	__u32 settle_time;
	float focal_length;
	float fnumber;
	__u32 pos_low;
	__u32 pos_high;
	enum dw9714l_mode mode;
};

struct dw9714l_cal
{
	enum dw9714l_mode mode;
};

#endif  /* __DW9714L_H__ */

