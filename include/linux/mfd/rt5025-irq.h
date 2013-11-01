/*
 *  include/linux/mfd/rt5025-irq.h
 *  Include header file for Richtek RT5025 PMIC IRQ file
 *
 *  Copyright (C) 2013 Richtek Electronics
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_RT5025_IRQ_H
#define __LINUX_RT5025_IRQ_H

#define RT5025_REG_CHGSTAT	0x01

#define RT5025_REG_IRQEN1	0x30
#define RT5025_REG_IRQSTATUS1	0x31
#define RT5025_REG_IRQEN2	0x32
#define RT5025_REG_IRQSTATUS2	0x33
#define RT5025_REG_IRQEN3	0x34
#define RT5025_REG_IRQSTATUS3	0x35
#define RT5025_REG_IRQEN4	0x36
#define RT5025_REG_IRQSTATUS4	0x37
#define RT5025_REG_IRQEN5	0x38
#define RT5025_REG_IRQSTATUS5	0x39

#define RT5025_INACIRQ_MASK	0x40
#define RT5025_INUSBIRQ_MASK	0x08
#define RT5025_ADAPIRQ_MASK	(RT5025_INACIRQ_MASK|RT5025_INUSBIRQ_MASK)
#define RT5025_CHTERMI_MASK	0x01

#define RT5025_REG_GAUGEIRQEN	0x50
#define RT5025_REG_GAUGEIRQFLG	0x51
#define RT5025_FLG_TEMP		0x30
#define RT5025_FLG_VOLT		0x07

#endif /* #ifndef __LINUX_RT5025_IRQ_H */
