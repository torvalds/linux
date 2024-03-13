/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Definitions and platform data for Analog Devices
 * Backlight drivers ADP8860
 *
 * Copyright 2009-2010 Analog Devices Inc.
 */

#ifndef __LINUX_I2C_ADP8860_H
#define __LINUX_I2C_ADP8860_H

#include <linux/leds.h>
#include <linux/types.h>

#define ID_ADP8860		8860

#define ADP8860_MAX_BRIGHTNESS	0x7F
#define FLAG_OFFT_SHIFT 8

/*
 * LEDs subdevice platform data
 */

#define ADP8860_LED_DIS_BLINK	(0 << FLAG_OFFT_SHIFT)
#define ADP8860_LED_OFFT_600ms	(1 << FLAG_OFFT_SHIFT)
#define ADP8860_LED_OFFT_1200ms	(2 << FLAG_OFFT_SHIFT)
#define ADP8860_LED_OFFT_1800ms	(3 << FLAG_OFFT_SHIFT)

#define ADP8860_LED_ONT_200ms	0
#define ADP8860_LED_ONT_600ms	1
#define ADP8860_LED_ONT_800ms	2
#define ADP8860_LED_ONT_1200ms	3

#define ADP8860_LED_D7		(7)
#define ADP8860_LED_D6		(6)
#define ADP8860_LED_D5		(5)
#define ADP8860_LED_D4		(4)
#define ADP8860_LED_D3		(3)
#define ADP8860_LED_D2		(2)
#define ADP8860_LED_D1		(1)

/*
 * Backlight subdevice platform data
 */

#define ADP8860_BL_D7		(1 << 6)
#define ADP8860_BL_D6		(1 << 5)
#define ADP8860_BL_D5		(1 << 4)
#define ADP8860_BL_D4		(1 << 3)
#define ADP8860_BL_D3		(1 << 2)
#define ADP8860_BL_D2		(1 << 1)
#define ADP8860_BL_D1		(1 << 0)

#define ADP8860_FADE_T_DIS	0	/* Fade Timer Disabled */
#define ADP8860_FADE_T_300ms	1	/* 0.3 Sec */
#define ADP8860_FADE_T_600ms	2
#define ADP8860_FADE_T_900ms	3
#define ADP8860_FADE_T_1200ms	4
#define ADP8860_FADE_T_1500ms	5
#define ADP8860_FADE_T_1800ms	6
#define ADP8860_FADE_T_2100ms	7
#define ADP8860_FADE_T_2400ms	8
#define ADP8860_FADE_T_2700ms	9
#define ADP8860_FADE_T_3000ms	10
#define ADP8860_FADE_T_3500ms	11
#define ADP8860_FADE_T_4000ms	12
#define ADP8860_FADE_T_4500ms	13
#define ADP8860_FADE_T_5000ms	14
#define ADP8860_FADE_T_5500ms	15	/* 5.5 Sec */

#define ADP8860_FADE_LAW_LINEAR	0
#define ADP8860_FADE_LAW_SQUARE	1
#define ADP8860_FADE_LAW_CUBIC1	2
#define ADP8860_FADE_LAW_CUBIC2	3

#define ADP8860_BL_AMBL_FILT_80ms	0	/* Light sensor filter time */
#define ADP8860_BL_AMBL_FILT_160ms	1
#define ADP8860_BL_AMBL_FILT_320ms	2
#define ADP8860_BL_AMBL_FILT_640ms	3
#define ADP8860_BL_AMBL_FILT_1280ms	4
#define ADP8860_BL_AMBL_FILT_2560ms	5
#define ADP8860_BL_AMBL_FILT_5120ms	6
#define ADP8860_BL_AMBL_FILT_10240ms	7	/* 10.24 sec */

/*
 * Blacklight current 0..30mA
 */
#define ADP8860_BL_CUR_mA(I)		((I * 127) / 30)

/*
 * L2 comparator current 0..1106uA
 */
#define ADP8860_L2_COMP_CURR_uA(I)	((I * 255) / 1106)

/*
 * L3 comparator current 0..138uA
 */
#define ADP8860_L3_COMP_CURR_uA(I)	((I * 255) / 138)

struct adp8860_backlight_platform_data {
	u8 bl_led_assign;	/* 1 = Backlight 0 = Individual LED */

	u8 bl_fade_in;		/* Backlight Fade-In Timer */
	u8 bl_fade_out;		/* Backlight Fade-Out Timer */
	u8 bl_fade_law;		/* fade-on/fade-off transfer characteristic */

	u8 en_ambl_sens;	/* 1 = enable ambient light sensor */
	u8 abml_filt;		/* Light sensor filter time */

	u8 l1_daylight_max;	/* use BL_CUR_mA(I) 0 <= I <= 30 mA */
	u8 l1_daylight_dim;	/* typ = 0, use BL_CUR_mA(I) 0 <= I <= 30 mA */
	u8 l2_office_max;	/* use BL_CUR_mA(I) 0 <= I <= 30 mA */
	u8 l2_office_dim;	/* typ = 0, use BL_CUR_mA(I) 0 <= I <= 30 mA */
	u8 l3_dark_max;		/* use BL_CUR_mA(I) 0 <= I <= 30 mA */
	u8 l3_dark_dim;		/* typ = 0, use BL_CUR_mA(I) 0 <= I <= 30 mA */

	u8 l2_trip;		/* use L2_COMP_CURR_uA(I) 0 <= I <= 1106 uA */
	u8 l2_hyst;		/* use L2_COMP_CURR_uA(I) 0 <= I <= 1106 uA */
	u8 l3_trip;		/* use L3_COMP_CURR_uA(I) 0 <= I <= 551 uA */
	u8 l3_hyst;		/* use L3_COMP_CURR_uA(I) 0 <= I <= 551 uA */

	/**
	 * Independent Current Sinks / LEDS
	 * Sinks not assigned to the Backlight can be exposed to
	 * user space using the LEDS CLASS interface
	 */

	int num_leds;
	struct led_info	*leds;
	u8 led_fade_in;		/* LED Fade-In Timer */
	u8 led_fade_out;	/* LED Fade-Out Timer */
	u8 led_fade_law;	/* fade-on/fade-off transfer characteristic */
	u8 led_on_time;

	/**
	 * Gain down disable. Setting this option does not allow the
	 * charge pump to switch to lower gains. NOT AVAILABLE on ADP8860
	 * 1 = the charge pump doesn't switch down in gain until all LEDs are 0.
	 *  The charge pump switches up in gain as needed. This feature is
	 *  useful if the ADP8863 charge pump is used to drive an external load.
	 *  This feature must be used when utilizing small fly capacitors
	 *  (0402 or smaller).
	 * 0 = the charge pump automatically switches up and down in gain.
	 *  This provides optimal efficiency, but is not suitable for driving
	 *  loads that are not connected through the ADP8863 diode drivers.
	 *  Additionally, the charge pump fly capacitors should be low ESR
	 * and sized 0603 or greater.
	 */

	u8 gdwn_dis;
};

#endif /* __LINUX_I2C_ADP8860_H */
