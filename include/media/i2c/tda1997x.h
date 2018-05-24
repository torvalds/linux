/* SPDX-License-Identifier: GPL-2.0 */
/*
 * tda1997x - NXP HDMI receiver
 *
 * Copyright 2017 Tim Harvey <tharvey@gateworks.com>
 *
 */

#ifndef _TDA1997X_
#define _TDA1997X_

/* Platform Data */
struct tda1997x_platform_data {
	enum v4l2_mbus_type vidout_bus_type;
	u32 vidout_bus_width;
	u8 vidout_port_cfg[9];
	/* pin polarity (1=invert) */
	bool vidout_inv_de;
	bool vidout_inv_hs;
	bool vidout_inv_vs;
	bool vidout_inv_pclk;
	/* clock delays (0=-8, 1=-7 ... 15=+7 pixels) */
	u8 vidout_delay_hs;
	u8 vidout_delay_vs;
	u8 vidout_delay_de;
	u8 vidout_delay_pclk;
	/* sync selections (controls how sync pins are derived) */
	u8 vidout_sel_hs;
	u8 vidout_sel_vs;
	u8 vidout_sel_de;

	/* Audio Port Output */
	int audout_format;
	u32 audout_mclk_fs;	/* clock multiplier */
	u32 audout_width;	/* 13 or 32 bit */
	u32 audout_layout;	/* layout0=AP0 layout1=AP0,AP1,AP2,AP3 */
	bool audout_layoutauto;	/* audio layout dictated by pkt header */
	bool audout_invert_clk;	/* data valid on rising edge of BCLK */
	bool audio_auto_mute;	/* enable hardware audio auto-mute */
};

#endif
