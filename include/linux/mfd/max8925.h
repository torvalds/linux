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

#include <linux/mutex.h>
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
	MAX8925_ID_MAX,
};

enum {
	/*
	 * Charging current threshold trigger going from fast charge
	 * to TOPOFF charge. From 5% to 20% of fasting charging current.
	 */
	MAX8925_TOPOFF_THR_5PER,
	MAX8925_TOPOFF_THR_10PER,
	MAX8925_TOPOFF_THR_15PER,
	MAX8925_TOPOFF_THR_20PER,
};

enum {
	/* Fast charging current */
	MAX8925_FCHG_85MA,
	MAX8925_FCHG_300MA,
	MAX8925_FCHG_460MA,
	MAX8925_FCHG_600MA,
	MAX8925_FCHG_700MA,
	MAX8925_FCHG_800MA,
	MAX8925_FCHG_900MA,
	MAX8925_FCHG_1000MA,
};

/* Charger registers */
#define MAX8925_CHG_IRQ1		(0x7e)
#define MAX8925_CHG_IRQ2		(0x7f)
#define MAX8925_CHG_IRQ1_MASK		(0x80)
#define MAX8925_CHG_IRQ2_MASK		(0x81)
#define MAX8925_CHG_STATUS		(0x82)

/* GPM registers */
#define MAX8925_SYSENSEL		(0x00)
#define MAX8925_ON_OFF_IRQ1		(0x01)
#define MAX8925_ON_OFF_IRQ1_MASK	(0x02)
#define MAX8925_ON_OFF_STATUS		(0x03)
#define MAX8925_ON_OFF_IRQ2		(0x0d)
#define MAX8925_ON_OFF_IRQ2_MASK	(0x0e)
#define MAX8925_RESET_CNFG		(0x0f)

/* Touch registers */
#define MAX8925_TSC_IRQ			(0x00)
#define MAX8925_TSC_IRQ_MASK		(0x01)
#define MAX8925_TSC_CNFG1		(0x02)
#define MAX8925_ADC_SCHED		(0x10)
#define MAX8925_ADC_RES_END		(0x6f)

#define MAX8925_NREF_OK			(1 << 4)

/* RTC registers */
#define MAX8925_ALARM0_CNTL		(0x18)
#define MAX8925_ALARM1_CNTL		(0x19)
#define MAX8925_RTC_IRQ			(0x1c)
#define MAX8925_RTC_IRQ_MASK		(0x1d)
#define MAX8925_MPL_CNTL		(0x1e)

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

#define MAX8925_MAX_REGULATOR		(23)

#define MAX8925_NAME_SIZE		(32)

/* IRQ definitions */
enum {
	MAX8925_IRQ_VCHG_DC_OVP,
	MAX8925_IRQ_VCHG_DC_F,
	MAX8925_IRQ_VCHG_DC_R,
	MAX8925_IRQ_VCHG_THM_OK_R,
	MAX8925_IRQ_VCHG_THM_OK_F,
	MAX8925_IRQ_VCHG_SYSLOW_F,
	MAX8925_IRQ_VCHG_SYSLOW_R,
	MAX8925_IRQ_VCHG_RST,
	MAX8925_IRQ_VCHG_DONE,
	MAX8925_IRQ_VCHG_TOPOFF,
	MAX8925_IRQ_VCHG_TMR_FAULT,
	MAX8925_IRQ_GPM_RSTIN,
	MAX8925_IRQ_GPM_MPL,
	MAX8925_IRQ_GPM_SW_3SEC,
	MAX8925_IRQ_GPM_EXTON_F,
	MAX8925_IRQ_GPM_EXTON_R,
	MAX8925_IRQ_GPM_SW_1SEC,
	MAX8925_IRQ_GPM_SW_F,
	MAX8925_IRQ_GPM_SW_R,
	MAX8925_IRQ_GPM_SYSCKEN_F,
	MAX8925_IRQ_GPM_SYSCKEN_R,
	MAX8925_IRQ_RTC_ALARM1,
	MAX8925_IRQ_RTC_ALARM0,
	MAX8925_IRQ_TSC_STICK,
	MAX8925_IRQ_TSC_NSTICK,
	MAX8925_NR_IRQS,
};

struct max8925_chip {
	struct device		*dev;
	struct i2c_client	*i2c;
	struct i2c_client	*adc;
	struct i2c_client	*rtc;
	struct mutex		io_lock;
	struct mutex		irq_lock;

	int			irq_base;
	int			core_irq;
	int			tsc_irq;

	unsigned int            wakeup_flag;
};

struct max8925_backlight_pdata {
	int	lxw_scl;	/* 0/1 -- 0.8Ohm/0.4Ohm */
	int	lxw_freq;	/* 700KHz ~ 1400KHz */
	int	dual_string;	/* 0/1 -- single/dual string */
};

struct max8925_touch_pdata {
	unsigned int		flags;
};

struct max8925_power_pdata {
	int		(*set_charger)(int);
	unsigned	batt_detect:1;
	unsigned	topoff_threshold:2;
	unsigned	fast_charge:3;	/* charge current */
	unsigned	no_temp_support:1; /* set if no temperature detect */
	unsigned	no_insert_detect:1; /* set if no ac insert detect */
	char		**supplied_to;
	int		num_supplicants;
};

/*
 * irq_base: stores IRQ base number of MAX8925 in platform
 * tsc_irq: stores IRQ number of MAX8925 TSC
 */
struct max8925_platform_data {
	struct max8925_backlight_pdata	*backlight;
	struct max8925_touch_pdata	*touch;
	struct max8925_power_pdata	*power;
	struct regulator_init_data	*regulator[MAX8925_MAX_REGULATOR];

	int		irq_base;
	int		tsc_irq;
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

