/*
 * Maxim8925 Interface
 *
 * Copyright (C) 2009 Marvell International Ltd.
 *	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MFD_MAX8925_H
#define __LINUX_MFD_MAX8925_H

#include <linux/interrupt.h>

/* Unified sub device IDs for MAX8925 */
enum {
	MAX8925_ID_SD1,
	MAX8925_ID_SD2,
	MAX8925_ID_SD3,
	MAX8925_ID_LDO1,
	MAX8925_ID_LDO2,
	MAX8925_ID_LDO3,
	MAX8925_ID_LDO4,
	MAX8925_ID_LDO5,
	MAX8925_ID_LDO6,
	MAX8925_ID_LDO7,
	MAX8925_ID_LDO8,
	MAX8925_ID_LDO9,
	MAX8925_ID_LDO10,
	MAX8925_ID_LDO11,
	MAX8925_ID_LDO12,
	MAX8925_ID_LDO13,
	MAX8925_ID_LDO14,
	MAX8925_ID_LDO15,
	MAX8925_ID_LDO16,
	MAX8925_ID_LDO17,
	MAX8925_ID_LDO18,
	MAX8925_ID_LDO19,
	MAX8925_ID_LDO20,
};

/* Charger registers */
#define MAX8925_CHG_IRQ1		(0x7e)
#define MAX8925_CHG_IRQ2		(0x7f)
#define MAX8925_CHG_IRQ1_MASK		(0x80)
#define MAX8925_CHG_IRQ2_MASK		(0x81)

/* GPM registers */
#define MAX8925_SYSENSEL		(0x00)
#define MAX8925_ON_OFF_IRQ1		(0x01)
#define MAX8925_ON_OFF_IRQ1_MASK	(0x02)
#define MAX8925_ON_OFF_STAT		(0x03)
#define MAX8925_ON_OFF_IRQ2		(0x0d)
#define MAX8925_ON_OFF_IRQ2_MASK	(0x0e)
#define MAX8925_RESET_CNFG		(0x0f)

/* Touch registers */
#define MAX8925_TSC_IRQ			(0x00)
#define MAX8925_TSC_IRQ_MASK		(0x01)
#define MAX8925_ADC_RES_END		(0x6f)

/* RTC registers */
#define MAX8925_RTC_STATUS		(0x1a)
#define MAX8925_RTC_IRQ			(0x1c)
#define MAX8925_RTC_IRQ_MASK		(0x1d)

/* WLED registers */
#define MAX8925_WLED_MODE_CNTL		(0x84)
#define MAX8925_WLED_CNTL		(0x85)

/* MAX8925 Registers */
#define MAX8925_SDCTL1			(0x04)
#define MAX8925_SDCTL2			(0x07)
#define MAX8925_SDCTL3			(0x0A)
#define MAX8925_SDV1			(0x06)
#define MAX8925_SDV2			(0x09)
#define MAX8925_SDV3			(0x0C)
#define MAX8925_LDOCTL1			(0x18)
#define MAX8925_LDOCTL2			(0x1C)
#define MAX8925_LDOCTL3			(0x20)
#define MAX8925_LDOCTL4			(0x24)
#define MAX8925_LDOCTL5			(0x28)
#define MAX8925_LDOCTL6			(0x2C)
#define MAX8925_LDOCTL7			(0x30)
#define MAX8925_LDOCTL8			(0x34)
#define MAX8925_LDOCTL9			(0x38)
#define MAX8925_LDOCTL10		(0x3C)
#define MAX8925_LDOCTL11		(0x40)
#define MAX8925_LDOCTL12		(0x44)
#define MAX8925_LDOCTL13		(0x48)
#define MAX8925_LDOCTL14		(0x4C)
#define MAX8925_LDOCTL15		(0x50)
#define MAX8925_LDOCTL16		(0x10)
#define MAX8925_LDOCTL17		(0x14)
#define MAX8925_LDOCTL18		(0x72)
#define MAX8925_LDOCTL19		(0x5C)
#define MAX8925_LDOCTL20		(0x9C)
#define MAX8925_LDOVOUT1		(0x1A)
#define MAX8925_LDOVOUT2		(0x1E)
#define MAX8925_LDOVOUT3		(0x22)
#define MAX8925_LDOVOUT4		(0x26)
#define MAX8925_LDOVOUT5		(0x2A)
#define MAX8925_LDOVOUT6		(0x2E)
#define MAX8925_LDOVOUT7		(0x32)
#define MAX8925_LDOVOUT8		(0x36)
#define MAX8925_LDOVOUT9		(0x3A)
#define MAX8925_LDOVOUT10		(0x3E)
#define MAX8925_LDOVOUT11		(0x42)
#define MAX8925_LDOVOUT12		(0x46)
#define MAX8925_LDOVOUT13		(0x4A)
#define MAX8925_LDOVOUT14		(0x4E)
#define MAX8925_LDOVOUT15		(0x52)
#define MAX8925_LDOVOUT16		(0x12)
#define MAX8925_LDOVOUT17		(0x16)
#define MAX8925_LDOVOUT18		(0x74)
#define MAX8925_LDOVOUT19		(0x5E)
#define MAX8925_LDOVOUT20		(0x9E)

