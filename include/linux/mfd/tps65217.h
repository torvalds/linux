/*
 * linux/mfd/tps65217.h
 *
 * Functions to access TPS65217 power management chip.
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LINUX_MFD_TPS65217_H
#define __LINUX_MFD_TPS65217_H

#include <linux/i2c.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

/* TPS chip id list */
#define TPS65217			0xF0

/* I2C ID for TPS65217 part */
#define TPS65217_I2C_ID			0x24

/* All register addresses */
#define TPS65217_REG_CHIPID		0X00
#define TPS65217_REG_PPATH		0X01
#define TPS65217_REG_INT		0X02
#define TPS65217_REG_CHGCONFIG0		0X03
#define TPS65217_REG_CHGCONFIG1		0X04
#define TPS65217_REG_CHGCONFIG2		0X05
#define TPS65217_REG_CHGCONFIG3		0X06
#define TPS65217_REG_WLEDCTRL1		0X07
#define TPS65217_REG_WLEDCTRL2		0X08
#define TPS65217_REG_MUXCTRL		0X09
#define TPS65217_REG_STATUS		0X0A
#define TPS65217_REG_PASSWORD		0X0B
#define TPS65217_REG_PGOOD		0X0C
#define TPS65217_REG_DEFPG		0X0D
#define TPS65217_REG_DEFDCDC1		0X0E
#define TPS65217_REG_DEFDCDC2		0X0F
#define TPS65217_REG_DEFDCDC3		0X10
#define TPS65217_REG_DEFSLEW		0X11
#define TPS65217_REG_DEFLDO1		0X12
#define TPS65217_REG_DEFLDO2		0X13
#define TPS65217_REG_DEFLS1		0X14
#define TPS65217_REG_DEFLS2		0X15
#define TPS65217_REG_ENABLE		0X16
#define TPS65217_REG_DEFUVLO		0X18
#define TPS65217_REG_SEQ1		0X19
#define TPS65217_REG_SEQ2		0X1A
#define TPS65217_REG_SEQ3		0X1B
#define TPS65217_REG_SEQ4		0X1C
#define TPS65217_REG_SEQ5		0X1D
#define TPS65217_REG_SEQ6		0X1E

/* Register field definitions */
#define TPS65217_CHIPID_CHIP_MASK	0xF0
#define TPS65217_CHIPID_REV_MASK	0x0F

#define TPS65217_PPATH_ACSINK_ENABLE	BIT(7)
#define TPS65217_PPATH_USBSINK_ENABLE	BIT(6)
#define TPS65217_PPATH_AC_PW_ENABLE	BIT(5)
#define TPS65217_PPATH_USB_PW_ENABLE	BIT(4)
#define TPS65217_PPATH_AC_CURRENT_MASK	0x0C
#define TPS65217_PPATH_USB_CURRENT_MASK	0x03

#define TPS65217_INT_PBM		BIT(6)
#define TPS65217_INT_ACM		BIT(5)
#define TPS65217_INT_USBM		BIT(4)
#define TPS65217_INT_PBI		BIT(2)
#define TPS65217_INT_ACI		BIT(1)
#define TPS65217_INT_USBI		BIT(0)

#define TPS65217_CHGCONFIG0_TREG	BIT(7)
#define TPS65217_CHGCONFIG0_DPPM	BIT(6)
#define TPS65217_CHGCONFIG0_TSUSP	BIT(5)
#define TPS65217_CHGCONFIG0_TERMI	BIT(4)
#define TPS65217_CHGCONFIG0_ACTIVE	BIT(3)
#define TPS65217_CHGCONFIG0_CHGTOUT	BIT(2)
#define TPS65217_CHGCONFIG0_PCHGTOUT	BIT(1)
#define TPS65217_CHGCONFIG0_BATTEMP	BIT(0)

#define TPS65217_CHGCONFIG1_TMR_MASK	0xC0
#define TPS65217_CHGCONFIG1_TMR_ENABLE	BIT(5)
#define TPS65217_CHGCONFIG1_NTC_TYPE	BIT(4)
#define TPS65217_CHGCONFIG1_RESET	BIT(3)
#define TPS65217_CHGCONFIG1_TERM	BIT(2)
#define TPS65217_CHGCONFIG1_SUSP	BIT(1)
#define TPS65217_CHGCONFIG1_CHG_EN	BIT(0)

