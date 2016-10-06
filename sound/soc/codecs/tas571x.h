/*
 * TAS571x amplifier audio driver
 *
 * Copyright (C) 2015 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _TAS571X_H
#define _TAS571X_H

/* device registers */
#define TAS571X_CLK_CTRL_REG		0x00
#define TAS571X_DEV_ID_REG		0x01
#define TAS571X_ERR_STATUS_REG		0x02
#define TAS571X_SYS_CTRL_1_REG		0x03
#define TAS571X_SDI_REG			0x04
#define TAS571X_SDI_FMT_MASK		0x0f

#define TAS571X_SYS_CTRL_2_REG		0x05
#define TAS571X_SYS_CTRL_2_SDN_MASK	0x40

#define TAS571X_SOFT_MUTE_REG		0x06
#define TAS571X_SOFT_MUTE_CH1_SHIFT	0
#define TAS571X_SOFT_MUTE_CH2_SHIFT	1
#define TAS571X_SOFT_MUTE_CH3_SHIFT	2

#define TAS571X_MVOL_REG		0x07
#define TAS571X_CH1_VOL_REG		0x08
#define TAS571X_CH2_VOL_REG		0x09
#define TAS571X_CH3_VOL_REG		0x0a
#define TAS571X_VOL_CFG_REG		0x0e
#define TAS571X_MODULATION_LIMIT_REG	0x10
#define TAS571X_IC_DELAY_CH1_REG	0x11
#define TAS571X_IC_DELAY_CH2_REG	0x12
#define TAS571X_IC_DELAY_CH3_REG	0x13
#define TAS571X_IC_DELAY_CH4_REG	0x14

#define TAS571X_PWM_CH_SDN_GROUP_REG	0x19	/* N/A on TAS5717, TAS5719 */
#define TAS571X_PWM_CH1_SDN_MASK	(1<<0)
#define TAS571X_PWM_CH2_SDN_SHIFT	(1<<1)
#define TAS571X_PWM_CH3_SDN_SHIFT	(1<<2)
#define TAS571X_PWM_CH4_SDN_SHIFT	(1<<3)

#define TAS571X_START_STOP_PERIOD_REG	0x1a
#define TAS571X_OSC_TRIM_REG		0x1b
#define TAS571X_BKND_ERR_REG		0x1c
#define TAS571X_INPUT_MUX_REG		0x20
#define TAS571X_CH4_SRC_SELECT_REG	0x21
#define TAS571X_PWM_MUX_REG		0x25

/* 20-byte biquad registers */
#define TAS5717_CH1_BQ0_REG		0x26
#define TAS5717_CH1_BQ1_REG		0x27
#define TAS5717_CH1_BQ2_REG		0x28
#define TAS5717_CH1_BQ3_REG		0x29
#define TAS5717_CH1_BQ4_REG		0x2a
#define TAS5717_CH1_BQ5_REG		0x2b
#define TAS5717_CH1_BQ6_REG		0x2c
#define TAS5717_CH1_BQ7_REG		0x2d
#define TAS5717_CH1_BQ8_REG		0x2e
#define TAS5717_CH1_BQ9_REG		0x2f

#define TAS5717_CH2_BQ0_REG		0x30
#define TAS5717_CH2_BQ1_REG		0x31
#define TAS5717_CH2_BQ2_REG		0x32
#define TAS5717_CH2_BQ3_REG		0x33
#define TAS5717_CH2_BQ4_REG		0x34
#define TAS5717_CH2_BQ5_REG		0x35
#define TAS5717_CH2_BQ6_REG		0x36
#define TAS5717_CH2_BQ7_REG		0x37
#define TAS5717_CH2_BQ8_REG		0x38
#define TAS5717_CH2_BQ9_REG		0x39

#define TAS5717_CH1_BQ10_REG		0x58
#define TAS5717_CH1_BQ11_REG		0x59

#define TAS5717_CH4_BQ0_REG		0x5a
#define TAS5717_CH4_BQ1_REG		0x5b

#define TAS5717_CH2_BQ10_REG		0x5c
#define TAS5717_CH2_BQ11_REG		0x5d

#define TAS5717_CH3_BQ0_REG		0x5e
#define TAS5717_CH3_BQ1_REG		0x5f

#define TAS5717_CH1_RIGHT_CH_MIX_REG	0x72
#define TAS5717_CH1_LEFT_CH_MIX_REG	0x73
#define TAS5717_CH2_LEFT_CH_MIX_REG	0x76
#define TAS5717_CH2_RIGHT_CH_MIX_REG	0x77

#endif /* _TAS571X_H */
