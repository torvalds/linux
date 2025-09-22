/*	$OpenBSD: i82489reg.h,v 1.5 2019/07/26 04:35:38 kevlo Exp $	*/
/*	$NetBSD: i82489reg.h,v 1.1.2.1 2000/02/20 16:30:27 sommerfeld Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Frank van der Linden.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


/*
 * Registers and constants for the 82489DX and Pentium (and up) integrated
 * "local" APIC.
 */

#define	LAPIC_ID		0x020		/* ID. RW */
#	define LAPIC_ID_MASK		0x0f000000
#	define LAPIC_ID_SHIFT		24

#define LAPIC_VERS		0x030		/* Version. R */
#	define LAPIC_VERSION_MASK	0x000000ff
#	define LAPIC_VERSION_LVT_MASK	0x00ff0000
#	define LAPIC_VERSION_LVT_SHIFT	16

#define LAPIC_TPRI		0x080		/* Task Prio. RW */
#	define LAPIC_TPRI_MASK		0x000000ff
#	define LAPIC_TPRI_INT_MASK	0x000000f0
#	define LAPIC_TPRI_SUB_MASK	0x0000000f

#define LAPIC_APRI		0x090		/* Arbitration prio R */
#	define LAPIC_APRI_MASK		0x000000ff

#define LAPIC_PPRI		0x0a0		/* Processor prio. R */
#define LAPIC_EOI		0x0b0		/* End Int. W */
#define LAPIC_RRR		0x0c0		/* Remote read R */
#define LAPIC_LDR		0x0d0		/* Logical dest. RW */
#define LAPIC_DFR		0x0e0		/* Dest. format RW */

#define LAPIC_SVR		0x0f0		/* Spurious intvec RW */
#	define LAPIC_SVR_VECTOR_MASK	0x000000ff
#	define LAPIC_SVR_VEC_FIX	0x0000000f
#	define LAPIC_SVR_VEC_PROG	0x000000f0
#	define LAPIC_SVR_ENABLE		0x00000100
#	define LAPIC_SVR_SWEN		0x00000100
#	define LAPIC_SVR_FOCUS		0x00000200
#	define LAPIC_SVR_FDIS		0x00000200

#define LAPIC_ISR	0x100			/* Int. status. R */
#define LAPIC_TMR	0x180
#define LAPIC_IRR	0x200
#define LAPIC_ESR	0x280			/* Err status. R */

#define LAPIC_ICRLO	0x300			/* Int. cmd. RW */
#	define LAPIC_DLMODE_MASK	0x00000700
#	define LAPIC_DLMODE_FIXED	0x00000000
#	define LAPIC_DLMODE_LOW		0x00000100
#	define LAPIC_DLMODE_SMI		0x00000200
#	define LAPIC_DLMODE_RR		0x00000300
#	define LAPIC_DLMODE_NMI		0x00000400
#	define LAPIC_DLMODE_INIT	0x00000500
#	define LAPIC_DLMODE_STARTUP	0x00000600

#	define LAPIC_DSTMODE_LOG	0x00000800

#	define LAPIC_DLSTAT_BUSY	0x00001000

#	define LAPIC_LVL_ASSERT		0x00004000
#	define LAPIC_LVL_DEASSERT	0x00000000

#	define LAPIC_LVL_TRIG		0x00008000

#	define LAPIC_RRSTAT_MASK	0x00030000
#	define LAPIC_RRSTAT_INPROG	0x00010000
#	define LAPIC_RRSTAT_VALID	0x00020000

#	define LAPIC_DEST_MASK		0x000c0000
#	define LAPIC_DEST_SELF		0x00040000
#	define LAPIC_DEST_ALLINCL	0x00080000
#	define LAPIC_DEST_ALLEXCL	0x000c0000

#	define LAPIC_RESV2_MASK		0xfff00000


#define LAPIC_ICRHI	0x310			/* Int. cmd. RW */

#define LAPIC_LVTT	0x320			/* Loc.vec.(timer) RW */
#	define LAPIC_LVTT_VEC_MASK	0x000000ff
#	define LAPIC_LVTT_DS		0x00001000
#	define LAPIC_LVTT_M		0x00010000
#	define LAPIC_LVTT_TM		0x00020000
#	 define LAPIC_LVTT_TM_ONESHOT	0x00000000
#	 define LAPIC_LVTT_TM_PERIODIC	0x00020000
#	 define LAPIC_LVTT_TM_TSCDL	0x00040000

#define LAPIC_PCINT	0x340
#define LAPIC_LVINT0	0x350			/* Loc.vec (LINT0) RW */
#	define LAPIC_LVT_PERIODIC	0x00020000
#	define LAPIC_LVT_MASKED		0x00010000
#	define LAPIC_LVT_LEVTRIG	0x00008000
#	define LAPIC_LVT_REMOTE_IRR	0x00004000
#	define LAPIC_INP_POL		0x00002000
#	define LAPIC_PEND_SEND		0x00001000

#define LAPIC_LVINT1	0x360			/* Loc.vec (LINT1) RW */
#define LAPIC_LVERR	0x370			/* Loc.vec (ERROR) RW */
#define LAPIC_ICR_TIMER	0x380			/* Initial count RW */
#define LAPIC_CCR_TIMER	0x390			/* Current count RO */

#define LAPIC_DCR_TIMER	0x3e0			/* Divisor config register */
#	define LAPIC_DCRT_DIV1		0x0b
#	define LAPIC_DCRT_DIV2		0x00
#	define LAPIC_DCRT_DIV4		0x01
#	define LAPIC_DCRT_DIV8		0x02
#	define LAPIC_DCRT_DIV16		0x03
#	define LAPIC_DCRT_DIV32		0x08
#	define LAPIC_DCRT_DIV64		0x09
#	define LAPIC_DCRT_DIV128	0x0a

#define LAPIC_BASE		0xfee00000

#define LAPIC_IRQ_MASK(i)	(1 << ((i) + 1))
