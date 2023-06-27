/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *              http://www.samsung.com
 */

#ifndef __LINUX_MFD_SEC_CORE_H
#define __LINUX_MFD_SEC_CORE_H

/* Macros to represent minimum voltages for LDO/BUCK */
#define MIN_3000_MV		3000000
#define MIN_2500_MV		2500000
#define MIN_2000_MV		2000000
#define MIN_1800_MV		1800000
#define MIN_1500_MV		1500000
#define MIN_1400_MV		1400000
#define MIN_1000_MV		1000000

#define MIN_900_MV		900000
#define MIN_850_MV		850000
#define MIN_800_MV		800000
#define MIN_750_MV		750000
#define MIN_650_MV		650000
#define MIN_600_MV		600000
#define MIN_500_MV		500000

/* Ramp delay in uV/us */
#define RAMP_DELAY_12_MVUS	12000

/* Macros to represent steps for LDO/BUCK */
#define STEP_50_MV		50000
#define STEP_25_MV		25000
#define STEP_12_5_MV		12500
#define STEP_6_25_MV		6250

struct gpio_desc;

enum sec_device_type {
	S5M8767X,
	S2MPA01,
	S2MPS11X,
	S2MPS13X,
	S2MPS14X,
	S2MPS15X,
	S2MPU02,
};

/**
 * struct sec_pmic_dev - s2m/s5m master device for sub-drivers
 * @dev:		Master device of the chip
 * @pdata:		Platform data populated with data from DTS
 *			or board files
 * @regmap_pmic:	Regmap associated with PMIC's I2C address
 * @i2c:		I2C client of the main driver
 * @device_type:	Type of device, matches enum sec_device_type
 * @irq_base:		Base IRQ number for device, required for IRQs
 * @irq:		Generic IRQ number for device
 * @irq_data:		Runtime data structure for IRQ controller
 * @wakeup:		Whether or not this is a wakeup device
 */
struct sec_pmic_dev {
	struct device *dev;
	struct sec_platform_data *pdata;
	struct regmap *regmap_pmic;
	struct i2c_client *i2c;

	unsigned long device_type;
	int irq;
	struct regmap_irq_chip_data *irq_data;
};

int sec_irq_init(struct sec_pmic_dev *sec_pmic);
void sec_irq_exit(struct sec_pmic_dev *sec_pmic);
int sec_irq_resume(struct sec_pmic_dev *sec_pmic);

struct sec_platform_data {
	struct sec_regulator_data	*regulators;
	struct sec_opmode_data		*opmode;
	int				num_regulators;

	int				buck_gpios[3];
	int				buck_ds[3];
	unsigned int			buck2_voltage[8];
	bool				buck2_gpiodvs;
	unsigned int			buck3_voltage[8];
	bool				buck3_gpiodvs;
	unsigned int			buck4_voltage[8];
	bool				buck4_gpiodvs;

	int				buck_default_idx;
	int				buck_ramp_delay;

	bool				buck2_ramp_enable;
	bool				buck3_ramp_enable;
	bool				buck4_ramp_enable;

	int				buck2_init;
	int				buck3_init;
	int				buck4_init;
	/* Whether or not manually set PWRHOLD to low during shutdown. */
	bool				manual_poweroff;
	/* Disable the WRSTBI (buck voltage warm reset) when probing? */
	bool				disable_wrstbi;
};

/**
 * sec_regulator_data - regulator data
 * @id: regulator id
 * @initdata: regulator init data (contraints, supplies, ...)
 */
struct sec_regulator_data {
	int				id;
	struct regulator_init_data	*initdata;
	struct device_node		*reg_node;
	struct gpio_desc		*ext_control_gpiod;
};

/*
 * sec_opmode_data - regulator operation mode data
 * @id: regulator id
 * @mode: regulator operation mode
 */
struct sec_opmode_data {
	int id;
	unsigned int mode;
};

/*
 * samsung regulator operation mode
 * SEC_OPMODE_OFF	Regulator always OFF
 * SEC_OPMODE_ON	Regulator always ON
 * SEC_OPMODE_LOWPOWER  Regulator is on in low-power mode
 * SEC_OPMODE_SUSPEND   Regulator is changed by PWREN pin
 *			If PWREN is high, regulator is on
 *			If PWREN is low, regulator is off
 */

enum sec_opmode {
	SEC_OPMODE_OFF,
	SEC_OPMODE_ON,
	SEC_OPMODE_LOWPOWER,
	SEC_OPMODE_SUSPEND,
};

#endif /*  __LINUX_MFD_SEC_CORE_H */
