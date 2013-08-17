/*
 *  Copyright (C) 2010, Samsung Electronics Co. Ltd. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#ifndef __MXT540E_H__
#define __MXT540E_H__

#define MXT540E_DEV_NAME "Atmel MXT540E"

#define MXT540E_SW_RESET_TIME		300	/* msec */
#define MXT540E_HW_RESET_TIME		130	/* msec */

enum {
	RESERVED_T0 = 0,
	RESERVED_T1,
	DEBUG_DELTAS_T2,
	DEBUG_REFERENCES_T3,
	DEBUG_SIGNALS_T4,
	GEN_MESSAGEPROCESSOR_T5,
	GEN_COMMANDPROCESSOR_T6,
	GEN_POWERCONFIG_T7,
	GEN_ACQUISITIONCONFIG_T8,
	TOUCH_MULTITOUCHSCREEN_T9,
	TOUCH_SINGLETOUCHSCREEN_T10,
	TOUCH_XSLIDER_T11,
	TOUCH_YSLIDER_T12,
	TOUCH_XWHEEL_T13,
	TOUCH_YWHEEL_T14,
	TOUCH_KEYARRAY_T15,
	PROCG_SIGNALFILTER_T16,
	PROCI_LINEARIZATIONTABLE_T17,
	SPT_COMCONFIG_T18,
	SPT_GPIOPWM_T19,
	PROCI_GRIPFACESUPPRESSION_T20,
	RESERVED_T21,
	PROCG_NOISESUPPRESSION_T22,
	TOUCH_PROXIMITY_T23,
	PROCI_ONETOUCHGESTUREPROCESSOR_T24,
	SPT_SELFTEST_T25,
	DEBUG_CTERANGE_T26,
	PROCI_TWOTOUCHGESTUREPROCESSOR_T27,
	SPT_CTECONFIG_T28,
	SPT_GPI_T29,
	SPT_GATE_T30,
	TOUCH_KEYSET_T31,
	TOUCH_XSLIDERSET_T32,
	RESERVED_T33,
	GEN_MESSAGEBLOCK_T34,
	SPARE_T35,
	RESERVED_T36,
	DEBUG_DIAGNOSTIC_T37,
	SPT_USERDATA_T38,
	SPARE_T39,
	PROCI_GRIPSUPPRESSION_T40,
	SPARE_T41,
	PROCI_TOUCHSUPPRESSION_T42,
	SPT_DIGITIZER_T43,
	SPARE_T44,
	SPARE_T45,
	SPT_CTECONFIG_T46,
	PROCI_STYLUS_T47,
	PROCG_NOISESUPPRESSION_T48,
	SPARE_T49,
	SPARE_T50,
	SPARE_T51,
	TOUCH_PROXKEY_T52,
	GEN_DATASOURCE_T53,
	SPARE_T54,
	ADAPTIVE_T55,
	SPARE_T56,
	SPT_GENERICDATA_T57,
	SPARE_T58,
	SPARE_T59,
	SPARE_T60,
	SPT_TIMER_T61,
	RESERVED_T255 = 255,
};

struct mxt540e_platform_data {
	int max_finger_touches;
	const u8 **config_e;
	int gpio_read_done;
	int min_x;
	int max_x;
	int min_y;
	int max_y;
	int min_z;
	int max_z;
	int min_w;
	int max_w;
	u8 irqf_trigger_type;
	u8 chrgtime_batt;
	u8 chrgtime_charging;
	u8 tchthr_batt;
	u8 tchthr_charging;
	u8 actvsyncsperx_batt;
	u8 actvsyncsperx_charging;
	u8 calcfg_batt_e;
	u8 calcfg_charging_e;
	u8 atchfrccalthr_e;
	u8 atchfrccalratio_e;
	const u8 *t48_config_batt_e;
	const u8 *t48_config_chrg_e;
	void (*power_on) (void);
	void (*power_off) (void);
	void (*power_on_with_oleddet) (void);
	void (*power_off_with_oleddet) (void);
	void (*register_cb) (void *);
	void (*read_ta_status) (void *);
};

enum {
	MXT_PAGE_UP = 0x01,
	MXT_PAGE_DOWN = 0x02,
	MXT_DELTA_MODE = 0x10,
	MXT_REFERENCE_MODE = 0x11,
	MXT_CTE_MODE = 0x31
};

int get_tsp_status(void);

#endif
