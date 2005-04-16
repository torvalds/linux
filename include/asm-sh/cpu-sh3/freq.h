/*
 * include/asm-sh/cpu-sh3/freq.h
 *
 * Copyright (C) 2002, 2003 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH3_FREQ_H
#define __ASM_CPU_SH3_FREQ_H

#if defined(CONFIG_CPU_SUBTYPE_SH7300)
#define FRQCR			0xa415ff80
#else
#define FRQCR			0xffffff80
#endif
#define MIN_DIVISOR_NR		0
#define MAX_DIVISOR_NR		4

#endif /* __ASM_CPU_SH3_FREQ_H */

