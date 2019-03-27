/*	$NetBSD: asm.h,v 1.29 2000/12/14 21:29:51 jeffs Exp $	*/

/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)machAsmDefs.h	8.1 (Berkeley) 6/10/93
 *	JNPR: asm.h,v 1.10 2007/08/09 11:23:32 katta
 * $FreeBSD$
 */

/*
 * machAsmDefs.h --
 *
 *	Macros used when writing assembler programs.
 *
 *	Copyright (C) 1989 Digital Equipment Corporation.
 *	Permission to use, copy, modify, and distribute this software and
 *	its documentation for any purpose and without fee is hereby granted,
 *	provided that the above copyright notice appears in all copies.
 *	Digital Equipment Corporation makes no representations about the
 *	suitability of this software for any purpose.  It is provided "as is"
 *	without express or implied warranty.
 *
 * from: Header: /sprite/src/kernel/mach/ds3100.md/RCS/machAsmDefs.h,
 *	v 1.2 89/08/15 18:28:24 rab Exp  SPRITE (DECWRL)
 */

#ifndef _MACHINE_ASM_H_
#define	_MACHINE_ASM_H_

#include <machine/abi.h>
#include <machine/regdef.h>
#include <machine/endian.h>
#include <machine/cdefs.h>

#undef __FBSDID
#if !defined(lint) && !defined(STRIP_FBSDID)
#define	__FBSDID(s)	.ident s
#else
#define	__FBSDID(s)	/* nothing */
#endif

/*
 * Define -pg profile entry code.
 * Must always be noreorder, must never use a macro instruction
 * Final addiu to t9 must always equal the size of this _KERN_MCOUNT
 */
#define	_KERN_MCOUNT			\
	.set	push;			\
	.set	noreorder;		\
	.set	noat;			\
	subu	sp,sp,16;		\
	sw	t9,12(sp);		\
	move	AT,ra;			\
	lui	t9,%hi(_mcount);	\
	addiu	t9,t9,%lo(_mcount);	\
	jalr	t9;			\
	nop;				\
	lw	t9,4(sp);		\
	addiu	sp,sp,8;		\
	addiu	t9,t9,40;		\
	.set	pop;

#ifdef GPROF
#define	MCOUNT _KERN_MCOUNT
#else
#define	MCOUNT
#endif

#define	_C_LABEL(x)	x

#ifdef USE_AENT
#define	AENT(x)		\
	.aent	x, 0
#else
#define	AENT(x)
#endif

/*
 * WARN_REFERENCES: create a warning if the specified symbol is referenced
 */
#define	WARN_REFERENCES(_sym,_msg)				\
	.section .gnu.warning. ## _sym ; .ascii _msg ; .text

#ifdef __ELF__
# define _C_LABEL(x)    x
#else
#  define _C_LABEL(x)   _ ## x
#endif

/*
 * WEAK_ALIAS: create a weak alias.
 */
#define	WEAK_ALIAS(alias,sym)						\
	.weak alias;							\
	alias = sym

/*
 * STRONG_ALIAS: create a strong alias.
 */
#define STRONG_ALIAS(alias,sym)						\
	.globl alias;							\
	alias = sym

#define	GLOBAL(sym)						\
	.globl sym; sym:

#define	ENTRY(sym)						\
	.text; .globl sym; .ent sym; sym:

#define	ASM_ENTRY(sym)						\
	.text; .globl sym; .type sym,@function; sym:

/*
 * LEAF
 *	A leaf routine does
 *	- call no other function,
 *	- never use any register that callee-saved (S0-S8), and
 *	- not use any local stack storage.
 */
#define	LEAF(x)			\
	.globl	_C_LABEL(x);	\
	.ent	_C_LABEL(x), 0;	\
_C_LABEL(x): ;			\
	.frame sp, 0, ra;	\
	MCOUNT

/*
 * LEAF_NOPROFILE
 *	No profilable leaf routine.
 */
#define	LEAF_NOPROFILE(x)	\
	.globl	_C_LABEL(x);	\
	.ent	_C_LABEL(x), 0;	\
_C_LABEL(x): ;			\
	.frame	sp, 0, ra

/*
 * XLEAF
 *	declare alternate entry to leaf routine
 */
#define	XLEAF(x)		\
	.globl	_C_LABEL(x);	\
	AENT (_C_LABEL(x));	\
