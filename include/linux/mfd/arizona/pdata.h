/*
 * Platform data for Arizona devices
 *
 * Copyright 2012 Wolfson Microelectronics. PLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ARIZONA_PDATA_H
#define _ARIZONA_PDATA_H

#define ARIZONA_GPN_DIR                          0x8000  /* GPN_DIR */
#define ARIZONA_GPN_DIR_MASK                     0x8000  /* GPN_DIR */
#define ARIZONA_GPN_DIR_SHIFT                        15  /* GPN_DIR */
#define ARIZONA_GPN_DIR_WIDTH                         1  /* GPN_DIR */
#define ARIZONA_GPN_PU                           0x4000  /* GPN_PU */
#define ARIZONA_GPN_PU_MASK                      0x4000  /* GPN_PU */
#define ARIZONA_GPN_PU_SHIFT                         14  /* GPN_PU */
#define ARIZONA_GPN_PU_WIDTH                          1  /* GPN_PU */
#define ARIZONA_GPN_PD                           0x2000  /* GPN_PD */
#define ARIZONA_GPN_PD_MASK                      0x2000  /* GPN_PD */
#define ARIZONA_GPN_PD_SHIFT                         13  /* GPN_PD */
#define ARIZONA_GPN_PD_WIDTH                          1  /* GPN_PD */
#define ARIZONA_GPN_LVL                          0x0800  /* GPN_LVL */
#define ARIZONA_GPN_LVL_MASK                     0x0800  /* GPN_LVL */
#define ARIZONA_GPN_LVL_SHIFT                        11  /* GPN_LVL */
#define ARIZONA_GPN_LVL_WIDTH                         1  /* GPN_LVL */
#define ARIZONA_GPN_POL                          0x0400  /* GPN_POL */
#define ARIZONA_GPN_POL_MASK                     0x0400  /* GPN_POL */
#define ARIZONA_GPN_POL_SHIFT                        10  /* GPN_POL */
#define ARIZONA_GPN_POL_WIDTH                         1  /* GPN_POL */
#define ARIZONA_GPN_OP_CFG                       0x0200  /* GPN_OP_CFG */
#define ARIZONA_GPN_OP_CFG_MASK                  0x0200  /* GPN_OP_CFG */
#define ARIZONA_GPN_OP_CFG_SHIFT                      9  /* GPN_OP_CFG */
#define ARIZONA_GPN_OP_CFG_WIDTH                      1  /* GPN_OP_CFG */
#define ARIZONA_GPN_DB                           0x0100  /* GPN_DB */
#define ARIZONA_GPN_DB_MASK                      0x0100  /* GPN_DB */
#define ARIZONA_GPN_DB_SHIFT                          8  /* GPN_DB */
#define ARIZONA_GPN_DB_WIDTH                          1  /* GPN_DB */
#define ARIZONA_GPN_FN_MASK                      0x007F  /* GPN_FN - [6:0] */
#define ARIZONA_GPN_FN_SHIFT                          0  /* GPN_FN - [6:0] */
#define ARIZONA_GPN_FN_WIDTH                          7  /* GPN_FN - [6:0] */

#define ARIZONA_MAX_GPIO 5

#define ARIZONA_32KZ_MCLK1 1
#define ARIZONA_32KZ_MCLK2 2
#define ARIZONA_32KZ_NONE  3

#define ARIZONA_MAX_INPUT 4

#define ARIZONA_DMIC_MICVDD   0
#define ARIZONA_DMIC_MICBIAS1 1
#define ARIZONA_DMIC_MICBIAS2 2
#define ARIZONA_DMIC_MICBIAS3 3

#define ARIZONA_INMODE_DIFF 0
#define ARIZONA_INMODE_SE   1
#define ARIZONA_INMODE_DMIC 2

#define ARIZONA_MAX_OUTPUT 6

#define ARIZONA_HAP_ACT_ERM 0
#define ARIZONA_HAP_ACT_LRA 2

#define ARIZONA_MAX_PDM_SPK 2

struct regulator_init_data;

struct arizona_micd_config {
	unsigned int src;
	unsigned int bias;
	bool gpio;
};

struct arizona_pdata {
	int reset;      /** GPIO controlling /RESET, if any */
	int ldoena;     /** GPIO controlling LODENA, if any */

	/** Regulator configuration for MICVDD */
	struct regulator_init_data *micvdd;

	/** Regulator configuration for LDO1 */
	struct regulator_init_data *ldo1;

	/** If a direct 32kHz clock is provided on an MCLK specify it here */
	int clk32k_src;

	bool irq_active_high; /** IRQ polarity */

	/* Base GPIO */
	int gpio_base;

	/** Pin state for GPIO pins */
	int gpio_defaults[ARIZONA_MAX_GPIO];

	/** GPIO5 is used for jack detection */
	bool jd_gpio5;

	/** Use the headphone detect circuit to identify the accessory */
	bool hpdet_acc_id;

	/** GPIO used for mic isolation with HPDET */
	int hpdet_id_gpio;

	/** GPIO for mic detection polarity */
	int micd_pol_gpio;

	/** Mic detect ramp rate */
	int micd_bias_start_time;

	/** Mic detect sample rate */
	int micd_rate;

	/** Mic detect debounce level */
	int micd_dbtime;

	/** Headset polarity configurations */
	struct arizona_micd_config *micd_configs;
	int num_micd_configs;

	/** Reference voltage for DMIC inputs */
	int dmic_ref[ARIZONA_MAX_INPUT];

	/** Mode of input structures */
	int inmode[ARIZONA_MAX_INPUT];

	/** Mode for outputs */
	bool out_mono[ARIZONA_MAX_OUTPUT];

	/** PDM speaker mute setting */
	unsigned int spk_mute[ARIZONA_MAX_PDM_SPK];

	/** PDM speaker format */
	unsigned int spk_fmt[ARIZONA_MAX_PDM_SPK];

	/** Haptic actuator type */
	unsigned int hap_act;
};

#endif
