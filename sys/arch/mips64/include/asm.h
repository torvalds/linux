/*	$OpenBSD: asm.h,v 1.27 2021/05/01 16:11:10 visa Exp $ */

/*
 * Copyright (c) 2001-2002 Opsycon AB  (www.opsycon.se / www.opsycon.com)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#ifndef _MIPS64_ASM_H_
#define _MIPS64_ASM_H_

#include <machine/regdef.h>

#define	_MIPS_ISA_MIPS1		1	/* R2000/R3000 */
#define	_MIPS_ISA_MIPS2		2	/* R4000/R6000 */
#define	_MIPS_ISA_MIPS3		3	/* R4000 */
#define	_MIPS_ISA_MIPS4		4	/* TFP (R1x000) */
#define	_MIPS_ISA_MIPS32	32	/* MIPS32 */
#define	_MIPS_ISA_MIPS64	64	/* MIPS64 */

#if !defined(ABICALLS) && !defined(_NO_ABICALLS)
#define	ABICALLS	.abicalls
#endif

#if defined(ABICALLS) && !defined(_KERNEL)
	ABICALLS
#endif

#define _C_LABEL(x) x		/* XXX Obsolete but keep for a while */

#if !defined(__MIPSEL__) && !defined(__MIPSEB__)
#error "__MIPSEL__ or __MIPSEB__ must be defined"
#endif
/*
 * Define how to access unaligned data word
 */
#if defined(__MIPSEL__)
#define LWLO    lwl
#define LWHI    lwr
#define	SWLO	swl
#define	SWHI	swr
#define LDLO    ldl
#define LDHI    ldr
#define	SDLO	sdl
#define	SDHI	sdr
#endif
#if defined(__MIPSEB__)
#define LWLO    lwr
#define LWHI    lwl
#define	SWLO	swr
#define	SWHI	swl
#define LDLO    ldr
#define LDHI    ldl
#define	SDLO	sdr
#define	SDHI	sdl
#endif

/*
 *  Define programming environment for ABI.
 */
#if defined(ABICALLS) && !defined(_KERNEL) && !defined(_STANDALONE)

#ifndef _MIPS_SIM
#define _MIPS_SIM 1
#define _ABIO32	1
#endif
#ifndef _MIPS_ISA
#define _MIPS_ISA 2
#define _MIPS_ISA_MIPS2 2
#endif

#if (_MIPS_SIM == _ABIO32) || (_MIPS_SIM == _ABI32)
#define NARGSAVE	4

#define	SETUP_GP		\
	.set	noreorder;	\
	.cpload	t9;		\
	.set	reorder;

#define	SAVE_GP(x)		\
	.cprestore x

#define	SETUP_GP64(gpoff, name)
#define	RESTORE_GP64
#endif

#if (_MIPS_SIM == _ABI64) || (_MIPS_SIM == _ABIN32)
#define NARGSAVE	0

#define	SETUP_GP
#define	SAVE_GP(x)
#define	SETUP_GP64(gpoff, name)	\
	.cpsetup t9, gpoff, name
#define	RESTORE_GP64		\
	.cpreturn
#endif

#define	MKFSIZ(narg,locals) (((narg+locals)*REGSZ+31)&(~31))

#else /* defined(ABICALLS) && !defined(_KERNEL) */

#define	NARGSAVE	4
#define	SETUP_GP
#define	SAVE_GP(x)

#define	ALIGNSZ		16	/* Stack layout alignment */
#define	FRAMESZ(sz)	(((sz) + (ALIGNSZ-1)) & ~(ALIGNSZ-1))

#endif

/*
 *  Basic register operations based on selected ISA
 */
#if (_MIPS_ISA == _MIPS_ISA_MIPS1 || _MIPS_ISA == _MIPS_ISA_MIPS2 || \
    _MIPS_ISA == _MIPS_ISA_MIPS32)
#define REGSZ		4	/* 32 bit mode register size */
#define LOGREGSZ	2	/* log rsize */
#define	REG_S	sw
#define	REG_L	lw
#define	CF_SZ		24	/* Call frame size */
#define	CF_ARGSZ	16	/* Call frame arg size */
#define	CF_RA_OFFS	20	/* Call ra save offset */
#endif

#if (_MIPS_ISA == _MIPS_ISA_MIPS3 || _MIPS_ISA == _MIPS_ISA_MIPS4 || \
    _MIPS_ISA == _MIPS_ISA_MIPS64)
#define REGSZ		8	/* 64 bit mode register size */
#define LOGREGSZ	3	/* log rsize */
#define	REG_S	sd
#define	REG_L	ld
#define	CF_SZ		48	/* Call frame size (multiple of ALIGNSZ) */
#define	CF_ARGSZ	32	/* Call frame arg size */
#define	CF_RA_OFFS	40	/* Call ra save offset */
#endif

#ifndef __LP64__
#define	PTR_L		lw
#define	PTR_S		sw
#define	PTR_SUB		sub
#define	PTR_ADD		add
#define	PTR_SUBU	subu
#define	PTR_ADDU	addu
#define LI		li
#define	LA		la
#define	PTR_SLL		sll
#define	PTR_SRL		srl
#define	PTR_VAL		.word
#else
#define	PTR_L		ld
#define	PTR_S		sd
#define	PTR_ADD		dadd
#define	PTR_SUB		dsub
#define	PTR_SUBU	dsubu
#define	PTR_ADDU	daddu
#define LI		dli
#define LA		dla
#define	PTR_SLL		dsll
#define	PTR_SRL		dsrl
#define	PTR_VAL		.dword
#endif

