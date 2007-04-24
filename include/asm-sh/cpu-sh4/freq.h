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

#if defined(CONFIG_CPU_SUBTYPE_SH73180) || defined(CONFIG_CPU_SUBTYPE_SH7722)
#define FRQCR		        0xa4150000
#define VCLKCR			0xa4150004
#define SCLKACR			0xa4150008
#define SCLKBCR			0xa415000c
#define IrDACLKCR		0xa4150010
#elif defined(CONFIG_CPU_SUBTYPE_SH7780)
#define	FRQCR			0xffc80000
#elif defined(CONFIG_CPU_SUBTYPE_SH7785)
#define FRQCR0			0xffc80000
#define FRQCR1			0xffc80004
#define FRQMR1			0xffc80014
#else
#define FRQCR			0xffc00000
#endif
#define MIN_DIVISOR_NR		0
#define MAX_DIVISOR_NR		3

#endif /* __ASM_CPU_SH4_FREQ_H */

