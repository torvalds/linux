/*
 * include/asm-sh/cpu-sh4/freq.h
 *
 * Copyright (C) 2002, 2003 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH4_FREQ_H
#define __ASM_CPU_SH4_FREQ_H

#if defined(CONFIG_CPU_SUBTYPE_SH73180)
#define FRQCR		        0xa4150000
#else
#define FRQCR			0xffc00000
#endif
#define MIN_DIVISOR_NR		0
#define MAX_DIVISOR_NR		3

#endif /* __ASM_CPU_SH4_FREQ_H */

