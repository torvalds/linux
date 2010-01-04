/*
 * Marvell 88PM8607 Interface
 *
 * Copyright (C) 2009 Marvell International Ltd.
 * 	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MFD_88PM8607_H
#define __LINUX_MFD_88PM8607_H

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

#define CHIP_ID				(0x40)
#define CHIP_ID_MASK			(0xF8)

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
#define PM8607_LDO1			(0x10)
#define PM8607_DVC3			(0x26)
#define PM8607_MISC1			(0x40)

/* bit definitions for PM8607 events */
#define PM8607_EVENT_ONKEY		(1 << 0)
#define PM8607_EVENT_EXTON		(1 << 1)
#define PM8607_EVENT_CHG		(1 << 2)
#define PM8607_EVENT_BAT		(1 << 3)
#define PM8607_EVENT_RTC		(1 << 4)
#define PM8607_EVENT_CC			(1 << 5)
#define PM8607_EVENT_VBAT		(1 << 8)
#define PM8607_EVENT_VCHG		(1 << 9)
#define PM8607_EVENT_VSYS		(1 << 10)
#define PM8607_EVENT_TINT		(1 << 11)
#define PM8607_EVENT_GPADC0		(1 << 12)
#define PM8607_EVENT_GPADC1		(1 << 13)
#define PM8607_EVENT_GPADC2		(1 << 14)
#define PM8607_EVENT_GPADC3		(1 << 15)
#define PM8607_EVENT_AUDIO_SHORT	(1 << 16)
#define PM8607_EVENT_PEN		(1 << 17)
#define PM8607_EVENT_HEADSET		(1 << 18)
#define PM8607_EVENT_HOOK		(1 << 19)
#define PM8607_EVENT_MICIN		(1 << 20)
#define PM8607_EVENT_CHG_TIMEOUT	(1 << 21)
#define PM8607_EVENT_CHG_DONE		(1 << 22)
#define PM8607_EVENT_CHG_FAULT		(1 << 23)

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
#define PM8607_MISC1_PI2C		(1 << 0)

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


struct pm8607_chip {
	struct device		*dev;
	struct mutex		io_lock;
	struct i2c_client	*client;

	int (*read)(struct pm8607_chip *chip, int reg, int bytes, void *dest);
	int (*write)(struct pm8607_chip *chip, int reg, int bytes, void *src);

	int			buck3_double;	/* DVC ramp slope double */
	unsigned char		chip_id;

};

#define PM8607_MAX_REGULATOR	15	/* 3 Bucks, 12 LDOs */

enum {
	GI2C_PORT = 0,
	PI2C_PORT,
};

struct pm8607_platform_data {
	int	i2c_port;	/* Controlled by GI2C or PI2C */
	struct regulator_init_data *regulator[PM8607_MAX_REGULATOR];
};

extern int pm8607_reg_read(struct pm8607_chip *, int);
extern int pm8607_reg_write(struct pm8607_chip *, int, unsigned char);
extern int pm8607_bulk_read(struct pm8607_chip *, int, int,
			    unsigned char *);
extern int pm8607_bulk_write(struct pm8607_chip *, int, int,
			     unsigned char *);
extern int pm8607_set_bits(struct pm8607_chip *, int, unsigned char,
			   unsigned char);
#endif /* __LINUX_MFD_88PM8607_H */