#define TPS65217_CHGCONFIG2_DYNTMR	BIT(7)
#define TPS65217_CHGCONFIG2_VPREGHG	BIT(6)
#define TPS65217_CHGCONFIG2_VOREG_MASK	0x30

#define TPS65217_CHGCONFIG3_ICHRG_MASK	0xC0
#define TPS65217_CHGCONFIG3_DPPMTH_MASK	0x30
#define TPS65217_CHGCONFIG2_PCHRGT	BIT(3)
#define TPS65217_CHGCONFIG2_TERMIF	0x06
#define TPS65217_CHGCONFIG2_TRANGE	BIT(0)

#define TPS65217_WLEDCTRL1_ISINK_ENABLE	BIT(3)
#define TPS65217_WLEDCTRL1_ISEL		BIT(2)
#define TPS65217_WLEDCTRL1_FDIM_MASK	0x03

#define TPS65217_WLEDCTRL2_DUTY_MASK	0x7F

#define TPS65217_MUXCTRL_MUX_MASK	0x07

#define TPS65217_STATUS_OFF		BIT(7)
#define TPS65217_STATUS_ACPWR		BIT(3)
#define TPS65217_STATUS_USBPWR		BIT(2)
#define TPS65217_STATUS_PB		BIT(0)

#define TPS65217_PASSWORD_REGS_UNLOCK	0x7D

#define TPS65217_PGOOD_LDO3_PG		BIT(6)
#define TPS65217_PGOOD_LDO4_PG		BIT(5)
#define TPS65217_PGOOD_DC1_PG		BIT(4)
#define TPS65217_PGOOD_DC2_PG		BIT(3)
#define TPS65217_PGOOD_DC3_PG		BIT(2)
#define TPS65217_PGOOD_LDO1_PG		BIT(1)
#define TPS65217_PGOOD_LDO2_PG		BIT(0)

#define TPS65217_DEFPG_LDO1PGM		BIT(3)
#define TPS65217_DEFPG_LDO2PGM		BIT(2)
#define TPS65217_DEFPG_PGDLY_MASK	0x03

#define TPS65217_DEFDCDCX_XADJX		BIT(7)
#define TPS65217_DEFDCDCX_DCDC_MASK	0x3F

#define TPS65217_DEFSLEW_GO		BIT(7)
#define TPS65217_DEFSLEW_GODSBL		BIT(6)
#define TPS65217_DEFSLEW_PFM_EN1	BIT(5)
#define TPS65217_DEFSLEW_PFM_EN2	BIT(4)
#define TPS65217_DEFSLEW_PFM_EN3	BIT(3)
#define TPS65217_DEFSLEW_SLEW_MASK	0x07

#define TPS65217_DEFLDO1_LDO1_MASK	0x0F

#define TPS65217_DEFLDO2_TRACK		BIT(6)
#define TPS65217_DEFLDO2_LDO2_MASK	0x3F

#define TPS65217_DEFLDO3_LDO3_EN	BIT(5)
#define TPS65217_DEFLDO3_LDO3_MASK	0x1F

#define TPS65217_DEFLDO4_LDO4_EN	BIT(5)
#define TPS65217_DEFLDO4_LDO4_MASK	0x1F

#define TPS65217_ENABLE_LS1_EN		BIT(6)
#define TPS65217_ENABLE_LS2_EN		BIT(5)
#define TPS65217_ENABLE_DC1_EN		BIT(4)
#define TPS65217_ENABLE_DC2_EN		BIT(3)
#define TPS65217_ENABLE_DC3_EN		BIT(2)
#define TPS65217_ENABLE_LDO1_EN		BIT(1)
#define TPS65217_ENABLE_LDO2_EN		BIT(0)

#define TPS65217_DEFUVLO_UVLOHYS	BIT(2)
#define TPS65217_DEFUVLO_UVLO_MASK	0x03

#define TPS65217_SEQ1_DC1_SEQ_MASK	0xF0
#define TPS65217_SEQ1_DC2_SEQ_MASK	0x0F

