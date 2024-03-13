/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Platform data structure for g762 fan controller driver
 *
 * Copyright (C) 2013, Arnaud EBALARD <arno@natisbad.org>
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
