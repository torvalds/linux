/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 by Ralf Baechle
 */
#ifndef __ASM_MACH_GENERIC_TIMEX_H
#define __ASM_MACH_GENERIC_TIMEX_H

#include <linux/config.h>

/*
 * Last remaining user of the i8254 PIC, will be converted, too ...
 */
#ifdef CONFIG_SNI_RM200_PCI
#define CLOCK_TICK_RATE		1193182
#else
#define CLOCK_TICK_RATE		500000
#endif

#endif /* __ASM_MACH_GENERIC_TIMEX_H */
