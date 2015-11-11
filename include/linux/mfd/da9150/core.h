/*
 * DA9150 MFD Driver - Core Data
 *
 * Copyright (c) 2014 Dialog Semiconductor
 *
 * Author: Adam Thomson <Adam.Thomson.Opensource@diasemi.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __DA9150_CORE_H
#define __DA9150_CORE_H

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>

/* I2C address paging */
#define DA9150_REG_PAGE_SHIFT	8
#define DA9150_REG_PAGE_MASK	0xFF

/* IRQs */
#define DA9150_NUM_IRQ_REGS	4
#define DA9150_IRQ_VBUS		0
#define DA9150_IRQ_CHG		1
#define DA9150_IRQ_TCLASS	2
#define DA9150_IRQ_TJUNC	3
#define DA9150_IRQ_VFAULT	4
#define DA9150_IRQ_CONF		5
#define DA9150_IRQ_DAT		6
#define DA9150_IRQ_DTYPE	7
#define DA9150_IRQ_ID		8
#define DA9150_IRQ_ADP		9
#define DA9150_IRQ_SESS_END	10
#define DA9150_IRQ_SESS_VLD	11
#define DA9150_IRQ_FG		12
#define DA9150_IRQ_GP		13
#define DA9150_IRQ_TBAT		14
#define DA9150_IRQ_GPIOA	15
#define DA9150_IRQ_GPIOB	16
#define DA9150_IRQ_GPIOC	17
#define DA9150_IRQ_GPIOD	18
#define DA9150_IRQ_GPADC	19
#define DA9150_IRQ_WKUP		20

/* I2C sub-device address */
#define DA9150_QIF_I2C_ADDR_LSB		0x5

struct da9150_fg_pdata {
	u32 update_interval;	/* msecs */
	u8 warn_soc_lvl;	/* % value */
	u8 crit_soc_lvl;	/* % value */
};

struct da9150_pdata {
	int irq_base;
	struct da9150_fg_pdata *fg_pdata;
};

struct da9150 {
	struct device *dev;
	struct regmap *regmap;
	struct i2c_client *core_qif;

	struct regmap_irq_chip_data *regmap_irq_data;
	int irq;
	int irq_base;
};

/* Device I/O - Query Interface for FG and standard register access */
void da9150_read_qif(struct da9150 *da9150, u8 addr, int count, u8 *buf);
void da9150_write_qif(struct da9150 *da9150, u8 addr, int count, const u8 *buf);

u8 da9150_reg_read(struct da9150 *da9150, u16 reg);
void da9150_reg_write(struct da9150 *da9150, u16 reg, u8 val);
void da9150_set_bits(struct da9150 *da9150, u16 reg, u8 mask, u8 val);

void da9150_bulk_read(struct da9150 *da9150, u16 reg, int count, u8 *buf);
void da9150_bulk_write(struct da9150 *da9150, u16 reg, int count, const u8 *buf);

#endif /* __DA9150_CORE_H */
