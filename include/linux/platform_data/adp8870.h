/*
 * Definitions and platform data for Analog Devices
 * Backlight drivers ADP8870
 *
 * Copyright 2009-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __LINUX_I2C_ADP8870_H
#define __LINUX_I2C_ADP8870_H

#define ID_ADP8870		8870

#define ADP8870_MAX_BRIGHTNESS	0x7F
#define FLAG_OFFT_SHIFT 8

/*
 * LEDs subdevice platform data
 */

#define ADP8870_LED_DIS_BLINK	(0 << FLAG_OFFT_SHIFT)
#define ADP8870_LED_OFFT_600ms	(1 << FLAG_OFFT_SHIFT)
#define ADP8870_LED_OFFT_1200ms	(2 << FLAG_OFFT_SHIFT)
#define ADP8870_LED_OFFT_1800ms	(3 << FLAG_OFFT_SHIFT)

#define ADP8870_LED_ONT_200ms	0
#define ADP8870_LED_ONT_600ms	1
#define ADP8870_LED_ONT_800ms	2
#define ADP8870_LED_ONT_1200ms	3

#define ADP8870_LED_D7		(7)
#define ADP8870_LED_D6		(6)
#define ADP8870_LED_D5		(5)
#define ADP8870_LED_D4		(4)
#define ADP8870_LED_D3		(3)
#define ADP8870_LED_D2		(2)
#define ADP8870_LED_D1		(1)

/*
 * Backlight subdevice platform data
 */

#define ADP8870_BL_D7		(1 << 6)
#define ADP8870_BL_D6		(1 << 5)
#define ADP8870_BL_D5		(1 << 4)
#define ADP8870_BL_D4		(1 << 3)
#define ADP8870_BL_D3		(1 << 2)
#define ADP8870_BL_D2		(1 << 1)
#define ADP8870_BL_D1		(1 << 0)

#define ADP8870_FADE_T_DIS	0	/* Fade Timer Disabled */
#define ADP8870_FADE_T_300ms	1	/* 0.3 Sec */
#define ADP8870_FADE_T_600ms	2
#define ADP8870_FADE_T_900ms	3
#define ADP8870_FADE_T_1200ms	4
#define ADP8870_FADE_T_1500ms	5
#define ADP8870_FADE_T_1800ms	6
#define ADP8870_FADE_T_2100ms	7
#define ADP8870_FADE_T_2400ms	8
#define ADP8870_FADE_T_2700ms	9
#define ADP8870_FADE_T_3000ms	10
#define ADP8870_FADE_T_3500ms	11
#define ADP8870_FADE_T_4000ms	12
#define ADP8870_FADE_T_4500ms	13
#define ADP8870_FADE_T_5000ms	14
#define ADP8870_FADE_T_5500ms	15	/* 5.5 Sec */

#define ADP8870_FADE_LAW_LINEAR	0
#define ADP8870_FADE_LAW_SQUARE	1
#define ADP8870_FADE_LAW_CUBIC1	2
#define ADP8870_FADE_LAW_CUBIC2	3

#define ADP8870_BL_AMBL_FILT_80ms	0	/* Light sensor filter time */
#define ADP8870_BL_AMBL_FILT_160ms	1
#define ADP8870_BL_AMBL_FILT_320ms	2
#define ADP8870_BL_AMBL_FILT_640ms	3
#define ADP8870_BL_AMBL_FILT_1280ms	4
#define ADP8870_BL_AMBL_FILT_2560ms	5
#define ADP8870_BL_AMBL_FILT_5120ms	6
#define ADP8870_BL_AMBL_FILT_10240ms	7	/* 10.24 sec */

/*
 * Blacklight current 0..30mA
 */
#define ADP8870_BL_CUR_mA(I)		((I * 127) / 30)

/*
 * L2 comparator current 0..1106uA
 */
#define ADP8870_L2_COMP_CURR_uA(I)	((I * 255) / 1106)

/*
 * L3 comparator current 0..551uA
 */
#define ADP8870_L3_COMP_CURR_uA(I)	((I * 255) / 551)

/*
 * L4 comparator current 0..275uA
 */
#define ADP8870_L4_COMP_CURR_uA(I)	((I * 255) / 275)

/*
 * L5 comparator current 0..138uA
 */
#define ADP8870_L5_COMP_CURR_uA(I)	((I * 255) / 138)

struct adp8870_backlight_platform_data {
	u8 bl_led_assign;	/* 1 = Backlight 0 = Individual LED */
	u8 pwm_assign;		/* 1 = Enables PWM mode */

	u8 bl_fade_in;		/* Backlight Fade-In Timer */
	u8 bl_fade_out;		/* Backlight Fade-Out Timer */
	u8 bl_fade_law;		/* fade-on/fade-off transfer characteristic */

	u8 en_ambl_sens;	/* 1 = enable ambient light sensor */
	u8 abml_filt;		/* Light sensor filter time */

	u8 l1_daylight_max;	/* use BL_CUR_mA(I) 0 <= I <= 30 mA */
	u8 l1_daylight_dim;	/* typ = 0, use BL_CUR_mA(I) 0 <= I <= 30 mA */
	u8 l2_bright_max;	/* use BL_CUR_mA(I) 0 <= I <= 30 mA */
	u8 l2_bright_dim;	/* typ = 0, use BL_CUR_mA(I) 0 <= I <= 30 mA */
	u8 l3_office_max;	/* use BL_CUR_mA(I) 0 <= I <= 30 mA */
	u8 l3_office_dim;	/* typ = 0, use BL_CUR_mA(I) 0 <= I <= 30 mA */
	u8 l4_indoor_max;	/* use BL_CUR_mA(I) 0 <= I <= 30 mA */
	u8 l4_indor_dim;	/* typ = 0, use BL_CUR_mA(I) 0 <= I <= 30 mA */
	u8 l5_dark_max;		/* use BL_CUR_mA(I) 0 <= I <= 30 mA */
	u8 l5_dark_dim;		/* typ = 0, use BL_CUR_mA(I) 0 <= I <= 30 mA */

	u8 l2_trip;		/* use L2_COMP_CURR_uA(I) 0 <= I <= 1106 uA */
	u8 l2_hyst;		/* use L2_COMP_CURR_uA(I) 0 <= I <= 1106 uA */
	u8 l3_trip;		/* use L3_COMP_CURR_uA(I) 0 <= I <= 551 uA */
	u8 l3_hyst;		/* use L3_COMP_CURR_uA(I) 0 <= I <= 551 uA */
	u8 l4_trip;		/* use L4_COMP_CURR_uA(I) 0 <= I <= 275 uA */
	u8 l4_hyst;		/* use L4_COMP_CURR_uA(I) 0 <= I <= 275 uA */
	u8 l5_trip;		/* use L5_COMP_CURR_uA(I) 0 <= I <= 138 uA */
	u8 l5_hyst;		/* use L6_COMP_CURR_uA(I) 0 <= I <= 138 uA */

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
};

#endif /* __LINUX_I2C_ADP8870_H */
