/*
 * AS3711 PMIC MFC driver header
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 * Author: Guennadi Liakhovetski, <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License as
 * published by the Free Software Foundation
 */

#ifndef MFD_AS3711_H
#define MFD_AS3711_H

/*
 * Client data
 */

/* Register addresses */
#define AS3711_SD_1_VOLTAGE		0	/* Digital Step-Down */
#define AS3711_SD_2_VOLTAGE		1
#define AS3711_SD_3_VOLTAGE		2
#define AS3711_SD_4_VOLTAGE		3
#define AS3711_LDO_1_VOLTAGE		4	/* Analog LDO */
#define AS3711_LDO_2_VOLTAGE		5
#define AS3711_LDO_3_VOLTAGE		6	/* Digital LDO */
#define AS3711_LDO_4_VOLTAGE		7
#define AS3711_LDO_5_VOLTAGE		8
#define AS3711_LDO_6_VOLTAGE		9
#define AS3711_LDO_7_VOLTAGE		0xa
#define AS3711_LDO_8_VOLTAGE		0xb
#define AS3711_SD_CONTROL		0x10
#define AS3711_GPIO_SIGNAL_OUT		0x20
#define AS3711_GPIO_SIGNAL_IN		0x21
#define AS3711_SD_CONTROL_1		0x30
#define AS3711_SD_CONTROL_2		0x31
#define AS3711_CURR_CONTROL		0x40
#define AS3711_CURR1_VALUE		0x43
#define AS3711_CURR2_VALUE		0x44
#define AS3711_CURR3_VALUE		0x45
#define AS3711_STEPUP_CONTROL_1		0x50
#define AS3711_STEPUP_CONTROL_2		0x51
#define AS3711_STEPUP_CONTROL_4		0x53
#define AS3711_STEPUP_CONTROL_5		0x54
#define AS3711_REG_STATUS		0x73
#define AS3711_INTERRUPT_STATUS_1	0x77
#define AS3711_INTERRUPT_STATUS_2	0x78
#define AS3711_INTERRUPT_STATUS_3	0x79
#define AS3711_CHARGER_STATUS_1		0x86
#define AS3711_CHARGER_STATUS_2		0x87
#define AS3711_ASIC_ID_1		0x90
#define AS3711_ASIC_ID_2		0x91

#define AS3711_MAX_REG		AS3711_ASIC_ID_2
#define AS3711_NUM_REGS		(AS3711_MAX_REG + 1)

/* Regulators */
enum {
	AS3711_REGULATOR_SD_1,
	AS3711_REGULATOR_SD_2,
	AS3711_REGULATOR_SD_3,
	AS3711_REGULATOR_SD_4,
	AS3711_REGULATOR_LDO_1,
	AS3711_REGULATOR_LDO_2,
	AS3711_REGULATOR_LDO_3,
	AS3711_REGULATOR_LDO_4,
	AS3711_REGULATOR_LDO_5,
	AS3711_REGULATOR_LDO_6,
	AS3711_REGULATOR_LDO_7,
	AS3711_REGULATOR_LDO_8,

	AS3711_REGULATOR_MAX,
};

struct device;
struct regmap;

struct as3711 {
	struct device *dev;
	struct regmap *regmap;
};

#define AS3711_MAX_STEPDOWN 4
#define AS3711_MAX_STEPUP 2
#define AS3711_MAX_LDO 8

enum as3711_su2_feedback {
	AS3711_SU2_VOLTAGE,
	AS3711_SU2_CURR1,
	AS3711_SU2_CURR2,
	AS3711_SU2_CURR3,
	AS3711_SU2_CURR_AUTO,
};

enum as3711_su2_fbprot {
	AS3711_SU2_LX_SD4,
	AS3711_SU2_GPIO2,
	AS3711_SU2_GPIO3,
	AS3711_SU2_GPIO4,
};

/*
 * Platform data
 */

struct as3711_regulator_pdata {
	struct regulator_init_data *init_data[AS3711_REGULATOR_MAX];
};

struct as3711_bl_pdata {
	const char *su1_fb;
	int su1_max_uA;
	const char *su2_fb;
	int su2_max_uA;
	enum as3711_su2_feedback su2_feedback;
	enum as3711_su2_fbprot su2_fbprot;
	bool su2_auto_curr1;
	bool su2_auto_curr2;
	bool su2_auto_curr3;
};

struct as3711_platform_data {
	struct as3711_regulator_pdata regulator;
	struct as3711_bl_pdata backlight;
};

#endif
