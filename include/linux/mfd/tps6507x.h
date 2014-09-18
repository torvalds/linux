/* linux/mfd/tps6507x.h
 *
 * Functions to access TPS65070 power management chip.
 *
 * Copyright (c) 2009 RidgeRun (todd.fischer@ridgerun.com)
 *
 *
 *  For licencing details see kernel-base/COPYING
 */

#ifndef __LINUX_MFD_TPS6507X_H
#define __LINUX_MFD_TPS6507X_H

/*
 * ----------------------------------------------------------------------------
 * Registers, all 8 bits
 * ----------------------------------------------------------------------------
 */


/* Register definitions */
#define	TPS6507X_REG_PPATH1			0X01
#define	TPS6507X_CHG_USB			BIT(7)
#define	TPS6507X_CHG_AC				BIT(6)
#define	TPS6507X_CHG_USB_PW_ENABLE		BIT(5)
#define	TPS6507X_CHG_AC_PW_ENABLE		BIT(4)
#define	TPS6507X_CHG_AC_CURRENT			BIT(2)
#define	TPS6507X_CHG_USB_CURRENT		BIT(0)

#define	TPS6507X_REG_INT			0X02
#define	TPS6507X_REG_MASK_AC_USB		BIT(7)
#define	TPS6507X_REG_MASK_TSC			BIT(6)
#define	TPS6507X_REG_MASK_PB_IN			BIT(5)
#define	TPS6507X_REG_TSC_INT			BIT(3)
#define	TPS6507X_REG_PB_IN_INT			BIT(2)
#define	TPS6507X_REG_AC_USB_APPLIED		BIT(1)
#define	TPS6507X_REG_AC_USB_REMOVED		BIT(0)

#define	TPS6507X_REG_CHGCONFIG0			0X03

#define	TPS6507X_REG_CHGCONFIG1			0X04
#define	TPS6507X_CON_CTRL1_DCDC1_ENABLE		BIT(4)
#define	TPS6507X_CON_CTRL1_DCDC2_ENABLE		BIT(3)
#define	TPS6507X_CON_CTRL1_DCDC3_ENABLE		BIT(2)
#define	TPS6507X_CON_CTRL1_LDO1_ENABLE		BIT(1)
#define	TPS6507X_CON_CTRL1_LDO2_ENABLE		BIT(0)

#define	TPS6507X_REG_CHGCONFIG2			0X05

#define	TPS6507X_REG_CHGCONFIG3			0X06

#define	TPS6507X_REG_ADCONFIG			0X07
#define	TPS6507X_ADCONFIG_AD_ENABLE		BIT(7)
#define	TPS6507X_ADCONFIG_START_CONVERSION	BIT(6)
#define	TPS6507X_ADCONFIG_CONVERSION_DONE	BIT(5)
#define	TPS6507X_ADCONFIG_VREF_ENABLE		BIT(4)
#define	TPS6507X_ADCONFIG_INPUT_AD_IN1		0
#define	TPS6507X_ADCONFIG_INPUT_AD_IN2		1
#define	TPS6507X_ADCONFIG_INPUT_AD_IN3		2
#define	TPS6507X_ADCONFIG_INPUT_AD_IN4		3
#define	TPS6507X_ADCONFIG_INPUT_TS_PIN		4
#define	TPS6507X_ADCONFIG_INPUT_BAT_CURRENT	5
#define	TPS6507X_ADCONFIG_INPUT_AC_VOLTAGE	6
#define	TPS6507X_ADCONFIG_INPUT_SYS_VOLTAGE	7
#define	TPS6507X_ADCONFIG_INPUT_CHARGER_VOLTAGE 8
#define	TPS6507X_ADCONFIG_INPUT_BAT_VOLTAGE	9
#define	TPS6507X_ADCONFIG_INPUT_THRESHOLD_VOLTAGE 10
#define	TPS6507X_ADCONFIG_INPUT_ISET1_VOLTAGE	11
#define	TPS6507X_ADCONFIG_INPUT_ISET2_VOLTAGE	12
#define	TPS6507X_ADCONFIG_INPUT_REAL_TSC	14
#define	TPS6507X_ADCONFIG_INPUT_TSC		15

