/*
 * Samsung SoC DP device support
 *
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * Author: Jingoo Han <jg1.han@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _S5P_DP_H
#define _S5P_DP_H

#define DP_TIMEOUT_LOOP_COUNT 100
#define MAX_CR_LOOP 5
#define MAX_EQ_LOOP 4

enum link_rate_type {
	LINK_RATE_1_62GBPS = 0x06,
	LINK_RATE_2_70GBPS = 0x0a
};

enum link_lane_count_type {
	LANE_COUNT1 = 1,
	LANE_COUNT2 = 2,
	LANE_COUNT4 = 4
};

enum link_training_state {
	START,
	CLOCK_RECOVERY,
	EQUALIZER_TRAINING,
	FINISHED,
	FAILED
};

enum voltage_swing_level {
	VOLTAGE_LEVEL_0,
	VOLTAGE_LEVEL_1,
	VOLTAGE_LEVEL_2,
	VOLTAGE_LEVEL_3,
};

enum pre_emphasis_level {
	PRE_EMPHASIS_LEVEL_0,
	PRE_EMPHASIS_LEVEL_1,
	PRE_EMPHASIS_LEVEL_2,
	PRE_EMPHASIS_LEVEL_3,
};

enum pattern_set {
	PRBS7,
	D10_2,
	TRAINING_PTN1,
	TRAINING_PTN2,
	DP_NONE
};

enum color_space {
	COLOR_RGB,
	COLOR_YCBCR422,
	COLOR_YCBCR444
};

enum color_depth {
	COLOR_6,
	COLOR_8,
	COLOR_10,
	COLOR_12
};

enum color_coefficient {
	COLOR_YCBCR601,
	COLOR_YCBCR709
};

enum dynamic_range {
	VESA,
	CEA
};

enum pll_status {
	PLL_UNLOCKED,
	PLL_LOCKED
};

enum clock_recovery_m_value_type {
	CALCULATED_M,
	REGISTER_M
};

enum video_timing_recognition_type {
	VIDEO_TIMING_FROM_CAPTURE,
	VIDEO_TIMING_FROM_REGISTER
};

enum analog_power_block {
	AUX_BLOCK,
	CH0_BLOCK,
	CH1_BLOCK,
	CH2_BLOCK,
	CH3_BLOCK,
	ANALOG_TOTAL,
	POWER_ALL
};

enum aux_tx_terminal_resistor {
	AUX_TX_37_5_OHM,
	AUX_TX_45_OHM,
	AUX_TX_50_OHM,
	AUX_TX_65_OHM
};

struct video_info {
	char *name;

	bool h_sync_polarity;
	bool v_sync_polarity;
	bool interlaced;

	enum color_space color_space;
	enum dynamic_range dynamic_range;
	enum color_coefficient ycbcr_coeff;
	enum color_depth color_depth;

	enum link_rate_type link_rate;
	enum link_lane_count_type lane_count;
};

struct analog_param {
	enum aux_tx_terminal_resistor aux_tx_terminal_resistor;
	unsigned int tx_amplitude;
};

struct s5p_dp_platdata {
	struct video_info *video_info;
	struct analog_param *analog_param;

	void (*phy_init)(void);
	void (*phy_exit)(void);
	void (*backlight_on)(void);
	void (*backlight_off)(void);
	void (*lcd_on)(void);
	void (*lcd_off)(void);
};

#endif /* _S5P_DP_H */
