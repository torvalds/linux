/*
 *  include/linux/mfd/rt5025-misc.h
 *  Include header file for Richtek RT5025 PMIC Misc
 *
 *  Copyright (C) 2013 Richtek Electronics
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_RT5025_MISC_H
#define __LINUX_RT5025_MISC_H

#define RT5025_RESETCTRL_REG	0x15
#define RT5025_VSYSULVO_REG	0x17
#define RT5025_PWRONCTRL_REG	0x19
#define RT5025_SHDNCTRL_REG	0x1A
#define RT5025_PWROFFEN_REG	0x1B

#define RT5025_SHDNCTRL_MASK	0x80
#define RT5025_VSYSOFF_MASK	0xE0

#endif /* #ifndef __LINUX_RT5025_MISC_H */
