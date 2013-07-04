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
#define RT5025_REG_IRQEN2	0x32
#define RT5025_REG_IRQEN3	0x34
#define RT5025_REG_IRQEN4	0x36
#define RT5025_REG_IRQEN5	0x38

#endif /* #ifndef __LINUX_RT5025_IRQ_H */
