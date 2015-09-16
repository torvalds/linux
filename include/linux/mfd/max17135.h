/*
 * Copyright (C) 2010-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#ifndef __LINUX_REGULATOR_MAX17135_H_
#define __LINUX_REGULATOR_MAX17135_H_

/*
 * PMIC Register Addresses
 */
enum {
    REG_MAX17135_EXT_TEMP = 0x0,
    REG_MAX17135_CONFIG,
    REG_MAX17135_INT_TEMP = 0x4,
    REG_MAX17135_STATUS,
    REG_MAX17135_PRODUCT_REV,
    REG_MAX17135_PRODUCT_ID,
    REG_MAX17135_DVR,
    REG_MAX17135_ENABLE,
    REG_MAX17135_FAULT,  /*0x0A*/
    REG_MAX17135_HVINP,
    REG_MAX17135_PRGM_CTRL,
    REG_MAX17135_TIMING1 = 0x10,    /* Timing regs base address is 0x10 */
    REG_MAX17135_TIMING2,
    REG_MAX17135_TIMING3,
    REG_MAX17135_TIMING4,
    REG_MAX17135_TIMING5,
    REG_MAX17135_TIMING6,
    REG_MAX17135_TIMING7,
    REG_MAX17135_TIMING8,
};
#define MAX17135_REG_NUM        21
#define MAX17135_MAX_REGISTER   0xFF

/*
 * Bitfield macros that use rely on bitfield width/shift information.
 */
#define BITFMASK(field) (((1U << (field ## _WID)) - 1) << (field ## _LSH))
#define BITFVAL(field, val) ((val) << (field ## _LSH))
#define BITFEXT(var, bit) ((var & BITFMASK(bit)) >> (bit ## _LSH))

/*
 * Shift and width values for each register bitfield
 */
#define EXT_TEMP_LSH    7
#define EXT_TEMP_WID    9

#define THERMAL_SHUTDOWN_LSH    0
#define THERMAL_SHUTDOWN_WID    1

#define INT_TEMP_LSH    7
#define INT_TEMP_WID    9

#define STAT_BUSY_LSH   0
#define STAT_BUSY_WID   1
#define STAT_OPEN_LSH   1
#define STAT_OPEN_WID   1
#define STAT_SHRT_LSH   2
#define STAT_SHRT_WID   1

#define PROD_REV_LSH    0
#define PROD_REV_WID    8

#define PROD_ID_LSH     0
#define PROD_ID_WID     8

#define DVR_LSH         0
#define DVR_WID         8

#define ENABLE_LSH      0
#define ENABLE_WID      1
#define VCOM_ENABLE_LSH 1
#define VCOM_ENABLE_WID 1

#define FAULT_FBPG_LSH      0
#define FAULT_FBPG_WID      1
#define FAULT_HVINP_LSH     1
#define FAULT_HVINP_WID     1
#define FAULT_HVINN_LSH     2
#define FAULT_HVINN_WID     1
#define FAULT_FBNG_LSH      3
#define FAULT_FBNG_WID      1
#define FAULT_HVINPSC_LSH   4
#define FAULT_HVINPSC_WID   1
#define FAULT_HVINNSC_LSH   5
#define FAULT_HVINNSC_WID   1
#define FAULT_OT_LSH        6
#define FAULT_OT_WID        1
#define FAULT_POK_LSH       7
#define FAULT_POK_WID       1

#define HVINP_LSH           0
#define HVINP_WID           4

#define CTRL_DVR_LSH        0
#define CTRL_DVR_WID        1
#define CTRL_TIMING_LSH     1
#define CTRL_TIMING_WID     1

#define TIMING1_LSH         0
#define TIMING1_WID         8
#define TIMING2_LSH         0
#define TIMING2_WID         8
#define TIMING3_LSH         0
#define TIMING3_WID         8
#define TIMING4_LSH         0
#define TIMING4_WID         8
#define TIMING5_LSH         0
#define TIMING5_WID         8
#define TIMING6_LSH         0
#define TIMING6_WID         8
#define TIMING7_LSH         0
#define TIMING7_WID         8
#define TIMING8_LSH         0
#define TIMING8_WID         8

struct max17135 {
	/* chip revision */
	int rev;

	struct device *dev;
	struct max17135_platform_data *pdata;

	/* Platform connection */
	struct i2c_client *i2c_client;

	/* Timings */
	unsigned int gvee_pwrup;
	unsigned int vneg_pwrup;
	unsigned int vpos_pwrup;
	unsigned int gvdd_pwrup;
	unsigned int gvdd_pwrdn;
	unsigned int vpos_pwrdn;
	unsigned int vneg_pwrdn;
	unsigned int gvee_pwrdn;

	/* GPIOs */
	int gpio_pmic_pwrgood;
	int gpio_pmic_vcom_ctrl;
	int gpio_pmic_wakeup;
	int gpio_pmic_v3p3;
	int gpio_pmic_intr;

	/* MAX17135 part variables */
	int pass_num;
	int vcom_uV;

	/* One-time VCOM setup marker */
	bool vcom_setup;

	/* powerup/powerdown wait time */
	int max_wait;
};

enum {
    /* In alphabetical order */
    MAX17135_DISPLAY, /* virtual master enable */
    MAX17135_GVDD,
    MAX17135_GVEE,
    MAX17135_HVINN,
    MAX17135_HVINP,
    MAX17135_VCOM,
    MAX17135_VNEG,
    MAX17135_VPOS,
    MAX17135_V3P3,
    MAX17135_NUM_REGULATORS,
};

/*
 * Declarations
 */
struct regulator_init_data;
struct max17135_regulator_data;

struct max17135_platform_data {
	unsigned int gvee_pwrup;
	unsigned int vneg_pwrup;
	unsigned int vpos_pwrup;
	unsigned int gvdd_pwrup;
	unsigned int gvdd_pwrdn;
	unsigned int vpos_pwrdn;
	unsigned int vneg_pwrdn;
	unsigned int gvee_pwrdn;
	int gpio_pmic_pwrgood;
	int gpio_pmic_vcom_ctrl;
	int gpio_pmic_wakeup;
	int gpio_pmic_v3p3;
	int gpio_pmic_intr;
	int pass_num;
	int vcom_uV;

	/* PMIC */
	struct max17135_regulator_data *regulators;
	int num_regulators;
};

struct max17135_regulator_data {
	int id;
	struct regulator_init_data *initdata;
	struct device_node *reg_node;
};

int max17135_reg_read(int reg_num, unsigned int *reg_val);
int max17135_reg_write(int reg_num, const unsigned int reg_val);

#endif
