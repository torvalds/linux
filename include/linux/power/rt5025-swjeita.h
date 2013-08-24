/*
 *  include/linux/power/rt5025-swjeita.h
 *  Include header file for Richtek RT5025 Core Jeita Driver
 *
 *  Copyright (C) 2013 Richtek Electronics
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_RT5025_SWJEITA_H
#define __LINUX_RT5025_SWJEITA_H

#define RT5025_REG_CHGCTL3	0x03
#define RT5025_REG_CHGCTL4	0x04
#define RT5025_REG_CHGCTL7	0x07
#define RT5025_REG_AINH		0x5E
#define RT5025_REG_CHANNELL	0x63
#define RT5025_REG_IRQCTL	0x50
#define RT5025_REG_TALRTMAX	0x56
#define RT5025_REG_TALRTMIN	0x57
#define RT5025_REG_INTTEMP_MSB	0x5A

#define RT5025_CHGCCEN_MASK	0x10
#define RT5025_CHGICC_SHIFT	3
#define RT5025_CHGICC_MASK	0x78
#define RT5025_CHGCV_SHIFT	2
#define RT5025_CHGCV_MASK	0xFC
#define RT5025_AINEN_MASK	0x04
#define RT5025_INTEN_MASK	0x40
#define RT5025_TMXEN_MASK	0x20
#define RT5025_TMNEN_MASK	0x10


#endif /* #ifndef __LINUX_RT5025_SWJEITA_H */
