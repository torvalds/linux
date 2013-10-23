/*
 *  include/linux/power/rt5025-power.h
 *  Include header file for Richtek RT5025 Core Power Driver
 *
 *  Copyright (C) 2013 Richtek Electronics
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_RT5025_POWER_H
#define __LINUX_RT5025_POWER_H

#define RT5025_REG_CHGSTAT 0x01
#define RT5025_REG_CHGCTL2 0x02
#define RT5025_REG_CHGCTL3 0x03
#define RT5025_REG_CHGCTL4 0x04
#define RT5025_REG_CHGCTL5 0x05
#define RT5025_REG_CHGCTL6 0x06
#define RT5025_REG_CHGCTL7 0x07

#define RT5025_CHGRST_MASK 0x80

#define RT5025_CHGBUCKEN_MASK 0x02
#define RT5025_CHGCEN_MASK    0x10
#define RT5025_CHGAICR_MASK   0x06

#define RT5025_CHGSTAT_MASK  0x30
#define RT5025_CHGSTAT_SHIFT 4
#define RT5025_CHGSTAT_UNKNOWN 0x04

#ifdef CONFIG_RT5025_SUPPORT_ACUSB_DUALIN
#define RT5025_CHG_ACONLINE   0x02
#define RT5025_CHG_ACSHIFT    1
#define RT5025_CHG_USBONLINE  0x01
#define RT5025_CHG_USBSHIFT   0
#else
#define RT5025_CHG_ACONLINE   0x01
#define RT5025_CHG_ACSHIFT    0
#define RT5025_CHG_USBONLINE  0x02
#define RT5025_CHG_USBSHIFT   1
#endif /* CONFIG_RT5025_SUPPORT_ACUSB_DUALIN */

#endif /* #ifndef __LINUX_RT5025_POWER_H */
