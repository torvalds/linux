/*	$OpenBSD: asm.h,v 1.16 2022/12/06 18:50:59 guenther Exp $	*/

/*
 * Mach Operating System
 * Copyright (c) 1993-1992 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON AND OMRON ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND OMRON DISCLAIM ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#ifndef _M88K_ASM_H_
#define _M88K_ASM_H_

#define	_C_LABEL(name)		name
#define	_ASM_LABEL(name)	name

#define	_ENTRY(name) \
	.text; .align 3; .globl name; .type name,@function; name:

#define	ENTRY(name)		_ENTRY(name)
#define	ASENTRY(name)		_ENTRY(name)

#define	END(name) \
	.size name,.-name

#define	GLOBAL(name) \
	.globl name; name:

#define ASGLOBAL(name) \
	.globl name; name:

#define	LOCAL(name) \
	name:

#define	ASLOCAL(name) \
	name:

#define	BSS(name, size) \
	.comm	name, size

#define	ASBSS(name, size) \
	.comm	name, size

#define	STRONG_ALIAS(alias,sym)						\
	.global alias;							\
	alias = sym
#define	WEAK_ALIAS(alias,sym)						\
	.weak alias;							\
	alias = sym

#ifdef _KERNEL

#ifdef _LOCORE

/*
 * Control register symbolic names
 */

#define	PID	%cr0
#define	PSR	%cr1
#define	EPSR	%cr2
#define	SSBR	%cr3
#define	SXIP	%cr4		/* 88100 */
#define	EXIP	%cr4		/* 88110 */
#define	SNIP	%cr5		/* 88100 */
#define	ENIP	%cr5		/* 88110 */
#define	SFIP	%cr6		/* 88100 */
#define	VBR	%cr7
#define	DMT0	%cr8		/* 88100 */
#define	DMD0	%cr9		/* 88100 */
#define	DMA0	%cr10		/* 88100 */
#define	DMT1	%cr11		/* 88100 */
#define	DMD1	%cr12		/* 88100 */
#define	DMA1	%cr13		/* 88100 */
#define	DMT2	%cr14		/* 88100 */
#define	DMD2	%cr15		/* 88100 */
#define	DMA2	%cr16		/* 88100 */
#define	SRX	%cr16		/* 88110 */
#define	SR0	%cr17
#define	SR1	%cr18
#define	SR2	%cr19
#define	SR3	%cr20
#define	ICMD	%cr25		/* 88110 */
#define	ICTL	%cr26		/* 88110 */
#define	ISAR	%cr27		/* 88110 */
#define	ISAP	%cr28		/* 88110 */
#define	IUAP	%cr29		/* 88110 */
#define	IIR	%cr30		/* 88110 */
#define	IBP	%cr31		/* 88110 */
#define	IPPU	%cr32		/* 88110 */
#define	IPPL	%cr33		/* 88110 */
#define	ISR	%cr34		/* 88110 */
#define	ILAR	%cr35		/* 88110 */
#define	IPAR	%cr36		/* 88110 */
#define	DCMD	%cr40		/* 88110 */
#define	DCTL	%cr41		/* 88110 */
#define	DSAR	%cr42		/* 88110 */
#define	DSAP	%cr43		/* 88110 */
#define	DUAP	%cr44		/* 88110 */
#define	DIR	%cr45		/* 88110 */
#define	DBP	%cr46		/* 88110 */
#define	DPPU	%cr47		/* 88110 */
#define	DPPL	%cr48		/* 88110 */
#define	DSR	%cr49		/* 88110 */
#define	DLAR	%cr50		/* 88110 */
#define	DPAR	%cr51		/* 88110 */

#define	FPECR	%fcr0
#define	FPHS1	%fcr1		/* 88100 */
#define	FPLS1	%fcr2		/* 88100 */
#define	FPHS2	%fcr3		/* 88100 */
#define	FPLS2	%fcr4		/* 88100 */
#define	FPPT	%fcr5		/* 88100 */
#define	FPRH	%fcr6		/* 88100 */
#define	FPRL	%fcr7		/* 88100 */
#define	FPIT	%fcr8		/* 88100 */
#define	FPSR	%fcr62
#define	FPCR	%fcr63

#define	CPU	SR0

/*
 * At various times, there is the need to clear the pipeline (i.e.
 * synchronize).  A "tb1 0, r0, foo" will do that (because a trap
 * instruction always synchronizes, and this particular instruction
 * will never actually take the trap).
 */
#define	FLUSH_PIPELINE		tb1	0, %r0, 0

#define	NOP			or	%r0, %r0, %r0
#define RTE			NOP; rte

/*
 * PSR bits
 */
#define	PSR_SHADOW_FREEZE_BIT		0
#define	PSR_INTERRUPT_DISABLE_BIT	1
#define	PSR_FPU_DISABLE_BIT		3
#define	PSR_GRAPHICS_DISABLE_BIT	4	/* SFU2 - MC88110 */
#define	PSR_SERIALIZE_BIT		25	/* MC88110 */
#define	PSR_CARRY_BIT			28
#define	PSR_SERIAL_MODE_BIT		29
#define	PSR_BIG_ENDIAN_MODE		30
#define	PSR_SUPERVISOR_MODE_BIT		31

/*
 * DMT0/DMT1/DMT2 bits
 */
#define	DMT_VALID_BIT		0
#define	DMT_WRITE_BIT		1
#define	DMT_LOCK_BIT		12
#define	DMT_DOUBLE_BIT		13
#define	DMT_DAS_BIT		14
#define	DMT_DREG_OFFSET		7
#define	DMT_DREG_WIDTH		5

/*
 * Status bits for an SXIP/SNIP/SFIP address.
 */
#define	RTE_VALID_BIT		1
#define	RTE_ERROR_BIT		0

#define	VECTOR(x) \
	.word	x

#endif	/* _LOCORE */

#endif	/* _KERNEL */

#endif	/* _M88K_ASM_H_ */
