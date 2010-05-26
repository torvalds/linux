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

#ifndef _LINUX_MAX9635_H__
#define _LINUX_MAX9635_H__

#define MAX9635_NAME "MAX9635_als"
#define FOPS_MAX9635_NAME "MAX9635"


#ifdef __KERNEL__

struct max9635_als_zone_data {
	int  als_lower_threshold;
	int  als_higher_threshold;
};

struct max9635_platform_data {
	u8	configure;
	u8	threshold_timer;
	u8	def_low_threshold;
	u8	def_high_threshold;
	u32 lens_percent_t;
	struct max9635_als_zone_data *als_lux_table;
	u8  num_of_zones;
	int (*power_on)(void);
	int (*power_off)(void);
};

#endif	/* __KERNEL__ */

#define MAX9635_IO			0xA3

#define MAX9635_IOCTL_GET_ENABLE	_IOR(MAX9635_IO, 0x00, char)
#define MAX9635_IOCTL_SET_ENABLE	_IOW(MAX9635_IO, 0x01, char)

#endif	/* _LINUX_MAX9635_H__ */