#define TPS65217_SEQ2_DC3_SEQ_MASK	0xF0
#define TPS65217_SEQ2_LDO1_SEQ_MASK	0x0F

#define TPS65217_SEQ3_LDO2_SEQ_MASK	0xF0
#define TPS65217_SEQ3_LDO3_SEQ_MASK	0x0F

#define TPS65217_SEQ4_LDO4_SEQ_MASK	0xF0

#define TPS65217_SEQ5_DLY1_MASK		0xC0
#define TPS65217_SEQ5_DLY2_MASK		0x30
#define TPS65217_SEQ5_DLY3_MASK		0x0C
#define TPS65217_SEQ5_DLY4_MASK		0x03

#define TPS65217_SEQ6_DLY5_MASK		0xC0
#define TPS65217_SEQ6_DLY6_MASK		0x30
#define TPS65217_SEQ6_SEQUP		BIT(2)
#define TPS65217_SEQ6_SEQDWN		BIT(1)
#define TPS65217_SEQ6_INSTDWN		BIT(0)

#define TPS65217_MAX_REGISTER		0x1E
#define TPS65217_PROTECT_NONE		0
#define TPS65217_PROTECT_L1		1
#define TPS65217_PROTECT_L2		2


enum tps65217_regulator_id {
	/* DCDC's */
	TPS65217_DCDC_1,
	TPS65217_DCDC_2,
	TPS65217_DCDC_3,
	/* LDOs */
	TPS65217_LDO_1,
	TPS65217_LDO_2,
	TPS65217_LDO_3,
	TPS65217_LDO_4,
};

#define TPS65217_MAX_REG_ID		TPS65217_LDO_4

/* Number of step-down converters available */
#define TPS65217_NUM_DCDC		3
/* Number of LDO voltage regulators available */
#define TPS65217_NUM_LDO		4
/* Number of total regulators available */
#define TPS65217_NUM_REGULATOR		(TPS65217_NUM_DCDC + TPS65217_NUM_LDO)

enum tps65217_bl_isel {
	TPS65217_BL_ISET1 = 1,
	TPS65217_BL_ISET2,
};

enum tps65217_bl_fdim {
	TPS65217_BL_FDIM_100HZ,
	TPS65217_BL_FDIM_200HZ,
	TPS65217_BL_FDIM_500HZ,
	TPS65217_BL_FDIM_1000HZ,
};

struct tps65217_bl_pdata {
	enum tps65217_bl_isel isel;
	enum tps65217_bl_fdim fdim;
	int dft_brightness;
};

/**
 * struct tps65217_board - packages regulator init data
 * @tps65217_regulator_data: regulator initialization values
 *
 * Board data may be used to initialize regulator.
 */
struct tps65217_board {
	struct regulator_init_data *tps65217_init_data[TPS65217_NUM_REGULATOR];
	struct device_node *of_node[TPS65217_NUM_REGULATOR];
	struct tps65217_bl_pdata *bl_pdata;
};

/**
 * struct tps65217 - tps65217 sub-driver chip access routines
 *
 * Device data may be used to access the TPS65217 chip
 */

struct tps65217 {
	struct device *dev;
	struct tps65217_board *pdata;
	unsigned int id;
	struct regulator_desc desc[TPS65217_NUM_REGULATOR];
	struct regulator_dev *rdev[TPS65217_NUM_REGULATOR];
	struct regmap *regmap;
};

static inline struct tps65217 *dev_to_tps65217(struct device *dev)
{
	return dev_get_drvdata(dev);
}

static inline int tps65217_chip_id(struct tps65217 *tps65217)
{
	return tps65217->id;
}

int tps65217_reg_read(struct tps65217 *tps, unsigned int reg,
					unsigned int *val);
int tps65217_reg_write(struct tps65217 *tps, unsigned int reg,
			unsigned int val, unsigned int level);
int tps65217_set_bits(struct tps65217 *tps, unsigned int reg,
		unsigned int mask, unsigned int val, unsigned int level);
int tps65217_clear_bits(struct tps65217 *tps, unsigned int reg,
		unsigned int mask, unsigned int level);

#endif /*  __LINUX_MFD_TPS65217_H */