_C_LABEL(x):

/*
 * NESTED
 *	A function calls other functions and needs
 *	therefore stack space to save/restore registers.
 */
#define	NESTED(x, fsize, retpc)		\
	.globl	_C_LABEL(x);		\
	.ent	_C_LABEL(x), 0;		\
_C_LABEL(x): ;				\
	.frame	sp, fsize, retpc;	\
	MCOUNT

/*
 * NESTED_NOPROFILE(x)
 *	No profilable nested routine.
 */
#define	NESTED_NOPROFILE(x, fsize, retpc)	\
	.globl	_C_LABEL(x);			\
	.ent	_C_LABEL(x), 0;			\
_C_LABEL(x): ;					\
	.frame	sp, fsize, retpc

/*
 * XNESTED
 *	declare alternate entry point to nested routine.
 */
#define	XNESTED(x)		\
	.globl	_C_LABEL(x);	\
	AENT (_C_LABEL(x));	\
_C_LABEL(x):

/*
 * END
 *	Mark end of a procedure.
 */
#define	END(x)			\
	.end _C_LABEL(x)

/*
 * IMPORT -- import external symbol
 */
#define	IMPORT(sym, size)	\
	.extern _C_LABEL(sym),size

/*
 * EXPORT -- export definition of symbol
 */
#define	EXPORT(x)		\
	.globl	_C_LABEL(x);	\
_C_LABEL(x):

/*
 * VECTOR
 *	exception vector entrypoint
 *	XXX: regmask should be used to generate .mask
 */
#define	VECTOR(x, regmask)	\
	.ent	_C_LABEL(x),0;	\
	EXPORT(x);		\

