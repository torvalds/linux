/*
 * Platform data structure for g762 fan controller driver
 *
 * Copyright (C) 2013, Arnaud EBALARD <arno@natisbad.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __LINUX_PLATFORM_DATA_G762_H__
#define __LINUX_PLATFORM_DATA_G762_H__

/*
 * Following structure can be used to set g762 driver platform specific data
 * during board init. Note that passing a sparse structure is possible but
 * will result in non-specified attributes to be set to default value, hence
 * overloading those installed during boot (e.g. by u-boot).
 */

struct g762_platform_data {
	u32 fan_startv;
	u32 fan_gear_mode;
	u32 pwm_polarity;
	u32 clk_freq;
};

#endif /* __LINUX_PLATFORM_DATA_G762_H__ */
