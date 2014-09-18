/*
 * core.h
 *
 * copyright (c) 2011 Samsung Electronics Co., Ltd
 *              http://www.samsung.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __LINUX_MFD_SEC_CORE_H
#define __LINUX_MFD_SEC_CORE_H

enum sec_device_type {
	S5M8751X,
	S5M8763X,
	S5M8767X,
	S2MPA01,
	S2MPS11X,
	S2MPS14X,
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
 * @ono:		Power onoff IRQ number for s5m87xx
 * @wakeup:		Whether or not this is a wakeup device
 * @wtsr_smpl:		Whether or not to enable in RTC driver the Watchdog
 *			Timer Software Reset (registers set to default value
 *			after PWRHOLD falling) and Sudden Momentary Power Loss
 *			(PMIC will enter power on sequence after short drop in
 *			VBATT voltage).
 */
struct sec_pmic_dev {
	struct device *dev;
	struct sec_platform_data *pdata;
	struct regmap *regmap_pmic;
	struct i2c_client *i2c;

	unsigned long device_type;
	int irq_base;
	int irq;
	struct regmap_irq_chip_data *irq_data;

	int ono;
	bool wakeup;
	bool wtsr_smpl;
};

int sec_irq_init(struct sec_pmic_dev *sec_pmic);
void sec_irq_exit(struct sec_pmic_dev *sec_pmic);
int sec_irq_resume(struct sec_pmic_dev *sec_pmic);

struct sec_platform_data {
	struct sec_regulator_data	*regulators;
	struct sec_opmode_data		*opmode;
	int				device_type;
	int				num_regulators;

	int				irq_base;
	int				(*cfg_pmic_irq)(void);

	int				ono;
	bool				wakeup;
	bool				buck_voltage_lock;

	int				buck_gpios[3];
	int				buck_ds[3];
	unsigned int			buck2_voltage[8];
	bool				buck2_gpiodvs;
	unsigned int			buck3_voltage[8];
	bool				buck3_gpiodvs;
	unsigned int			buck4_voltage[8];
	bool				buck4_gpiodvs;

	int				buck_set1;
	int				buck_set2;
	int				buck_set3;
	int				buck2_enable;
	int				buck3_enable;
	int				buck4_enable;
	int				buck_default_idx;
	int				buck2_default_idx;
	int				buck3_default_idx;
	int				buck4_default_idx;

	int				buck_ramp_delay;

	int				buck2_ramp_delay;
	int				buck34_ramp_delay;
	int				buck5_ramp_delay;
	int				buck16_ramp_delay;
	int				buck7810_ramp_delay;
	int				buck9_ramp_delay;
	int				buck24_ramp_delay;
	int				buck3_ramp_delay;
	int				buck7_ramp_delay;
	int				buck8910_ramp_delay;

	bool				buck1_ramp_enable;
	bool				buck2_ramp_enable;
	bool				buck3_ramp_enable;
	bool				buck4_ramp_enable;
	bool				buck6_ramp_enable;

	int				buck2_init;
	int				buck3_init;
	int				buck4_init;
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
	int				ext_control_gpio;
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