#define	VECTOR_END(x)		\
	EXPORT(x ## End);	\
	END(x)

/*
 * Macros to panic and printf from assembly language.
 */
#define	PANIC(msg)			\
	PTR_LA	a0, 9f;			\
	jal	_C_LABEL(panic);	\
	nop;				\
	MSG(msg)

#define	PANIC_KSEG0(msg, reg)	PANIC(msg)

#define	PRINTF(msg)			\
	PTR_LA	a0, 9f;			\
	jal	_C_LABEL(printf);	\
	nop;				\
	MSG(msg)

#define	MSG(msg)			\
	.rdata;				\
9:	.asciiz	msg;			\
	.text

#define	ASMSTR(str)			\
	.asciiz str;			\
	.align	3

#if defined(__mips_o32) || defined(__mips_o64)
#define	ALSK	7		/* stack alignment */
#define	ALMASK	-7		/* stack alignment */
#define	SZFPREG	4
#define	FP_L	lwc1
#define	FP_S	swc1
#else
#define	ALSK	15		/* stack alignment */
#define	ALMASK	-15		/* stack alignment */
#define	SZFPREG	8
#define	FP_L	ldc1
#define	FP_S	sdc1
#endif

/*
 *   Endian-independent assembly-code aliases for unaligned memory accesses.
 */
#if _BYTE_ORDER == _LITTLE_ENDIAN
# define LWHI lwr
# define LWLO lwl
# define SWHI swr
# define SWLO swl
# if SZREG == 4
#  define REG_LHI   lwr
#  define REG_LLO   lwl
#  define REG_SHI   swr
#  define REG_SLO   swl
# else
#  define REG_LHI   ldr
#  define REG_LLO   ldl
#  define REG_SHI   sdr
#  define REG_SLO   sdl
# endif
#endif

#if _BYTE_ORDER == _BIG_ENDIAN
# define LWHI lwl
# define LWLO lwr
# define SWHI swl
# define SWLO swr
# if SZREG == 4
#  define REG_LHI   lwl
#  define REG_LLO   lwr
#  define REG_SHI   swl
#  define REG_SLO   swr
# else
#  define REG_LHI   ldl
#  define REG_LLO   ldr
#  define REG_SHI   sdl
#  define REG_SLO   sdr
# endif
#endif

/*
 * While it would be nice to be compatible with the SGI
 * REG_L and REG_S macros, because they do not take parameters, it
 * is impossible to use them with the _MIPS_SIM_ABIX32 model.
 *
 * These macros hide the use of mips3 instructions from the
 * assembler to prevent the assembler from generating 64-bit style
 * ABI calls.
 */
#if _MIPS_SZPTR == 32
#define	PTR_ADD		add
#define	PTR_ADDI	addi
#define	PTR_ADDU	addu
#define	PTR_ADDIU	addiu
#define	PTR_SUB		add
#define	PTR_SUBI	subi
#define	PTR_SUBU	subu
#define	PTR_SUBIU	subu
#define	PTR_L		lw
#define	PTR_LA		la
#define	PTR_LI		li
#define	PTR_S		sw
#define	PTR_SLL		sll
#define	PTR_SLLV	sllv
#define	PTR_SRL		srl
#define	PTR_SRLV	srlv
#define	PTR_SRA		sra
#define	PTR_SRAV	srav
#define	PTR_LL		ll
#define	PTR_SC		sc
#define	PTR_WORD	.word
#define	PTR_SCALESHIFT	2
#else /* _MIPS_SZPTR == 64 */
#define	PTR_ADD		dadd
#define	PTR_ADDI	daddi
#define	PTR_ADDU	daddu
#define	PTR_ADDIU	daddiu
#define	PTR_SUB		dadd
#define	PTR_SUBI	dsubi
#define	PTR_SUBU	dsubu
#define	PTR_SUBIU	dsubu
#define	PTR_L		ld
#define	PTR_LA		dla
#define	PTR_LI		dli
#define	PTR_S		sd
#define	PTR_SLL		dsll
#define	PTR_SLLV	dsllv
#define	PTR_SRL		dsrl
#define	PTR_SRLV	dsrlv
#define	PTR_SRA		dsra
#define	PTR_SRAV	dsrav
#define	PTR_LL		lld
#define	PTR_SC		scd
#define	PTR_WORD	.dword
#define	PTR_SCALESHIFT	3
#endif /* _MIPS_SZPTR == 64 */

#if _MIPS_SZINT == 32
#define	INT_ADD		add
#define	INT_ADDI	addi
#define	INT_ADDU	addu
#define	INT_ADDIU	addiu
#define	INT_SUB		add
#define	INT_SUBI	subi
#define	INT_SUBU	subu
#define	INT_SUBIU	subu
#define	INT_L		lw
#define	INT_LA		la
#define	INT_S		sw
#define	INT_SLL		sll
#define	INT_SLLV	sllv
#define	INT_SRL		srl
#define	INT_SRLV	srlv
#define	INT_SRA		sra
#define	INT_SRAV	srav
#define	INT_LL		ll
#define	INT_SC		sc
#define	INT_WORD	.word
#define	INT_SCALESHIFT	2
#else
#define	INT_ADD		dadd
#define	INT_ADDI	daddi
#define	INT_ADDU	daddu
#define	INT_ADDIU	daddiu
#define	INT_SUB		dadd
#define	INT_SUBI	dsubi
#define	INT_SUBU	dsubu
#define	INT_SUBIU	dsubu
#define	INT_L		ld
#define	INT_LA		dla
#define	INT_S		sd
#define	INT_SLL		dsll
#define	INT_SLLV	dsllv
#define	INT_SRL		dsrl
#define	INT_SRLV	dsrlv
#define	INT_SRA		dsra
#define	INT_SRAV	dsrav
#define	INT_LL		lld
#define	INT_SC		scd
#define	INT_WORD	.dword
#define	INT_SCALESHIFT	3
#endif

#if _MIPS_SZLONG == 32
#define	LONG_ADD	add
#define	LONG_ADDI	addi
#define	LONG_ADDU	addu
#define	LONG_ADDIU	addiu
#define	LONG_SUB	add
#define	LONG_SUBI	subi
#define	LONG_SUBU	subu
#define	LONG_SUBIU	subu
#define	LONG_L		lw
#define	LONG_LA		la
#define	LONG_S		sw
#define	LONG_SLL	sll
#define	LONG_SLLV	sllv
#define	LONG_SRL	srl
#define	LONG_SRLV	srlv
#define	LONG_SRA	sra
#define	LONG_SRAV	srav
#define	LONG_LL		ll
#define	LONG_SC		sc
#define	LONG_WORD	.word
#define	LONG_SCALESHIFT	2
#else
#define	LONG_ADD	dadd
#define	LONG_ADDI	daddi
#define	LONG_ADDU	daddu
#define	LONG_ADDIU	daddiu
#define	LONG_SUB	dadd
#define	LONG_SUBI	dsubi
#define	LONG_SUBU	dsubu
#define	LONG_SUBIU	dsubu
#define	LONG_L		ld
#define	LONG_LA		dla
#define	LONG_S		sd
#define	LONG_SLL	dsll
#define	LONG_SLLV	dsllv
#define	LONG_SRL	dsrl
#define	LONG_SRLV	dsrlv
#define	LONG_SRA	dsra
#define	LONG_SRAV	dsrav
#define	LONG_LL		lld
#define	LONG_SC		scd
#define	LONG_WORD	.dword
#define	LONG_SCALESHIFT	3
#endif

#if SZREG == 4
#define	REG_L		lw
#define	REG_S		sw
#define	REG_LI		li
#define	REG_ADDU	addu
#define	REG_SLL		sll
#define	REG_SLLV	sllv
#define	REG_SRL		srl
#define	REG_SRLV	srlv
#define	REG_SRA		sra
#define	REG_SRAV	srav
#define	REG_LL		ll
#define	REG_SC		sc
#define	REG_SCALESHIFT	2
#else
#define	REG_L		ld
#define	REG_S		sd
#define	REG_LI		dli
#define	REG_ADDU	daddu
#define	REG_SLL		dsll
#define	REG_SLLV	dsllv
#define	REG_SRL		dsrl
#define	REG_SRLV	dsrlv
#define	REG_SRA		dsra
#define	REG_SRAV	dsrav
#define	REG_LL		lld
#define	REG_SC		scd
#define	REG_SCALESHIFT	3
#endif

#if _MIPS_ISA == _MIPS_ISA_MIPS1 || _MIPS_ISA == _MIPS_ISA_MIPS2 || \
    _MIPS_ISA == _MIPS_ISA_MIPS32
#define	MFC0		mfc0
#define	MTC0		mtc0
#endif
#if _MIPS_ISA == _MIPS_ISA_MIPS3 || _MIPS_ISA == _MIPS_ISA_MIPS4 || \
    _MIPS_ISA == _MIPS_ISA_MIPS64
#define	MFC0		dmfc0
#define	MTC0		dmtc0
#endif

#if defined(__mips_o32) || defined(__mips_o64)

#ifdef __ABICALLS__
#define	CPRESTORE(r)	.cprestore r
#define	CPLOAD(r)	.cpload r
#else
#define	CPRESTORE(r)	/* not needed */
#define	CPLOAD(r)	/* not needed */
#endif

#define	SETUP_GP	\
			.set push;				\
			.set noreorder;				\
			.cpload	t9;				\
			.set pop
#define	SETUP_GPX(r)	\
			.set push;				\
			.set noreorder;				\
			move	r,ra;	/* save old ra */	\
			bal	7f;				\
			nop;					\
		7:	.cpload	ra;				\
			move	ra,r;				\
			.set pop
#define	SETUP_GPX_L(r,lbl)	\
			.set push;				\
			.set noreorder;				\
			move	r,ra;	/* save old ra */	\
			bal	lbl;				\
			nop;					\
		lbl:	.cpload	ra;				\
			move	ra,r;				\
			.set pop
#define	SAVE_GP(x)	.cprestore x

#define	SETUP_GP64(a,b)		/* n32/n64 specific */
#define	SETUP_GP64_R(a,b)	/* n32/n64 specific */
#define	SETUP_GPX64(a,b)	/* n32/n64 specific */
#define	SETUP_GPX64_L(a,b,c)	/* n32/n64 specific */
#define	RESTORE_GP64		/* n32/n64 specific */
#define	USE_ALT_CP(a)		/* n32/n64 specific */
#endif /* __mips_o32 || __mips_o64 */

#if defined(__mips_o32) || defined(__mips_o64)
#define	REG_PROLOGUE	.set push
#define	REG_EPILOGUE	.set pop
#endif
#if defined(__mips_n32) || defined(__mips_n64)
#define	REG_PROLOGUE	.set push ; .set mips3
#define	REG_EPILOGUE	.set pop
#endif

#if defined(__mips_n32) || defined(__mips_n64)
#define	SETUP_GP		/* o32 specific */
#define	SETUP_GPX(r)		/* o32 specific */
#define	SETUP_GPX_L(r,lbl)	/* o32 specific */
#define	SAVE_GP(x)		/* o32 specific */
#define	SETUP_GP64(a,b)		.cpsetup $25, a, b
#define	SETUP_GPX64(a,b)	\
				.set push;			\
				move	b,ra;			\
				.set noreorder;			\
				bal	7f;			\
				nop;				\
			7:	.set pop;			\
				.cpsetup ra, a, 7b;		\
				move	ra,b
#define	SETUP_GPX64_L(a,b,c)	\
				.set push;			\
				move	b,ra;			\
				.set noreorder;			\
				bal	c;			\
				nop;				\
			c:	.set pop;			\
				.cpsetup ra, a, c;		\
				move	ra,b
#define	RESTORE_GP64		.cpreturn
#define	USE_ALT_CP(a)		.cplocal a
#endif	/* __mips_n32 || __mips_n64 */

#define	GET_CPU_PCPU(reg)		\
	PTR_L	reg, _C_LABEL(pcpup);

/*
 * Description of the setjmp buffer
 *
 * word  0	magic number	(dependant on creator)
 *       1	RA
 *       2	S0
 *       3	S1
 *       4	S2
 *       5	S3
 *       6	S4
 *       7	S5
 *       8	S6
 *       9	S7
 *       10	SP
 *       11	S8
 *       12	GP		(dependent on ABI)
 *       13	signal mask	(dependant on magic)
 *       14	(con't)
 *       15	(con't)
 *       16	(con't)
 *
 * The magic number number identifies the jmp_buf and
 * how the buffer was created as well as providing
 * a sanity check
 *
 */

#define _JB_MAGIC__SETJMP	0xBADFACED
#define _JB_MAGIC_SETJMP	0xFACEDBAD

/* Valid for all jmp_buf's */

#define _JB_MAGIC		0
#define _JB_REG_RA		1
#define _JB_REG_S0		2
#define _JB_REG_S1		3
#define _JB_REG_S2		4
#define _JB_REG_S3		5
#define _JB_REG_S4		6
#define _JB_REG_S5		7
#define _JB_REG_S6		8
#define _JB_REG_S7		9
#define _JB_REG_SP		10
#define _JB_REG_S8		11
#if defined(__mips_n32) || defined(__mips_n64)
#define	_JB_REG_GP		12
#endif

/* Only valid with the _JB_MAGIC_SETJMP magic */

#define _JB_SIGMASK		13
#define	__JB_SIGMASK_REMAINDER	14	/* sigmask_t is 128-bits */

#define _JB_FPREG_F20		15
#define _JB_FPREG_F21		16
#define _JB_FPREG_F22		17
#define _JB_FPREG_F23		18
#define _JB_FPREG_F24		19
#define _JB_FPREG_F25		20
#define _JB_FPREG_F26		21
#define _JB_FPREG_F27		22
#define _JB_FPREG_F28		23
#define _JB_FPREG_F29		24
#define _JB_FPREG_F30		25
#define _JB_FPREG_F31		26
#define _JB_FPREG_FCSR		27

/*
 * Various macros for dealing with TLB hazards
 * (a) why so many?
 * (b) when to use?
 * (c) why not used everywhere?
 */
/*
 * Assume that w alaways need nops to escape CP0 hazard
 * TODO: Make hazard delays configurable. Stuck with 5 cycles on the moment
 * For more info on CP0 hazards see Chapter 7 (p.99) of "MIPS32 Architecture 
 *    For Programmers Volume III: The MIPS32 Privileged Resource Architecture"
 */
#if defined(CPU_NLM)
#define	HAZARD_DELAY	sll $0,3
#define	ITLBNOPFIX	sll $0,3
#elif defined(CPU_RMI)
#define	HAZARD_DELAY
#define	ITLBNOPFIX
#elif defined(CPU_MIPS74K)
#define	HAZARD_DELAY	sll $0,$0,3
#define	ITLBNOPFIX	sll $0,$0,3
#else
#define	ITLBNOPFIX	nop;nop;nop;nop;nop;nop;nop;nop;nop;sll $0,$0,3;
#define	HAZARD_DELAY	nop;nop;nop;nop;sll $0,$0,3;
#endif

#endif /* !_MACHINE_ASM_H_ */
