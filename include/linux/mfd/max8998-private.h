/*
 * max8998.h - Voltage regulator driver for the Maxim 8998
 *
 *  Copyright (C) 2009-2010 Samsung Electrnoics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *  Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __LINUX_MFD_MAX8998_PRIV_H
#define __LINUX_MFD_MAX8998_PRIV_H

#define MAX8998_NUM_IRQ_REGS	4

/* MAX 8998 registers */
enum {
	MAX8998_REG_IRQ1,
	MAX8998_REG_IRQ2,
	MAX8998_REG_IRQ3,
	MAX8998_REG_IRQ4,
	MAX8998_REG_IRQM1,
	MAX8998_REG_IRQM2,
	MAX8998_REG_IRQM3,
	MAX8998_REG_IRQM4,
	MAX8998_REG_STATUS1,
	MAX8998_REG_STATUS2,
	MAX8998_REG_STATUSM1,
	MAX8998_REG_STATUSM2,
	MAX8998_REG_CHGR1,
	MAX8998_REG_CHGR2,
	MAX8998_REG_LDO_ACTIVE_DISCHARGE1,
	MAX8998_REG_LDO_ACTIVE_DISCHARGE2,
	MAX8998_REG_BUCK_ACTIVE_DISCHARGE3,
	MAX8998_REG_ONOFF1,
	MAX8998_REG_ONOFF2,
	MAX8998_REG_ONOFF3,
	MAX8998_REG_ONOFF4,
	MAX8998_REG_BUCK1_VOLTAGE1,
	MAX8998_REG_BUCK1_VOLTAGE2,
	MAX8998_REG_BUCK1_VOLTAGE3,
	MAX8998_REG_BUCK1_VOLTAGE4,
	MAX8998_REG_BUCK2_VOLTAGE1,
	MAX8998_REG_BUCK2_VOLTAGE2,
	MAX8998_REG_BUCK3,
	MAX8998_REG_BUCK4,
	MAX8998_REG_LDO2_LDO3,
	MAX8998_REG_LDO4,
	MAX8998_REG_LDO5,
	MAX8998_REG_LDO6,
	MAX8998_REG_LDO7,
	MAX8998_REG_LDO8_LDO9,
	MAX8998_REG_LDO10_LDO11,
	MAX8998_REG_LDO12,
	MAX8998_REG_LDO13,
	MAX8998_REG_LDO14,
	MAX8998_REG_LDO15,
	MAX8998_REG_LDO16,
	MAX8998_REG_LDO17,
	MAX8998_REG_BKCHR,
	MAX8998_REG_LBCNFG1,
	MAX8998_REG_LBCNFG2,
};

/* IRQ definitions */
enum {
	MAX8998_IRQ_DCINF,
	MAX8998_IRQ_DCINR,
	MAX8998_IRQ_JIGF,
	MAX8998_IRQ_JIGR,
	MAX8998_IRQ_PWRONF,
	MAX8998_IRQ_PWRONR,

	MAX8998_IRQ_WTSREVNT,
	MAX8998_IRQ_SMPLEVNT,
	MAX8998_IRQ_ALARM1,
	MAX8998_IRQ_ALARM0,

	MAX8998_IRQ_ONKEY1S,
	MAX8998_IRQ_TOPOFFR,
	MAX8998_IRQ_DCINOVPR,
	MAX8998_IRQ_CHGRSTF,
	MAX8998_IRQ_DONER,
	MAX8998_IRQ_CHGFAULT,

	MAX8998_IRQ_LOBAT1,
	MAX8998_IRQ_LOBAT2,

	MAX8998_IRQ_NR,
};

/* MAX8998 various variants */
enum {
	TYPE_MAX8998 = 0, /* Default */
	TYPE_LP3974,	/* National version of MAX8998 */
	TYPE_LP3979,	/* Added AVS */
};

#define MAX8998_IRQ_DCINF_MASK		(1 << 2)
#define MAX8998_IRQ_DCINR_MASK		(1 << 3)
#define MAX8998_IRQ_JIGF_MASK		(1 << 4)
#define MAX8998_IRQ_JIGR_MASK		(1 << 5)
#define MAX8998_IRQ_PWRONF_MASK		(1 << 6)
#define MAX8998_IRQ_PWRONR_MASK		(1 << 7)

#define MAX8998_IRQ_WTSREVNT_MASK	(1 << 0)
#define MAX8998_IRQ_SMPLEVNT_MASK	(1 << 1)
#define MAX8998_IRQ_ALARM1_MASK		(1 << 2)
#define MAX8998_IRQ_ALARM0_MASK		(1 << 3)

#define MAX8998_IRQ_ONKEY1S_MASK	(1 << 0)
#define MAX8998_IRQ_TOPOFFR_MASK	(1 << 2)
#define MAX8998_IRQ_DCINOVPR_MASK	(1 << 3)
#define MAX8998_IRQ_CHGRSTF_MASK	(1 << 4)
#define MAX8998_IRQ_DONER_MASK		(1 << 5)
#define MAX8998_IRQ_CHGFAULT_MASK	(1 << 7)

#define MAX8998_IRQ_LOBAT1_MASK		(1 << 0)
#define MAX8998_IRQ_LOBAT2_MASK		(1 << 1)

#define MAX8998_ENRAMP                  (1 << 4)

struct irq_domain;

/**
 * struct max8998_dev - max8998 master device for sub-drivers
 * @dev: master device of the chip (can be used to access platform data)
 * @pdata: platform data for the driver and subdrivers
 * @i2c: i2c client private data for regulator
 * @rtc: i2c client private data for rtc
 * @iolock: mutex for serializing io access
 * @irqlock: mutex for buslock
 * @irq_base: base IRQ number for max8998, required for IRQs
 * @irq: generic IRQ number for max8998
 * @ono: power onoff IRQ number for max8998
 * @irq_masks_cur: currently active value
 * @irq_masks_cache: cached hardware value
 * @type: indicate which max8998 "variant" is used
 */
struct max8998_dev {
	struct device *dev;
	struct max8998_platform_data *pdata;
	struct i2c_client *i2c;
	struct i2c_client *rtc;
	struct mutex iolock;
	struct mutex irqlock;

	unsigned int irq_base;
	struct irq_domain *irq_domain;
	int irq;
	int ono;
	u8 irq_masks_cur[MAX8998_NUM_IRQ_REGS];
	u8 irq_masks_cache[MAX8998_NUM_IRQ_REGS];
	int type;
	bool wakeup;
};

int max8998_irq_init(struct max8998_dev *max8998);
void max8998_irq_exit(struct max8998_dev *max8998);
int max8998_irq_resume(struct max8998_dev *max8998);

extern int max8998_read_reg(struct i2c_client *i2c, u8 reg, u8 *dest);
extern int max8998_bulk_read(struct i2c_client *i2c, u8 reg, int count,
		u8 *buf);
extern int max8998_write_reg(struct i2c_client *i2c, u8 reg, u8 value);
extern int max8998_bulk_write(struct i2c_client *i2c, u8 reg, int count,
		u8 *buf);
extern int max8998_update_reg(struct i2c_client *i2c, u8 reg, u8 val, u8 mask);

#endif /*  __LINUX_MFD_MAX8998_PRIV_H */
