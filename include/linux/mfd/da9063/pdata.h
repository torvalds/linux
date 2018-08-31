/*
 * Platform configuration options for DA9063
 *
 * Copyright 2012 Dialog Semiconductor Ltd.
 *
 * Author: Michal Hajduk, Dialog Semiconductor
 * Author: Krystian Garbaciak, Dialog Semiconductor
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __MFD_DA9063_PDATA_H__
#define __MFD_DA9063_PDATA_H__

#include <linux/regulator/machine.h>

/*
 * Regulator configuration
 */
/* DA9063 and DA9063L regulator IDs */
enum {
	/* BUCKs */
	DA9063_ID_BCORE1,
	DA9063_ID_BCORE2,
	DA9063_ID_BPRO,
	DA9063_ID_BMEM,
	DA9063_ID_BIO,
	DA9063_ID_BPERI,

	/* BCORE1 and BCORE2 in merged mode */
	DA9063_ID_BCORES_MERGED,
	/* BMEM and BIO in merged mode */
	DA9063_ID_BMEM_BIO_MERGED,
	/* When two BUCKs are merged, they cannot be reused separately */

	/* LDOs on both DA9063 and DA9063L */
	DA9063_ID_LDO3,
	DA9063_ID_LDO7,
	DA9063_ID_LDO8,
	DA9063_ID_LDO9,
	DA9063_ID_LDO11,

	/* DA9063-only LDOs */
	DA9063_ID_LDO1,
	DA9063_ID_LDO2,
	DA9063_ID_LDO4,
	DA9063_ID_LDO5,
	DA9063_ID_LDO6,
	DA9063_ID_LDO10,
};

/* Regulators platform data */
struct da9063_regulator_data {
	int				id;
	struct regulator_init_data	*initdata;
};

struct da9063_regulators_pdata {
	unsigned			n_regulators;
	struct da9063_regulator_data	*regulator_data;
};


/*
 * RGB LED configuration
 */
/* LED IDs for flags in struct led_info. */
enum {
	DA9063_GPIO11_LED,
	DA9063_GPIO14_LED,
	DA9063_GPIO15_LED,

	DA9063_LED_NUM
};
#define DA9063_LED_ID_MASK		0x3

/* LED polarity for flags in struct led_info. */
#define DA9063_LED_HIGH_LEVEL_ACTIVE	0x0
#define DA9063_LED_LOW_LEVEL_ACTIVE	0x4


/*
 * General PMIC configuration
 */
/* HWMON ADC channels configuration */
#define DA9063_FLG_FORCE_IN0_MANUAL_MODE	0x0010
#define DA9063_FLG_FORCE_IN0_AUTO_MODE		0x0020
#define DA9063_FLG_FORCE_IN1_MANUAL_MODE	0x0040
#define DA9063_FLG_FORCE_IN1_AUTO_MODE		0x0080
#define DA9063_FLG_FORCE_IN2_MANUAL_MODE	0x0100
#define DA9063_FLG_FORCE_IN2_AUTO_MODE		0x0200
#define DA9063_FLG_FORCE_IN3_MANUAL_MODE	0x0400
#define DA9063_FLG_FORCE_IN3_AUTO_MODE		0x0800

/* Disable register caching. */
#define DA9063_FLG_NO_CACHE			0x0008

struct da9063;

/* DA9063 platform data */
struct da9063_pdata {
	int				(*init)(struct da9063 *da9063);
	int				irq_base;
	bool				key_power;
	unsigned			flags;
	struct da9063_regulators_pdata	*regulators_pdata;
	struct led_platform_data	*leds_pdata;
};

#endif	/* __MFD_DA9063_PDATA_H__ */
