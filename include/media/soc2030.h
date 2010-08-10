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

#ifndef __SOC2030_H__
#define __SOC2030_H__

#include <linux/ioctl.h>  /* For IOCTL macros */

#define SOC2030_IOCTL_SET_MODE		_IOWR('o', 1, struct soc2030_mode)
#define SOC2030_IOCTL_GET_STATUS	 _IOC(_IOC_READ, 'o', 2, 10)


struct soc2030_mode {
	int xres;
	int yres;
};

#ifdef __KERNEL__
struct soc2030_platform_data {
	int (*power_on)(void);
	int (*power_off)(void);

};
#endif /* __KERNEL__ */

#endif  /* __SOC2030_H__ */

