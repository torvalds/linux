/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/sound/wm8996.h -- Platform data for WM8996
 *
 * Copyright 2011 Wolfson Microelectronics. PLC.
 */

#ifndef __LINUX_SND_WM8996_H
#define __LINUX_SND_WM8996_H

enum wm8996_inmode {
	WM8996_DIFFERRENTIAL_1 = 0,   /* IN1xP - IN1xN */
	WM8996_INVERTING = 1,         /* IN1xN */
	WM8996_NON_INVERTING = 2,     /* IN1xP */
	WM8996_DIFFERENTIAL_2 = 3,    /* IN2xP - IN2xP */
};

/**
 * ReTune Mobile configurations are specified with a label, sample
 * rate and set of values to write (the enable bits will be ignored).
 *
 * Configurations are expected to be generated using the ReTune Mobile
 * control panel in WISCE - see http://www.wolfsonmicro.com/wisce/
 */
struct wm8996_retune_mobile_config {
	const char *name;
	int rate;
	u16 regs[20];
};

#define WM8996_SET_DEFAULT 0x10000

struct wm8996_pdata {
	int irq_flags;  /** Set IRQ trigger flags; default active low */

	int micdet_def;  /** Default MICDET_SRC/HP1FB_SRC/MICD_BIAS */

	enum wm8996_inmode inl_mode;
	enum wm8996_inmode inr_mode;

	u32 spkmute_seq;  /** Value for register 0x802 */

	u32 gpio_default[5];

	int num_retune_mobile_cfgs;
	struct wm8996_retune_mobile_config *retune_mobile_cfgs;
};

#endif
