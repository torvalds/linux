/*
 * Copyright (C) 2011 ST-Ericsson SA.
 * Copyright (C) 2009 Motorola, Inc.
 *
 * License Terms: GNU General Public License v2
 *
 * Simple driver for National Semiconductor LM35330 Backlight driver chip
 *
 * Author: Shreshtha Kumar SAHU <shreshthakumar.sahu@stericsson.com>
 * based on leds-lm3530.c by Dan Murphy <D.Murphy@motorola.com>
 */

#ifndef _LINUX_LED_LM3530_H__
#define _LINUX_LED_LM3530_H__

#define LM3530_FS_CURR_5mA		(0) /* Full Scale Current */
#define LM3530_FS_CURR_8mA		(1)
#define LM3530_FS_CURR_12mA		(2)
#define LM3530_FS_CURR_15mA		(3)
#define LM3530_FS_CURR_19mA		(4)
#define LM3530_FS_CURR_22mA		(5)
#define LM3530_FS_CURR_26mA		(6)
#define LM3530_FS_CURR_29mA		(7)

#define LM3530_ALS_AVRG_TIME_32ms	(0) /* ALS Averaging Time */
#define LM3530_ALS_AVRG_TIME_64ms	(1)
#define LM3530_ALS_AVRG_TIME_128ms	(2)
#define LM3530_ALS_AVRG_TIME_256ms	(3)
#define LM3530_ALS_AVRG_TIME_512ms	(4)
#define LM3530_ALS_AVRG_TIME_1024ms	(5)
#define LM3530_ALS_AVRG_TIME_2048ms	(6)
#define LM3530_ALS_AVRG_TIME_4096ms	(7)

#define LM3530_RAMP_TIME_1ms		(0) /* Brigtness Ramp Time */
#define LM3530_RAMP_TIME_130ms		(1) /* Max to 0 and vice versa */
#define LM3530_RAMP_TIME_260ms		(2)
#define LM3530_RAMP_TIME_520ms		(3)
#define LM3530_RAMP_TIME_1s		(4)
#define LM3530_RAMP_TIME_2s		(5)
#define LM3530_RAMP_TIME_4s		(6)
#define LM3530_RAMP_TIME_8s		(7)

/* ALS Resistor Select */
#define LM3530_ALS_IMPD_Z		(0x00) /* ALS Impedance */
#define LM3530_ALS_IMPD_13_53kOhm	(0x01)
#define LM3530_ALS_IMPD_9_01kOhm	(0x02)
#define LM3530_ALS_IMPD_5_41kOhm	(0x03)
#define LM3530_ALS_IMPD_2_27kOhm	(0x04)
#define LM3530_ALS_IMPD_1_94kOhm	(0x05)
#define LM3530_ALS_IMPD_1_81kOhm	(0x06)
#define LM3530_ALS_IMPD_1_6kOhm		(0x07)
#define LM3530_ALS_IMPD_1_138kOhm	(0x08)
#define LM3530_ALS_IMPD_1_05kOhm	(0x09)
#define LM3530_ALS_IMPD_1_011kOhm	(0x0A)
#define LM3530_ALS_IMPD_941Ohm		(0x0B)
#define LM3530_ALS_IMPD_759Ohm		(0x0C)
#define LM3530_ALS_IMPD_719Ohm		(0x0D)
#define LM3530_ALS_IMPD_700Ohm		(0x0E)
#define LM3530_ALS_IMPD_667Ohm		(0x0F)

enum lm3530_mode {
	LM3530_BL_MODE_MANUAL = 0,	/* "man" */
	LM3530_BL_MODE_ALS,		/* "als" */
	LM3530_BL_MODE_PWM,		/* "pwm" */
};

/* ALS input select */
enum lm3530_als_mode {
	LM3530_INPUT_AVRG = 0,	/* ALS1 and ALS2 input average */
	LM3530_INPUT_ALS1,	/* ALS1 Input */
	LM3530_INPUT_ALS2,	/* ALS2 Input */
	LM3530_INPUT_CEIL,	/* Max of ALS1 and ALS2 */
};

/* PWM Platform Specific Data */
struct lm3530_pwm_data {
	void (*pwm_set_intensity) (int brightness, int max_brightness);
	int (*pwm_get_intensity) (int max_brightness);
};

/**
 * struct lm3530_platform_data
 * @mode: mode of operation i.e. Manual, ALS or PWM
 * @als_input_mode: select source of ALS input - ALS1/2 or average
 * @max_current: full scale LED current
 * @pwm_pol_hi: PWM input polarity - active high/active low
 * @als_avrg_time: ALS input averaging time
 * @brt_ramp_law: brightness mapping mode - exponential/linear
 * @brt_ramp_fall: rate of fall of led current
 * @brt_ramp_rise: rate of rise of led current
 * @als1_resistor_sel: internal resistance from ALS1 input to ground
 * @als2_resistor_sel: internal resistance from ALS2 input to ground
 * @als_vmin: als input voltage calibrated for max brightness in mV
 * @als_vmax: als input voltage calibrated for min brightness in mV
 * @brt_val: brightness value (0-255)
 * @pwm_data: PWM control functions (only valid when the mode is PWM)
 */
struct lm3530_platform_data {
	enum lm3530_mode mode;
	enum lm3530_als_mode als_input_mode;

	u8 max_current;
	bool pwm_pol_hi;
	u8 als_avrg_time;

	bool brt_ramp_law;
	u8 brt_ramp_fall;
	u8 brt_ramp_rise;

	u8 als1_resistor_sel;
	u8 als2_resistor_sel;

	u32 als_vmin;
	u32 als_vmax;

	u8 brt_val;

	struct lm3530_pwm_data pwm_data;
};

#endif	/* _LINUX_LED_LM3530_H__ */
