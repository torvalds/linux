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

/* RTC registers */
#define MAX8925_RTC_STATUS		(0x1a)
#define MAX8925_RTC_IRQ			(0x1c)
#define MAX8925_RTC_IRQ_MASK		(0x1d)

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

struct max8925_platform_data {
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

