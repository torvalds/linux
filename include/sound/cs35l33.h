/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/sound/cs35l33.h -- Platform data for CS35l33
 *
 * Copyright (c) 2016 Cirrus Logic Inc.
 */

#ifndef __CS35L33_H
#define __CS35L33_H

struct cs35l33_hg {
	bool enable_hg_algo;
	unsigned int mem_depth;
	unsigned int release_rate;
	unsigned int hd_rm;
	unsigned int ldo_thld;
	unsigned int ldo_path_disable;
	unsigned int ldo_entry_delay;
	bool vp_hg_auto;
	unsigned int vp_hg;
	unsigned int vp_hg_rate;
	unsigned int vp_hg_va;
};

struct cs35l33_pdata {
	/* Boost Controller Voltage Setting */
	unsigned int boost_ctl;

	/* Boost Controller Peak Current */
	unsigned int boost_ipk;

	/* Amplifier Drive Select */
	unsigned int amp_drv_sel;

	/* soft volume ramp */
	unsigned int ramp_rate;

	/* IMON adc scale */
	unsigned int imon_adc_scale;

	/* H/G algo configuration */
	struct cs35l33_hg hg_config;
};

#endif /* __CS35L33_H */
