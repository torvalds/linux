/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * include/linux/input/ad714x.h
 *
 * AD714x is very flexible, it can be used as buttons, scrollwheel,
 * slider, touchpad at the same time. That depends on the boards.
 * The platform_data for the device's "struct device" holds this
 * information.
 *
 * Copyright 2009-2011 Analog Devices Inc.
 */

#ifndef __LINUX_INPUT_AD714X_H__
#define __LINUX_INPUT_AD714X_H__

#define STAGE_NUM              12
#define STAGE_CFGREG_NUM       8
#define SYS_CFGREG_NUM         8

/* board information which need be initialized in arch/mach... */
struct ad714x_slider_plat {
	int start_stage;
	int end_stage;
	int max_coord;
};

struct ad714x_wheel_plat {
	int start_stage;
	int end_stage;
	int max_coord;
};

struct ad714x_touchpad_plat {
	int x_start_stage;
	int x_end_stage;
	int x_max_coord;

	int y_start_stage;
	int y_end_stage;
	int y_max_coord;
};

struct ad714x_button_plat {
	int keycode;
	unsigned short l_mask;
	unsigned short h_mask;
};

struct ad714x_platform_data {
	int slider_num;
	int wheel_num;
	int touchpad_num;
	int button_num;
	struct ad714x_slider_plat *slider;
	struct ad714x_wheel_plat *wheel;
	struct ad714x_touchpad_plat *touchpad;
	struct ad714x_button_plat *button;
	unsigned short stage_cfg_reg[STAGE_NUM][STAGE_CFGREG_NUM];
	unsigned short sys_cfg_reg[SYS_CFGREG_NUM];
	unsigned long irqflags;
};

#endif
