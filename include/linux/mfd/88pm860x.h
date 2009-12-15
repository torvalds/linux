/*
 * Marvell 88PM860x Interface
 *
 * Copyright (C) 2009 Marvell International Ltd.
 * 	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MFD_88PM860X_H
#define __LINUX_MFD_88PM860X_H

#include <linux/interrupt.h>

enum {
	CHIP_INVALID = 0,
	CHIP_PM8606,
	CHIP_PM8607,
	CHIP_MAX,
};

enum {
	PM8607_ID_BUCK1 = 0,
	PM8607_ID_BUCK2,
	PM8607_ID_BUCK3,

	PM8607_ID_LDO1,
	PM8607_ID_LDO2,
	PM8607_ID_LDO3,
	PM8607_ID_LDO4,
	PM8607_ID_LDO5,
	PM8607_ID_LDO6,
	PM8607_ID_LDO7,
	PM8607_ID_LDO8,
	PM8607_ID_LDO9,
	PM8607_ID_LDO10,
	PM8607_ID_LDO12,
	PM8607_ID_LDO14,

	PM8607_ID_RG_MAX,
};

#define PM8607_VERSION			(0x40)	/* 8607 chip ID */
#define PM8607_VERSION_MASK		(0xF0)	/* 8607 chip ID mask */

/* Interrupt Registers */
#define PM8607_STATUS_1			(0x01)
#define PM8607_STATUS_2			(0x02)
#define PM8607_INT_STATUS1		(0x03)
#define PM8607_INT_STATUS2		(0x04)
#define PM8607_INT_STATUS3		(0x05)
#define PM8607_INT_MASK_1		(0x06)
#define PM8607_INT_MASK_2		(0x07)
#define PM8607_INT_MASK_3		(0x08)

/* Regulator Control Registers */
#define PM8607_LDO1			(0x10)
#define PM8607_LDO2			(0x11)
#define PM8607_LDO3			(0x12)
#define PM8607_LDO4			(0x13)
#define PM8607_LDO5			(0x14)
#define PM8607_LDO6			(0x15)
#define PM8607_LDO7			(0x16)
#define PM8607_LDO8			(0x17)
#define PM8607_LDO9			(0x18)
#define PM8607_LDO10			(0x19)
#define PM8607_LDO12			(0x1A)
#define PM8607_LDO14			(0x1B)
#define PM8607_SLEEP_MODE1		(0x1C)
#define PM8607_SLEEP_MODE2		(0x1D)
#define PM8607_SLEEP_MODE3		(0x1E)
#define PM8607_SLEEP_MODE4		(0x1F)
#define PM8607_GO			(0x20)
#define PM8607_SLEEP_BUCK1		(0x21)
#define PM8607_SLEEP_BUCK2		(0x22)
#define PM8607_SLEEP_BUCK3		(0x23)
#define PM8607_BUCK1			(0x24)
#define PM8607_BUCK2			(0x25)
#define PM8607_BUCK3			(0x26)
#define PM8607_BUCK_CONTROLS		(0x27)
#define PM8607_SUPPLIES_EN11		(0x2B)
#define PM8607_SUPPLIES_EN12		(0x2C)
#define PM8607_GROUP1			(0x2D)
#define PM8607_GROUP2			(0x2E)
#define PM8607_GROUP3			(0x2F)
#define PM8607_GROUP4			(0x30)
#define PM8607_GROUP5			(0x31)
#define PM8607_GROUP6			(0x32)
#define PM8607_SUPPLIES_EN21		(0x33)
#define PM8607_SUPPLIES_EN22		(0x34)

/* RTC Control Registers */
#define PM8607_RTC1			(0xA0)
#define PM8607_RTC_COUNTER1		(0xA1)
#define PM8607_RTC_COUNTER2		(0xA2)
#define PM8607_RTC_COUNTER3		(0xA3)
#define PM8607_RTC_COUNTER4		(0xA4)
#define PM8607_RTC_EXPIRE1		(0xA5)
#define PM8607_RTC_EXPIRE2		(0xA6)
#define PM8607_RTC_EXPIRE3		(0xA7)
#define PM8607_RTC_EXPIRE4		(0xA8)
#define PM8607_RTC_TRIM1		(0xA9)
#define PM8607_RTC_TRIM2		(0xAA)
#define PM8607_RTC_TRIM3		(0xAB)
#define PM8607_RTC_TRIM4		(0xAC)
#define PM8607_RTC_MISC1		(0xAD)
#define PM8607_RTC_MISC2		(0xAE)
#define PM8607_RTC_MISC3		(0xAF)

/* Misc Registers */
#define PM8607_CHIP_ID			(0x00)
#define PM8607_B0_MISC1			(0x0C)
#define PM8607_LDO1			(0x10)
#define PM8607_DVC3			(0x26)
#define PM8607_A1_MISC1			(0x40)

