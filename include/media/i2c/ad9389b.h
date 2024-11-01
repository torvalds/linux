/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Analog Devices AD9389B/AD9889B video encoder driver header
 *
 * Copyright 2012 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef AD9389B_H
#define AD9389B_H

enum ad9389b_tmds_pll_gear {
	AD9389B_TMDS_PLL_GEAR_AUTOMATIC,
	AD9389B_TMDS_PLL_GEAR_SEMI_AUTOMATIC,
};

/* Platform dependent definitions */
struct ad9389b_platform_data {
	enum ad9389b_tmds_pll_gear tmds_pll_gear ;
	/* Differential Data/Clock Output Drive Strength (reg. 0xa2/0xa3) */
	u8 diff_data_drive_strength;
	u8 diff_clk_drive_strength;
};

/* notify events */
#define AD9389B_MONITOR_DETECT 0
#define AD9389B_EDID_DETECT 1

struct ad9389b_monitor_detect {
	int present;
};

struct ad9389b_edid_detect {
	int present;
	int segment;
};

#endif
