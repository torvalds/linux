/*
 * include/asm-sh/cpu-sh4/timer.h
 *
 * Copyright (C) 2004 Lineo Solutions, Inc. 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH4_TIMER_H
#define __ASM_CPU_SH4_TIMER_H

/*
 * ---------------------------------------------------------------------------
 * TMU Common definitions for SH4 processors
 *	SH7750S/SH7750R
 *	SH7751/SH7751R
 *	SH7760
 * ---------------------------------------------------------------------------
 */

#if !defined(CONFIG_CPU_SUBTYPE_SH7760)
#define TMU_TOCR        0xffd80000      /* Byte access */
#endif
#define TMU_TSTR        0xffd80004      /* Byte access */

#define TMU0_TCOR       0xffd80008      /* Long access */
#define TMU0_TCNT       0xffd8000c      /* Long access */
#define TMU0_TCR        0xffd80010      /* Word access */

#define TMU1_TCOR       0xffd80014      /* Long access */
#define TMU1_TCNT       0xffd80018      /* Long access */
#define TMU1_TCR        0xffd8001c      /* Word access */

#define TMU2_TCOR       0xffd80020      /* Long access */
#define TMU2_TCNT       0xffd80024      /* Long access */
#define TMU2_TCR        0xffd80028      /* Word access */
#define TMU2_TCPR	0xffd8002c	/* Long access */

#if !defined(CONFIG_CPU_SUBTYPE_SH7760)
#define TMU3_TCOR       0xfe100008      /* Long access */
#define TMU3_TCNT       0xfe10000c      /* Long access */
#define TMU3_TCR        0xfe100010      /* Word access */

#define TMU4_TCOR       0xfe100014      /* Long access */
#define TMU4_TCNT       0xfe100018      /* Long access */
#define TMU4_TCR        0xfe10001c      /* Word access */
#endif

#endif /* __ASM_CPU_SH4_TIMER_H */