#define	NOP	nop
#define	DMFC0	dmfc0
#define	DMTC0	dmtc0
#define	MFC0	mfc0
#define	MTC0	mtc0
#define	ERET	sync; eret

/*
 * Define -pg profile entry code.
 */
#if defined(XGPROF) || defined(XPROF)
#define	MCOUNT			\
	PTR_SUBU sp, sp, 64;	\
	SAVE_GP(16);		\
	sd	ra, 56(sp);	\
	sd	gp, 48(sp);	\
	.set	noat;		\
	.set	noreorder;	\
	move	AT, ra;		\
	jal	_mcount;	\
	PTR_SUBU sp, sp, 16;	\
	ld	ra, 56(sp);	\
	PTR_ADDU sp, sp, 64;	\
	.set reorder;		\
	.set	at;
#else
#define	MCOUNT
#endif

/*
 * LEAF(x, fsize)
 *
 *	Declare a leaf routine.
 */
#define LEAF(x, fsize)		\
	.align	3;		\
	.globl x;		\
	.ent x, 0;		\
x: ;				\
	.frame sp, fsize, ra;	\
	SETUP_GP		\
	MCOUNT

#define	ALEAF(x)		\
	.globl	x;		\
x:

/*
 * NLEAF(x)
 *
 *	Declare a non-profiled leaf routine.
 */
#define NLEAF(x, fsize)		\
	.align	3;		\
	.globl x;		\
	.ent x, 0;		\
x: ;				\
	.frame sp, fsize, ra;	\
	SETUP_GP

/*
 * NON_LEAF(x)
 *
 *	Declare a non-leaf routine (a routine that makes other C calls).
 */
#define NON_LEAF(x, fsize, retpc) \
	.align	3;		\
	.globl x;		\
	.ent x, 0;		\
x: ;				\
	.frame sp, fsize, retpc; \
	SETUP_GP		\
	MCOUNT

/*
 * NNON_LEAF(x)
 *
 *	Declare a non-profiled non-leaf routine
 *	(a routine that makes other C calls).
 */
#define NNON_LEAF(x, fsize, retpc) \
	.align	3;		\
	.globl x;		\
	.ent x, 0;		\
x: ;				\
	.frame sp, fsize, retpc	\
	SETUP_GP

/*
 * END(x)
 *
 *	Mark end of a procedure.
 */
#define END(x) \
	.end x

/*
 * STRONG_ALIAS, WEAK_ALIAS
 *	Create a strong or weak alias.
 */
#define STRONG_ALIAS(alias,sym) \
	.global alias; alias = sym
#define WEAK_ALIAS(alias,sym) \
	.weak alias; alias = sym


/*
 * Macros to panic and printf from assembly language.
 */
#define PANIC(msg) \
	LA	a0, 9f; \
	jal	panic;	\
	nop	;	\
	MSG(msg)

#define	PRINTF(msg) \
	LA	a0, 9f; \
	jal	printf; \
	nop	;	\
	MSG(msg)

#define	MSG(msg) \
	.rdata; \
9:	.asciiz	msg; \
	.text

#define	LOAD_XKPHYS(reg, cca) \
	li	reg, cca | 0x10; \
	dsll	reg, reg, 59

#ifdef MULTIPROCESSOR
#define GET_CPU_INFO(ci, tmp)	HW_GET_CPU_INFO(ci, tmp)
#else  /* MULTIPROCESSOR */
#define GET_CPU_INFO(ci, tmp)		\
	LA	ci, cpu_info_primary
#endif /* MULTIPROCESSOR */

/*
 * Hazards
 */

#ifdef CPU_OCTEON
/*
 * OCTEON clears hazards in hardware.
 */
#define	MFC0_HAZARD		/* nothing */
#define	MTC0_HAZARD		/* nothing */
#define	MTC0_SR_IE_HAZARD	/* nothing */
#define	MTC0_SR_CU_HAZARD	/* nothing */
#define	TLB_HAZARD		/* nothing */
#endif

/* Hazard between {d,}mfc0 of COP_0_VADDR */
#ifndef	PRE_MFC0_ADDR_HAZARD
#define	PRE_MFC0_ADDR_HAZARD	/* nothing */
#endif

/* Hazard after {d,}mfc0 from any register */
#ifndef	MFC0_HAZARD
#define	MFC0_HAZARD     	/* nothing */
#endif
/* Hazard after {d,}mtc0 to any register */
#ifndef	MTC0_HAZARD
#define	MTC0_HAZARD     	NOP; NOP; NOP; NOP
#endif
/* Hazard after {d,}mtc0 to COP_0_SR affecting the state of interrupts */
#ifndef	MTC0_SR_IE_HAZARD
#define	MTC0_SR_IE_HAZARD	MTC0_HAZARD
#endif
/* Hazard after {d,}mtc0 to COP_0_SR affecting the state of coprocessors */
#ifndef	MTC0_SR_CU_HAZARD
#define	MTC0_SR_CU_HAZARD	NOP; NOP
#endif

/* Hazard before and after a tlbp, tlbr, tlbwi or tlbwr instruction */
#ifndef	TLB_HAZARD
#define	TLB_HAZARD		NOP; NOP
#endif

#endif /* !_MIPS64_ASM_H_ */
