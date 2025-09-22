/*	$OpenBSD: trap.h,v 1.15 2025/07/16 07:15:42 jsg Exp $	*/

/*
 * Copyright (c) 1999-2004 Michael Shalayeff
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef	_MACHINE_TRAP_H_
#define	_MACHINE_TRAP_H_

/*
 * This is PA-RISC trap types per 1.1 specs, see .c files for references.
 */
#define	T_NONEXIST	0	/* invalid interrupt vector */
#define	T_HPMC		1	/* high priority machine check */
#define	T_POWERFAIL	2	/* power failure */
#define	T_RECOVERY	3	/* recovery counter */
#define	T_INTERRUPT	4	/* external interrupt */
#define	T_LPMC		5	/* low-priority machine check */
#define	T_ITLBMISS	6	/* instruction TLB miss fault */
#define	T_IPROT		7	/* instruction protection */
#define	T_ILLEGAL	8	/* Illegal instruction */
#define	T_IBREAK	9	/* break instruction */
#define	T_PRIV_OP	10	/* privileged operation */
#define	T_PRIV_REG	11	/* privileged register */
#define	T_OVERFLOW	12	/* overflow */
#define	T_CONDITION	13	/* conditional */
#define	T_EXCEPTION	14	/* assist exception */
#define	T_DTLBMISS	15	/* data TLB miss */
#define	T_ITLBMISSNA	16	/* ITLB non-access miss */
#define	T_DTLBMISSNA	17	/* DTLB non-access miss */
#define	T_DPROT		18	/* data protection/rights/alignment <7100 */
#define	T_DBREAK	19	/* data break */
#define	T_TLB_DIRTY	20	/* TLB dirty bit */
#define	T_PAGEREF	21	/* page reference */
#define	T_EMULATION	22	/* assist emulation */
#define	T_HIGHERPL	23	/* higher-privilege transfer */
#define	T_LOWERPL	24	/* lower-privilege transfer */
#define	T_TAKENBR	25	/* taken branch */
#define	T_DATACC	26	/* data access rights >=7100 */
#define	T_DATAPID	27	/* data protection ID >=7100 */
#define	T_DATALIGN	28	/* unaligned data ref */
#define	T_PERFMON	29	/* performance monitor interrupt */
#define	T_IDEBUG	30	/* debug SFU interrupt */
#define	T_DDEBUG	31	/* debug SFU interrupt */

/*
 * Reserved range for traps is 0-63, place user flag at 6th bit
 */
#define	T_USER_POS	25
#define	T_USER		(1 << (31 - T_USER_POS))

/*
 * Various trap frame flags.
 */
#define	TFF_LAST_POS	0
#define	TFF_SYS_POS	1
#define	TFF_INTR_POS	2

#define	TFF_LAST	(1 << (31 - TFF_LAST_POS))
#define	TFF_SYS		(1 << (31 - TFF_SYS_POS))
#define	TFF_INTR	(1 << (31 - TFF_INTR_POS))

/*
 * These are break instruction entry points.
 */
/* im5 */
#define	HPPA_BREAK_KERNEL	0
/* im13 */
#define HPPA_BREAK_SS		4
#define	HPPA_BREAK_KGDB		5
#define	HPPA_BREAK_GET_PSW	9
#define	HPPA_BREAK_SET_PSW	10
#define	HPPA_BREAK_SPLLOWER	11

/*
 * break instruction decoding.
 */
#define	break5(i)	((i) & 0x1f)
#define	break13(i)	(((i) >> 13) & 0x1fff)

#endif	/* _MACHINE_TRAP_H_ */