/* bit definitions of Status Query Interface */
#define PM8607_STATUS_CC		(1 << 3)
#define PM8607_STATUS_PEN		(1 << 4)
#define PM8607_STATUS_HEADSET		(1 << 5)
#define PM8607_STATUS_HOOK		(1 << 6)
#define PM8607_STATUS_MICIN		(1 << 7)
#define PM8607_STATUS_ONKEY		(1 << 8)
#define PM8607_STATUS_EXTON		(1 << 9)
#define PM8607_STATUS_CHG		(1 << 10)
#define PM8607_STATUS_BAT		(1 << 11)
#define PM8607_STATUS_VBUS		(1 << 12)
#define PM8607_STATUS_OV		(1 << 13)

/* bit definitions of BUCK3 */
#define PM8607_BUCK3_DOUBLE		(1 << 6)

/* bit definitions of Misc1 */
#define PM8607_A1_MISC1_PI2C		(1 << 0)
#define PM8607_B0_MISC1_INV_INT		(1 << 0)
#define PM8607_B0_MISC1_INT_CLEAR	(1 << 1)
#define PM8607_B0_MISC1_INT_MASK	(1 << 2)
#define PM8607_B0_MISC1_PI2C		(1 << 3)
#define PM8607_B0_MISC1_RESET		(1 << 6)

/* Interrupt Number in 88PM8607 */
enum {
	PM8607_IRQ_ONKEY = 0,
	PM8607_IRQ_EXTON,
	PM8607_IRQ_CHG,
	PM8607_IRQ_BAT,
	PM8607_IRQ_RTC,
	PM8607_IRQ_VBAT = 8,
	PM8607_IRQ_VCHG,
	PM8607_IRQ_VSYS,
	PM8607_IRQ_TINT,
	PM8607_IRQ_GPADC0,
	PM8607_IRQ_GPADC1,
	PM8607_IRQ_GPADC2,
	PM8607_IRQ_GPADC3,
	PM8607_IRQ_AUDIO_SHORT = 16,
	PM8607_IRQ_PEN,
	PM8607_IRQ_HEADSET,
	PM8607_IRQ_HOOK,
	PM8607_IRQ_MICIN,
	PM8607_IRQ_CHG_FAIL,
	PM8607_IRQ_CHG_DONE,
	PM8607_IRQ_CHG_FAULT,
};

enum {
	PM8607_CHIP_A0 = 0x40,
	PM8607_CHIP_A1 = 0x41,
	PM8607_CHIP_B0 = 0x48,
};

#define PM860X_NUM_IRQ		24

struct pm860x_irq {
	irq_handler_t		handler;
	void			*data;
};

struct pm860x_chip {
	struct device		*dev;
	struct mutex		io_lock;
	struct mutex		irq_lock;
	struct i2c_client	*client;
	struct i2c_client	*companion;	/* companion chip client */
	struct pm860x_irq	irq[PM860X_NUM_IRQ];

	int			buck3_double;	/* DVC ramp slope double */
	unsigned short		companion_addr;
	int			id;
	int			irq_mode;
	int			chip_irq;
	unsigned char		chip_version;

};

#define PM8607_MAX_REGULATOR	15	/* 3 Bucks, 12 LDOs */

enum {
	GI2C_PORT = 0,
	PI2C_PORT,
};

struct pm860x_platform_data {
	unsigned short	companion_addr;	/* I2C address of companion chip */
	int		i2c_port;	/* Controlled by GI2C or PI2C */
	int		irq_mode;	/* Clear interrupt by read/write(0/1) */
	struct regulator_init_data *regulator[PM8607_MAX_REGULATOR];
};

extern int pm860x_reg_read(struct i2c_client *, int);
extern int pm860x_reg_write(struct i2c_client *, int, unsigned char);
extern int pm860x_bulk_read(struct i2c_client *, int, int, unsigned char *);
extern int pm860x_bulk_write(struct i2c_client *, int, int, unsigned char *);
extern int pm860x_set_bits(struct i2c_client *, int, unsigned char,
			   unsigned char);

extern int pm860x_mask_irq(struct pm860x_chip *, int);
extern int pm860x_unmask_irq(struct pm860x_chip *, int);
extern int pm860x_request_irq(struct pm860x_chip *, int,
			      irq_handler_t handler, void *);
extern int pm860x_free_irq(struct pm860x_chip *, int);

extern int pm860x_device_init(struct pm860x_chip *chip,
			      struct pm860x_platform_data *pdata);
extern void pm860x_device_exit(struct pm860x_chip *chip);

#endif /* __LINUX_MFD_88PM860X_H */
