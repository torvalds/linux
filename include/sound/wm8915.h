/*
 * linux/sound/wm8915.h -- Platform data for WM8915
 *
 * Copyright 2011 Wolfson Microelectronics. PLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_SND_WM8903_H
#define __LINUX_SND_WM8903_H

enum wm8915_inmode {
	WM8915_DIFFERRENTIAL_1 = 0,   /* IN1xP - IN1xN */
	WM8915_INVERTING = 1,         /* IN1xN */
	WM8915_NON_INVERTING = 2,     /* IN1xP */
	WM8915_DIFFERENTIAL_2 = 3,    /* IN2xP - IN2xP */
};

/**
 * ReTune Mobile configurations are specified with a label, sample
 * rate and set of values to write (the enable bits will be ignored).
 *
 * Configurations are expected to be generated using the ReTune Mobile
 * control panel in WISCE - see http://www.wolfsonmicro.com/wisce/
 */
struct wm8915_retune_mobile_config {
	const char *name;
	int rate;
	u16 regs[20];
};

#define WM8915_SET_DEFAULT 0x10000

struct wm8915_pdata {
	int irq_flags;  /** Set IRQ trigger flags; default active low */

	int ldo_ena;  /** GPIO for LDO1; -1 for none */

	int micdet_def;  /** Default MICDET_SRC/HP1FB_SRC/MICD_BIAS */

	enum wm8915_inmode inl_mode;
	enum wm8915_inmode inr_mode;

	u32 spkmute_seq;  /** Value for register 0x802 */

	int gpio_base;
	u32 gpio_default[5];

	int num_retune_mobile_cfgs;
	struct wm8915_retune_mobile_config *retune_mobile_cfgs;
};

#endif