#define	TPS6507X_REG_TSCMODE			0X08
#define	TPS6507X_TSCMODE_X_POSITION		0
#define	TPS6507X_TSCMODE_Y_POSITION		1
#define	TPS6507X_TSCMODE_PRESSURE		2
#define	TPS6507X_TSCMODE_X_PLATE		3
#define	TPS6507X_TSCMODE_Y_PLATE		4
#define	TPS6507X_TSCMODE_STANDBY		5
#define	TPS6507X_TSCMODE_ADC_INPUT		6
#define	TPS6507X_TSCMODE_DISABLE		7

#define	TPS6507X_REG_ADRESULT_1			0X09

#define	TPS6507X_REG_ADRESULT_2			0X0A
#define	TPS6507X_REG_ADRESULT_2_MASK		(BIT(1) | BIT(0))

#define	TPS6507X_REG_PGOOD			0X0B

#define	TPS6507X_REG_PGOODMASK			0X0C

#define	TPS6507X_REG_CON_CTRL1			0X0D
#define	TPS6507X_CON_CTRL1_DCDC1_ENABLE		BIT(4)
#define	TPS6507X_CON_CTRL1_DCDC2_ENABLE		BIT(3)
#define	TPS6507X_CON_CTRL1_DCDC3_ENABLE		BIT(2)
#define	TPS6507X_CON_CTRL1_LDO1_ENABLE		BIT(1)
#define	TPS6507X_CON_CTRL1_LDO2_ENABLE		BIT(0)

#define	TPS6507X_REG_CON_CTRL2			0X0E

#define	TPS6507X_REG_CON_CTRL3			0X0F

#define	TPS6507X_REG_DEFDCDC1			0X10
#define TPS6507X_DEFDCDC1_DCDC1_EXT_ADJ_EN	BIT(7)
#define TPS6507X_DEFDCDC1_DCDC1_MASK		0X3F

#define	TPS6507X_REG_DEFDCDC2_LOW		0X11
#define TPS6507X_DEFDCDC2_LOW_DCDC2_MASK	0X3F

#define	TPS6507X_REG_DEFDCDC2_HIGH		0X12
#define TPS6507X_DEFDCDC2_HIGH_DCDC2_MASK	0X3F

#define	TPS6507X_REG_DEFDCDC3_LOW		0X13
#define TPS6507X_DEFDCDC3_LOW_DCDC3_MASK	0X3F

#define	TPS6507X_REG_DEFDCDC3_HIGH		0X14
#define TPS6507X_DEFDCDC3_HIGH_DCDC3_MASK	0X3F

#define	TPS6507X_REG_DEFSLEW			0X15

#define	TPS6507X_REG_LDO_CTRL1			0X16
#define TPS6507X_REG_LDO_CTRL1_LDO1_MASK	0X0F

#define	TPS6507X_REG_DEFLDO2			0X17
#define TPS6507X_REG_DEFLDO2_LDO2_MASK		0X3F

#define	TPS6507X_REG_WLED_CTRL1			0X18

#define	TPS6507X_REG_WLED_CTRL2			0X19

/* VDCDC MASK */
#define TPS6507X_DEFDCDCX_DCDC_MASK		0X3F

#define TPS6507X_MAX_REGISTER			0X19

/**
 * struct tps6507x_board - packages regulator and touchscreen init data
 * @tps6507x_regulator_data: regulator initialization values
 *
 * Board data may be used to initialize regulator and touchscreen.
 */

struct tps6507x_board {
	struct regulator_init_data *tps6507x_pmic_init_data;
	struct touchscreen_init_data *tps6507x_ts_init_data;
};

/**
 * struct tps6507x_dev - tps6507x sub-driver chip access routines
 * @read_dev() - I2C register read function
 * @write_dev() - I2C register write function
 *
 * Device data may be used to access the TPS6507x chip
 */

struct tps6507x_dev {
	struct device *dev;
	struct i2c_client *i2c_client;
	int (*read_dev)(struct tps6507x_dev *tps6507x, char reg, int size,
			void *dest);
	int (*write_dev)(struct tps6507x_dev *tps6507x, char reg, int size,
			 void *src);

	/* Client devices */
	struct tps6507x_pmic *pmic;
};

#endif /*  __LINUX_MFD_TPS6507X_H */
