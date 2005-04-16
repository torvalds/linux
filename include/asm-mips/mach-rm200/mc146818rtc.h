/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 by Ralf Baechle
 *
 * RTC routines for PC style attached Dallas chip with ARC epoch.
 */
#ifndef __ASM_MACH_RM200_MC146818RTC_H
#define __ASM_MACH_RM200_MC146818RTC_H

#define mc146818_decode_year(year) ((year) + 1980)

#include_next <mc146818rtc.h>

#endif /* __ASM_MACH_RM200_MC146818RTC_H */
