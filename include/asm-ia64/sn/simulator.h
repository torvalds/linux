/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 * Copyright (C) 2000-2004 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef _ASM_IA64_SN_SIMULATOR_H
#define _ASM_IA64_SN_SIMULATOR_H


#define SNMAGIC 0xaeeeeeee8badbeefL
#define IS_MEDUSA()			({long sn; asm("mov %0=cpuid[%1]" : "=r"(sn) : "r"(2)); sn == SNMAGIC;})

#define SIMULATOR_SLEEP()		asm("nop.i 0x8beef")
#define IS_RUNNING_ON_SIMULATOR()	(sn_prom_type)
#define IS_RUNNING_ON_FAKE_PROM()	(sn_prom_type == 2)
extern int sn_prom_type;		/* 0=hardware, 1=medusa/realprom, 2=medusa/fakeprom */

#endif /* _ASM_IA64_SN_SIMULATOR_H */
