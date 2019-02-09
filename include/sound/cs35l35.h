/*
 * linux/sound/cs35l35.h -- Platform data for CS35l35
 *
 * Copyright (c) 2016 Cirrus Logic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __CS35L35_H
#define __CS35L35_H

struct classh_cfg {
	/*
	 * Class H Algorithm Control Variables
	 * You can either have it done
	 * automatically or you can adjust
	 * these variables for tuning
	 *
	 * if you do not enable the internal algorithm
	 * you will get a set of mixer controls for
	 * Class H tuning
	 *
	 * Section 4.3 of the datasheet
	 */
	bool classh_bst_override;
	bool classh_algo_enable;
	int classh_bst_max_limit;
	int classh_mem_depth;
	int classh_release_rate;
	int classh_headroom;
	int classh_wk_fet_disable;
	int classh_wk_fet_delay;
	int classh_wk_fet_thld;
	int classh_vpch_auto;
	int classh_vpch_rate;
	int classh_vpch_man;
};

struct monitor_cfg {
	/*
	 * Signal Monitor Data
	 * highly configurable signal monitoring
	 * data positioning and different types of
	 * monitoring data.
	 *
	 * Section 4.8.2 - 4.8.4 of the datasheet
	 */
	bool is_present;
	bool imon_specs;
	bool vmon_specs;
	bool vpmon_specs;
	bool vbstmon_specs;
	bool vpbrstat_specs;
	bool zerofill_specs;
	u8 imon_dpth;
	u8 imon_loc;
	u8 imon_frm;
	u8 imon_scale;
	u8 vmon_dpth;
	u8 vmon_loc;
	u8 vmon_frm;
	u8 vpmon_dpth;
	u8 vpmon_loc;
	u8 vpmon_frm;
	u8 vbstmon_dpth;
	u8 vbstmon_loc;
	u8 vbstmon_frm;
	u8 vpbrstat_dpth;
	u8 vpbrstat_loc;
	u8 vpbrstat_frm;
	u8 zerofill_dpth;
	u8 zerofill_loc;
	u8 zerofill_frm;
};

struct cs35l35_platform_data {

	/* Stereo (2 Device) */
	bool stereo;
	/* serial port drive strength */
	int sp_drv_str;
	/* serial port drive in unused slots */
	int sp_drv_unused;
	/* Boost Power Down with FET */
	bool bst_pdn_fet_on;
	/* Boost Voltage : used if ClassH Algo Enabled */
	int bst_vctl;
	/* Boost Converter Peak Current CTRL */
	int bst_ipk;
	/* Amp Gain Zero Cross */
	bool gain_zc;
	/* Audio Input Location */
	int aud_channel;
	/* Advisory Input Location */
	int adv_channel;
	/* Shared Boost for stereo */
	bool shared_bst;
	/* Specifies this amp is using an external boost supply */
	bool ext_bst;
	/* Inductor Value */
	int boost_ind;
	/* ClassH Algorithm */
	struct classh_cfg classh_algo;
	/* Monitor Config */
	struct monitor_cfg mon_cfg;
};

#endif /* __CS35L35_H */
