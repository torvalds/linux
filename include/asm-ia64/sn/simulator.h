/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 * Copyright (C) 2000-2004 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef _ASM_IA64_SN_SIMULATOR_H
#define _ASM_IA64_SN_SIMULATOR_H

#include <linux/config.h>

#ifdef CONFIG_IA64_SGI_SN_SIM

#define SNMAGIC 0xaeeeeeee8badbeefL
#define IS_RUNNING_ON_SIMULATOR() ({long sn; asm("mov %0=cpuid[%1]" : "=r"(sn) : "r"(2)); sn == SNMAGIC;})

#define SIMULATOR_SLEEP()	asm("nop.i 0x8beef")

#else

#define IS_RUNNING_ON_SIMULATOR()	(0)
#define SIMULATOR_SLEEP()

#endif

#endif /* _ASM_IA64_SN_SIMULATOR_H */
