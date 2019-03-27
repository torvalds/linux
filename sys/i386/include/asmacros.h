/* -*- mode: asm -*- */
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993 The Regents of the University of California.
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
 * $FreeBSD$
 */

#ifndef _MACHINE_ASMACROS_H_
#define _MACHINE_ASMACROS_H_

#include <sys/cdefs.h>

/* XXX too much duplication in various asm*.h's. */

/*
 * CNAME is used to manage the relationship between symbol names in C
 * and the equivalent assembly language names.  CNAME is given a name as
 * it would be used in a C program.  It expands to the equivalent assembly
 * language name.
 */
#define CNAME(csym)		csym

#define ALIGN_DATA	.p2align 2	/* 4 byte alignment, zero filled */
#ifdef GPROF
#define ALIGN_TEXT	.p2align 4,0x90	/* 16-byte alignment, nop filled */
#else
#define ALIGN_TEXT	.p2align 2,0x90	/* 4-byte alignment, nop filled */
#endif
#define SUPERALIGN_TEXT	.p2align 4,0x90	/* 16-byte alignment, nop filled */

#define GEN_ENTRY(name)		ALIGN_TEXT; .globl CNAME(name); \
				.type CNAME(name),@function; CNAME(name):
#define NON_GPROF_ENTRY(name)	GEN_ENTRY(name)
#define NON_GPROF_RET		.byte 0xc3	/* opcode for `ret' */

#define	END(name)		.size name, . - name

#ifdef GPROF
/*
 * __mcount is like [.]mcount except that doesn't require its caller to set
 * up a frame pointer.  It must be called before pushing anything onto the
 * stack.  gcc should eventually generate code to call __mcount in most
 * cases.  This would make -pg in combination with -fomit-frame-pointer
 * useful.  gcc has a configuration variable PROFILE_BEFORE_PROLOGUE to
 * allow profiling before setting up the frame pointer, but this is
 * inadequate for good handling of special cases, e.g., -fpic works best
 * with profiling after the prologue.
 *
 * [.]mexitcount is a new function to support non-statistical profiling if an
 * accurate clock is available.  For C sources, calls to it are generated
 * by the FreeBSD extension `-mprofiler-epilogue' to gcc.  It is best to
 * call [.]mexitcount at the end of a function like the MEXITCOUNT macro does,
 * but gcc currently generates calls to it at the start of the epilogue to
 * avoid problems with -fpic.
 *
 * [.]mcount and __mcount may clobber the call-used registers and %ef.
 * [.]mexitcount may clobber %ecx and %ef.
 *
 * Cross-jumping makes non-statistical profiling timing more complicated.
 * It is handled in many cases by calling [.]mexitcount before jumping.  It
 * is handled for conditional jumps using CROSSJUMP() and CROSSJUMP_LABEL().
 * It is handled for some fault-handling jumps by not sharing the exit
 * routine.
 *
 * ALTENTRY() must be before a corresponding ENTRY() so that it can jump to
 * the main entry point.  Note that alt entries are counted twice.  They
 * have to be counted as ordinary entries for gprof to get the call times
 * right for the ordinary entries.
 *
 * High local labels are used in macros to avoid clashes with local labels
 * in functions.
 *
 * Ordinary `ret' is used instead of a macro `RET' because there are a lot
 * of `ret's.  0xc3 is the opcode for `ret' (`#define ret ... ret' can't
 * be used because this file is sometimes preprocessed in traditional mode).
 * `ret' clobbers eflags but this doesn't matter.
 */
#define ALTENTRY(name)		GEN_ENTRY(name) ; MCOUNT ; MEXITCOUNT ; jmp 9f
#define	CROSSJUMP(jtrue, label, jfalse) \
	jfalse 8f; MEXITCOUNT; jmp __CONCAT(to,label); 8:
#define CROSSJUMPTARGET(label) \
	ALIGN_TEXT; __CONCAT(to,label): ; MCOUNT; jmp label
#define ENTRY(name)		GEN_ENTRY(name) ; 9: ; MCOUNT
#define FAKE_MCOUNT(caller)	pushl caller ; call *__mcountp ; popl %ecx
#define MCOUNT			call *__mcountp
#define MCOUNT_LABEL(name)	GEN_ENTRY(name) ; nop ; ALIGN_TEXT
#ifdef GUPROF
#define MEXITCOUNT		call *__mexitcountp
#define ret			MEXITCOUNT ; NON_GPROF_RET
#else
#define MEXITCOUNT
#endif

