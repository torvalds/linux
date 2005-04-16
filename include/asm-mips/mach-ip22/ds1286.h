/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998, 2001, 03 by Ralf Baechle
 *
 * RTC routines for PC style attached Dallas chip.
 */
#ifndef __ASM_MACH_IP22_DS1286_H
#define __ASM_MACH_IP22_DS1286_H

#include <asm/sgi/hpc3.h>

#define rtc_read(reg)		(hpc3c0->rtcregs[(reg)] & 0xff)
#define rtc_write(data, reg)	do { hpc3c0->rtcregs[(reg)] = (data); } while(0)

#endif /* __ASM_MACH_IP22_DS1286_H */
