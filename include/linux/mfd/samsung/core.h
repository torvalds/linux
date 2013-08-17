/*
 * core.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *              http://www.samsung.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __LINUX_MFD_CORE_H
#define __LINUX_MFD_CORE_H

#define NUM_IRQ_REGS	4

enum sec_device_type {
	S5M8751X,
	S5M8763X,
	S5M8767X,
	S2MPS11X,
};

/**
 * struct sec_pmic_dev - sec_pmic master device for sub-drivers
 * @dev: master device of the chip (can be used to access platform data)
 * @i2c: i2c client private data for regulator
 * @rtc: i2c client private data for rtc
 * @iolock: mutex for serializing io access
 * @irqlock: mutex for buslock
 * @irq_base: base IRQ number for sec_pmic, required for IRQs
 * @irq: generic IRQ number for sec_pmic
 * @ono: power onoff IRQ number for sec_pmic
 * @irq_masks_cur: currently active value
 * @irq_masks_cache: cached hardware value
 * @type: indicate which sec_pmic "variant" is used
 */
struct sec_pmic_dev {
	struct device *dev;
	struct regmap *regmap;
	struct regmap *rtc_regmap;
	struct i2c_client *i2c;
	struct i2c_client *rtc;
	struct mutex iolock;
	struct mutex irqlock;

	int device_type;
	int irq_base;
	int irq;
	int ono;
	u8 irq_masks_cur[NUM_IRQ_REGS];
	u8 irq_masks_cache[NUM_IRQ_REGS];
	int type;
	bool wakeup;
	bool wtsr_smpl;
};

struct sec_pmic_platform_data {
	struct sec_regulator_data *regulators;
	struct sec_opmode_data *opmode_data;

	int device_type;
	int num_regulators;

	int irq_base;
	int (*cfg_pmic_irq)(void);

	int ono;
	bool wakeup;
	bool buck_voltage_lock;

	int buck_gpios[3];
	int buck2_voltage[8];
	bool buck2_gpiodvs;
	int buck3_voltage[8];
	bool buck3_gpiodvs;
	int buck4_voltage[8];
	bool buck4_gpiodvs;

	int buck_set1;
	int buck_set2;
	int buck_set3;
	int buck2_enable;
	int buck3_enable;
	int buck4_enable;
	int buck_default_idx;
	int buck2_default_idx;
	int buck3_default_idx;
	int buck4_default_idx;

	int buck_ramp_delay;
	int buck2_ramp_delay;
	int buck34_ramp_delay;
	int buck5_ramp_delay;
	int buck16_ramp_delay;
	int buck7810_ramp_delay;
	int buck9_ramp_delay;

	bool buck2_ramp_enable;
	bool buck3_ramp_enable;
	bool buck4_ramp_enable;
	bool buck5_ramp_enable;
	bool buck6_ramp_enable;

	bool wtsr_smpl;
};

int sec_irq_init(struct sec_pmic_dev *sec_pmic);
void sec_irq_exit(struct sec_pmic_dev *sec_pmic);
int sec_irq_resume(struct sec_pmic_dev *sec_pmic);

extern int sec_reg_read(struct sec_pmic_dev *sec_pmic, u8 reg, void *dest);
extern int sec_bulk_read(struct sec_pmic_dev *sec_pmic, u8 reg, int count, u8 *buf);
extern int sec_reg_write(struct sec_pmic_dev *sec_pmic, u8 reg, u8 value);
extern int sec_bulk_write(struct sec_pmic_dev *sec_pmic, u8 reg, int count, u8 *buf);
extern int sec_reg_update(struct sec_pmic_dev *sec_pmic, u8 reg, u8 val, u8 mask);

extern int sec_rtc_read(struct sec_pmic_dev *sec_pmic, u8 reg, void *dest);
extern int sec_rtc_bulk_read(struct sec_pmic_dev *sec_pmic, u8 reg, int count,
				u8 *buf);
extern int sec_rtc_write(struct sec_pmic_dev *sec_pmic, u8 reg, u8 value);
extern int sec_rtc_bulk_write(struct sec_pmic_dev *sec_pmic, u8 reg, int count,
				u8 *buf);
extern int sec_rtc_update(struct sec_pmic_dev *sec_pmic, u8 reg, unsigned int val,
				unsigned int mask);

/**
 * sec_regulator_data - regulator data
 * @id: regulator id
 * @initdata: regulator init data (contraints, supplies, ...)
 */
struct sec_regulator_data {
	int id;
	struct regulator_init_data *initdata;
};

struct sec_opmode_data {
	int id;
	int mode;
};

enum sec_opmode {
	SEC_OPMODE_NORMAL,
	SEC_OPMODE_LP,
	SEC_OPMODE_STANDBY,
};

struct sec_irq_data {
	int reg;
	int mask;
};

#endif /*  __LINUX_MFD_CORE_H */