/* bit definitions */
#define CHG_IRQ1_MASK			(0x07)
#define CHG_IRQ2_MASK			(0xff)
#define ON_OFF_IRQ1_MASK		(0xff)
#define ON_OFF_IRQ2_MASK		(0x03)
#define TSC_IRQ_MASK			(0x03)
#define RTC_IRQ_MASK			(0x0c)

#define MAX8925_NUM_IRQ			(32)

#define MAX8925_NAME_SIZE		(32)

enum {
	MAX8925_INVALID = 0,
	MAX8925_RTC,
	MAX8925_ADC,
	MAX8925_GPM,	/* general power management */
	MAX8925_MAX,
};

#define MAX8925_IRQ_VCHG_OVP		(0)
#define MAX8925_IRQ_VCHG_F		(1)
#define MAX8925_IRQ_VCHG_R		(2)
#define MAX8925_IRQ_VCHG_THM_OK_R	(8)
#define MAX8925_IRQ_VCHG_THM_OK_F	(9)
#define MAX8925_IRQ_VCHG_BATTLOW_F	(10)
#define MAX8925_IRQ_VCHG_BATTLOW_R	(11)
#define MAX8925_IRQ_VCHG_RST		(12)
#define MAX8925_IRQ_VCHG_DONE		(13)
#define MAX8925_IRQ_VCHG_TOPOFF		(14)
#define MAX8925_IRQ_VCHG_TMR_FAULT	(15)
#define MAX8925_IRQ_GPM_RSTIN		(16)
#define MAX8925_IRQ_GPM_MPL		(17)
#define MAX8925_IRQ_GPM_SW_3SEC		(18)
#define MAX8925_IRQ_GPM_EXTON_F		(19)
#define MAX8925_IRQ_GPM_EXTON_R		(20)
#define MAX8925_IRQ_GPM_SW_1SEC		(21)
#define MAX8925_IRQ_GPM_SW_F		(22)
#define MAX8925_IRQ_GPM_SW_R		(23)
#define MAX8925_IRQ_GPM_SYSCKEN_F	(24)
#define MAX8925_IRQ_GPM_SYSCKEN_R	(25)

#define MAX8925_IRQ_TSC_STICK		(0)
#define MAX8925_IRQ_TSC_NSTICK		(1)

#define MAX8925_MAX_REGULATOR		(23)

struct max8925_irq {
	irq_handler_t		handler;
	void			*data;
};

struct max8925_chip {
	struct device		*dev;
	struct mutex		io_lock;
	struct mutex		irq_lock;
	struct i2c_client	*i2c;
	struct max8925_irq	irq[MAX8925_NUM_IRQ];

	const char		*name;
	int			chip_id;
	int			chip_irq;
};

struct max8925_backlight_pdata {
	int	lxw_scl;	/* 0/1 -- 0.8Ohm/0.4Ohm */
	int	lxw_freq;	/* 700KHz ~ 1400KHz */
	int	dual_string;	/* 0/1 -- single/dual string */
};

struct max8925_touch_pdata {
	unsigned int		flags;
};

struct max8925_platform_data {
	struct max8925_backlight_pdata	*backlight;
	struct max8925_touch_pdata	*touch;
	struct regulator_init_data	*regulator[MAX8925_MAX_REGULATOR];

	int	chip_id;
	int	chip_irq;
};

extern int max8925_reg_read(struct i2c_client *, int);
extern int max8925_reg_write(struct i2c_client *, int, unsigned char);
extern int max8925_bulk_read(struct i2c_client *, int, int, unsigned char *);
extern int max8925_bulk_write(struct i2c_client *, int, int, unsigned char *);
extern int max8925_set_bits(struct i2c_client *, int, unsigned char,
			unsigned char);

extern int max8925_device_init(struct max8925_chip *,
				struct max8925_platform_data *);
extern void max8925_device_exit(struct max8925_chip *);
#endif /* __LINUX_MFD_MAX8925_H */