#else /* !GPROF */
/*
 * ALTENTRY() has to align because it is before a corresponding ENTRY().
 * ENTRY() has to align to because there may be no ALTENTRY() before it.
 * If there is a previous ALTENTRY() then the alignment code for ENTRY()
 * is empty.
 */
#define ALTENTRY(name)		GEN_ENTRY(name)
#define	CROSSJUMP(jtrue, label, jfalse)	jtrue label
#define	CROSSJUMPTARGET(label)
#define ENTRY(name)		GEN_ENTRY(name)
#define FAKE_MCOUNT(caller)
#define MCOUNT
#define MCOUNT_LABEL(name)
#define MEXITCOUNT
#endif /* GPROF */

#ifdef LOCORE

#define	GSEL_KPL	0x0020	/* GSEL(GCODE_SEL, SEL_KPL) */
#define	SEL_RPL_MASK	0x0003

/*
 * Convenience macro for declaring interrupt entry points.
 */
#define	IDTVEC(name)	ALIGN_TEXT; .globl __CONCAT(X,name); \
			.type __CONCAT(X,name),@function; __CONCAT(X,name):

/*
 * Macros to create and destroy a trap frame.
 */
	.macro	PUSH_FRAME2
	pushal
	pushl	$0
	movw	%ds,(%esp)
	pushl	$0
	movw	%es,(%esp)
	pushl	$0
	movw	%fs,(%esp)
	.endm

	.macro	PUSH_FRAME
	pushl	$0		/* dummy error code */
	pushl	$0		/* dummy trap type */
	PUSH_FRAME2
	.endm
	
/*
 * Access per-CPU data.
 */
#define	PCPU(member)	%fs:PC_ ## member

#define	PCPU_ADDR(member, reg)						\
	movl %fs:PC_PRVSPACE, reg ;					\
	addl $PC_ ## member, reg

/*
 * Setup the kernel segment registers.
 */
	.macro	SET_KERNEL_SREGS
	movl	$KDSEL, %eax	/* reload with kernel's data segment */
	movl	%eax, %ds
	movl	%eax, %es
	movl	$KPSEL, %eax	/* reload with per-CPU data segment */
	movl	%eax, %fs
	.endm

	.macro	NMOVE_STACKS
	movl	PCPU(KESP0), %edx
	movl	$TF_SZ, %ecx
	testl	$PSL_VM, TF_EFLAGS(%esp)
	jz	.L\@.1
	addl	$VM86_STACK_SPACE, %ecx
.L\@.1:	subl	%ecx, %edx
	movl	%edx, %edi
	movl	%esp, %esi
	rep; movsb
	movl	%edx, %esp
	.endm

	.macro	LOAD_KCR3
	call	.L\@.1
.L\@.1:	popl	%eax
	movl	(tramp_idleptd - .L\@.1)(%eax), %eax
	movl	%eax, %cr3
	.endm

	.macro	MOVE_STACKS
	LOAD_KCR3
	NMOVE_STACKS
	.endm

	.macro	KENTER
	testl	$PSL_VM, TF_EFLAGS(%esp)
	jz	.L\@.1
	LOAD_KCR3
	movl	PCPU(CURPCB), %eax
	testl	$PCB_VM86CALL, PCB_FLAGS(%eax)
	jnz	.L\@.3
	NMOVE_STACKS
	movl	$handle_ibrs_entry,%edx
	call	*%edx
	jmp	.L\@.3
.L\@.1:	testb	$SEL_RPL_MASK, TF_CS(%esp)
	jz	.L\@.3
.L\@.2:	MOVE_STACKS
	movl	$handle_ibrs_entry,%edx
	call	*%edx
.L\@.3:
	.endm

#endif /* LOCORE */

#ifdef __STDC__
#define ELFNOTE(name, type, desctype, descdata...) \
.pushsection .note.name                 ;       \
  .align 4                              ;       \
  .long 2f - 1f         /* namesz */    ;       \
  .long 4f - 3f         /* descsz */    ;       \
  .long type                            ;       \
1:.asciz #name                          ;       \
2:.align 4                              ;       \
3:desctype descdata                     ;       \
4:.align 4                              ;       \
.popsection
#else /* !__STDC__, i.e. -traditional */
#define ELFNOTE(name, type, desctype, descdata) \
.pushsection .note.name                 ;       \
  .align 4                              ;       \
  .long 2f - 1f         /* namesz */    ;       \
  .long 4f - 3f         /* descsz */    ;       \
  .long type                            ;       \
1:.asciz "name"                         ;       \
2:.align 4                              ;       \
3:desctype descdata                     ;       \
4:.align 4                              ;       \
.popsection
#endif /* __STDC__ */

#endif /* !_MACHINE_ASMACROS_H_ */
