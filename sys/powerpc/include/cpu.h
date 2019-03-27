/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 1995-1997 Wolfgang Solfrank.
 * Copyright (C) 1995-1997 TooLs GmbH.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	$NetBSD: cpu.h,v 1.11 2000/05/26 21:19:53 thorpej Exp $
 * $FreeBSD$
 */

#ifndef _MACHINE_CPU_H_
#define	_MACHINE_CPU_H_

#include <machine/frame.h>
#include <machine/pcb.h>
#include <machine/psl.h>

/*
 * CPU Feature Attributes
 *
 * These are defined in the PowerPC ELF ABI for the AT_HWCAP vector,
 * and are exported to userland via the machdep.cpu_features
 * sysctl.
 */

extern u_long cpu_features;
extern u_long cpu_features2;

#define	PPC_FEATURE_32		0x80000000	/* Always true */
#define	PPC_FEATURE_64		0x40000000	/* Defined on a 64-bit CPU */
#define	PPC_FEATURE_601_INSTR	0x20000000	/* Defined on a 64-bit CPU */
#define	PPC_FEATURE_HAS_ALTIVEC	0x10000000	
#define	PPC_FEATURE_HAS_FPU	0x08000000
#define	PPC_FEATURE_HAS_MMU	0x04000000
#define	PPC_FEATURE_UNIFIED_CACHE 0x01000000
#define	PPC_FEATURE_HAS_SPE	0x00800000
#define	PPC_FEATURE_HAS_EFP_SINGLE	0x00400000
#define	PPC_FEATURE_HAS_EFP_DOUBLE	0x00200000
#define	PPC_FEATURE_NO_TB	0x00100000
#define	PPC_FEATURE_POWER4	0x00080000
#define	PPC_FEATURE_POWER5	0x00040000
#define	PPC_FEATURE_POWER5_PLUS	0x00020000
#define	PPC_FEATURE_CELL	0x00010000
#define	PPC_FEATURE_BOOKE	0x00008000
#define	PPC_FEATURE_SMT		0x00004000
#define	PPC_FEATURE_ICACHE_SNOOP	0x00002000
#define	PPC_FEATURE_ARCH_2_05	0x00001000
#define	PPC_FEATURE_HAS_DFP	0x00000400
#define	PPC_FEATURE_POWER6_EXT	0x00000200
#define	PPC_FEATURE_ARCH_2_06	0x00000100
#define	PPC_FEATURE_HAS_VSX	0x00000080
#define	PPC_FEATURE_TRUE_LE	0x00000002
#define	PPC_FEATURE_PPC_LE	0x00000001

#define	PPC_FEATURE2_ARCH_2_07	0x80000000
#define	PPC_FEATURE2_HTM	0x40000000
#define	PPC_FEATURE2_DSCR	0x20000000
#define	PPC_FEATURE2_ISEL	0x08000000
#define	PPC_FEATURE2_TAR	0x04000000
#define	PPC_FEATURE2_HAS_VEC_CRYPTO	0x02000000
#define	PPC_FEATURE2_HTM_NOSC	0x01000000
#define	PPC_FEATURE2_ARCH_3_00	0x00800000
#define	PPC_FEATURE2_HAS_IEEE128	0x00400000
#define	PPC_FEATURE2_DARN	0x00200000
#define	PPC_FEATURE2_SCV	0x00100000
#define	PPC_FEATURE2_HTM_NOSUSPEND	0x01000000

#define	PPC_FEATURE_BITMASK						\
	"\20"								\
	"\040PPC32\037PPC64\036PPC601\035ALTIVEC\034FPU\033MMU\031UNIFIEDCACHE"	\
	"\030SPE\027SPESFP\026DPESFP\025NOTB\024POWER4\023POWER5\022P5PLUS\021CELL"\
	"\020BOOKE\017SMT\016ISNOOP\015ARCH205\013DFP\011ARCH206\010VSX"\
	"\002TRUELE\001PPCLE"
#define	PPC_FEATURE2_BITMASK						\
	"\20"								\
	"\040ARCH207\037HTM\036DSCR\034ISEL\033TAR\032VCRYPTO\031HTMNOSC" \
	"\030ARCH300\027IEEE128\026DARN\025SCV\024HTMNOSUSP"

#define	TRAPF_USERMODE(frame)	(((frame)->srr1 & PSL_PR) != 0)
#define	TRAPF_PC(frame)		((frame)->srr0)

/*
 * CTL_MACHDEP definitions.
 */
#define	CPU_CACHELINE	1

static __inline u_int64_t
get_cyclecount(void)
{
	u_int32_t _upper, _lower;
	u_int64_t _time;

	__asm __volatile(
		"mftb %0\n"
		"mftbu %1"
		: "=r" (_lower), "=r" (_upper));

	_time = (u_int64_t)_upper;
	_time = (_time << 32) + _lower;
	return (_time);
}

#define	cpu_getstack(td)	((td)->td_frame->fixreg[1])
#define	cpu_spinwait()		__asm __volatile("or 27,27,27") /* yield */
#define	cpu_lock_delay()	DELAY(1)

extern char btext[];
extern char etext[];

#ifdef __powerpc64__
extern void enter_idle_powerx(void);
extern uint64_t can_wakeup;
extern register_t lpcr;
#endif

void	cpu_halt(void);
void	cpu_reset(void);
void	cpu_sleep(void);
void	flush_disable_caches(void);
void	fork_trampoline(void);
void	swi_vm(void *);

#endif	/* _MACHINE_CPU_H_ */
