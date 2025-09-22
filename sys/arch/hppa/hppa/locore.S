/*	$OpenBSD: locore.S,v 1.208 2025/07/16 07:15:42 jsg Exp $	*/

/*
 * Copyright (c) 1998-2004 Michael Shalayeff
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
 *
 * Portitions of this file are derived from other sources, see
 * the copyrights and acknowledgements below.
 */
/*
 *  (c) Copyright 1988 HEWLETT-PACKARD COMPANY
 *
 *  To anyone who acknowledges that this file is provided "AS IS"
 *  without any express or implied warranty:
 *      permission to use, copy, modify, and distribute this file
 *  for any purpose is hereby granted without fee, provided that
 *  the above copyright notice and this notice appears in all
 *  copies, and that the name of Hewlett-Packard Company not be
 *  used in advertising or publicity pertaining to distribution
 *  of the software without specific, written prior permission.
 *  Hewlett-Packard Company makes no representations about the
 *  suitability of this software for any purpose.
 */
/*
 * Copyright (c) 1990,1991,1992,1994 The University of Utah and
 * the Computer Systems Laboratory (CSL).  All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 *	Utah $Hdr: locore.s 1.63 95/01/20$
 */

#include <sys/reboot.h>
#include <machine/param.h>
#include <machine/asm.h>
#include <machine/psl.h>
#include <machine/trap.h>
#include <machine/iomod.h>
#include <machine/pdc.h>
#include <machine/frame.h>
#include <machine/reg.h>
#include "assym.h"

/*
 * hw-specific instructions
 */

/* source: mklinux cache.s */
#define	MFCPU_C_PCXST(r,x)	.word	0x14001a00 | ((r) << 21) | ((x) << 16)
#define	MTCPU_PCXST(x,r)	.word	0x14001600 | ((r) << 21) | ((x) << 16)

/* source: PCXL and PCXL2 ERS */
/* Use MFCPU_C for DR0-8; MFCPU_T for DR25,27,28,29 */
#define	MFCPU_C_PCXL(r,x)	.word	0x14000600 | ((r) << 21) | ((x) << 16)
#define	MFCPU_T_PCXL(r,x)	.word	0x14001800 | ((r) << 21) | ((x))
#define	MTCPU_PCXL(x,r)		.word	0x14000240 | ((r) << 21) | ((x) << 16)
#define	DR_PAGE0_PCXL		.word	0x14000e00
#define	DR_PAGE1_PCXL		.word	0x14000e40

#define	MFCPU_PCXU(r,x)		.word	0x140008a0 | ((r) << 21) | ((x))
#define	MTCPU_PCXU(x,r)		.word	0x14001840 | ((r) << 21) | ((x) << 16)

	.import	$global$, data
	.import pdc, data
	.import cpu_info, data
	.import	panic, code

LEAF_ENTRY($kernel_setup)

	/*
	 * disable interrupts and turn off all bits in the psw so that
	 * we start in a known state.
	 */
	rsm	RESET_PSL, r0
	nop ! nop ! nop ! nop ! nop ! nop

	/* get things ready for the kernel to run in virtual mode */
	ldi	HPPA_PID_KERNEL, r1
	mtctl	r1, pidr1
	mtctl	r1, pidr2
#if pbably_not_worth_it
	mtctl	r0, pidr3
	mtctl	r0, pidr4
#endif
	mtsp	r0, sr0
	mtsp	r0, sr1
	mtsp	r0, sr2
	mtsp	r0, sr3
	mtsp	r0, sr4
	mtsp	r0, sr5
	mtsp	r0, sr6
	mtsp	r0, sr7

	/*
	 * to keep the spl() routines consistent we need to put the correct
	 * spl level into eiem, and reset any pending interrupts
	 */
	ldi	-1, r1
	mtctl	r0, eiem
	mtctl	r1, eirr

	/*
	 * load address of interrupt vector table
	 */
	ldil	L%$ivaaddr, t2
	ldo	R%$ivaaddr(t2), t2
	mtctl	t2, iva

	/*
	 * set up the dp pointer so that we can do quick references off of it
	 */
	ldil	L%$global$,dp
	ldo	R%$global$(dp),dp

	/*
	 * Create a stack frame for us to call C with. Clear out the previous
	 * sp marker to mark that this is the first frame on the stack.
	 */
	copy	arg1, sp
	ldo	0(arg1), r3
	stw,ma	r0, HPPA_FRAME_SIZE(sp)
	stw	r0, HPPA_FRAME_CRP(sp)
	stw	r0, HPPA_FRAME_PSP(sp)

	/*
	 * We need to set the Q bit so that we can take TLB misses after we
	 * turn on virtual memory.
	 */
	mtctl	r0, pcsq
	mtctl	r0, pcsq
	mtctl	rp, pcoq
	ldo	4(rp), rp
	mtctl	rp, pcoq
	mtctl	arg2, ipsw
	rfi
	nop
	nop
EXIT($kernel_setup)

/* int
 * pdc_call(func, pdc_flag, ...)
 *	iodcio_t func;
 *	int pdc_flag;
 */
ENTRY(pdc_call,160)

	mfctl	eiem, t1
	mtctl	r0, eiem
	stw	rp, HPPA_FRAME_CRP(sp)
	copy	arg0, r31
	copy	sp, ret1

	ldil	L%kernelmapped, ret0
	ldw	R%kernelmapped(ret0), ret0
	comb,=	r0, ret0, pdc_call_unmapped1
	nop
	ldil	L%pdc_stack, ret1
	ldw	R%pdc_stack(ret1), ret1

pdc_call_unmapped1
	copy	sp, r1
	ldo	HPPA_FRAME_SIZE+24*4(ret1), sp

	stw	r1, HPPA_FRAME_PSP(sp)

	/* save kernelmapped and eiem */
	stw	ret0, HPPA_FRAME_ARG(21)(sp)
	stw	t1, HPPA_FRAME_ARG(22)(sp)

	/* copy arguments */
	copy	arg2, arg0
	copy	arg3, arg1
	ldw	HPPA_FRAME_ARG(4)(r1), arg2
	ldw	HPPA_FRAME_ARG(5)(r1), arg3
	ldw	HPPA_FRAME_ARG(6)(r1), t1
	ldw	HPPA_FRAME_ARG(7)(r1), t2
	ldw	HPPA_FRAME_ARG(8)(r1), t3
	ldw	HPPA_FRAME_ARG(9)(r1), t4
	stw	t1, HPPA_FRAME_ARG(4)(sp)	/* XXX can use ,bc */
	stw	t2, HPPA_FRAME_ARG(5)(sp)
	stw	t3, HPPA_FRAME_ARG(6)(sp)
	stw	t4, HPPA_FRAME_ARG(7)(sp)
	ldw	HPPA_FRAME_ARG(10)(r1), t1
	ldw	HPPA_FRAME_ARG(11)(r1), t2
	ldw	HPPA_FRAME_ARG(12)(r1), t3
	ldw	HPPA_FRAME_ARG(13)(r1), t4
	stw	t1, HPPA_FRAME_ARG(8)(sp)
	stw	t2, HPPA_FRAME_ARG(9)(sp)
	stw	t3, HPPA_FRAME_ARG(10)(sp)
	stw	t4, HPPA_FRAME_ARG(11)(sp)

	/* save temp control regs */
	mfctl	cr24, t1
	mfctl	cr25, t2
	mfctl	cr26, t3
	mfctl	cr27, t4
	stw	t1, HPPA_FRAME_ARG(12)(sp)	/* XXX can use ,bc */
	stw	t2, HPPA_FRAME_ARG(13)(sp)
	stw	t3, HPPA_FRAME_ARG(14)(sp)
	stw	t4, HPPA_FRAME_ARG(15)(sp)
	mfctl	cr28, t1
	mfctl	cr29, t2
	mfctl	cr30, t3
	mfctl	cr31, t4
	stw	t1, HPPA_FRAME_ARG(16)(sp)
	stw	t2, HPPA_FRAME_ARG(17)(sp)
	stw	t3, HPPA_FRAME_ARG(18)(sp)
	stw	t4, HPPA_FRAME_ARG(19)(sp)

	comb,=	r0, ret0, pdc_call_unmapped2
	nop

	copy	arg0, t4
	ldi	PSL_Q, arg0 /* (!pdc_flag && args[0] == PDC_PIM)? PSL_M:0) */
	break	HPPA_BREAK_KERNEL, HPPA_BREAK_SET_PSW
	stw	ret0, HPPA_FRAME_ARG(23)(sp)
	copy	t4, arg0

pdc_call_unmapped2
	.call
	blr	r0, rp
	bv,n	(r31)
	nop

	/* load temp control regs */
	ldw	HPPA_FRAME_ARG(12)(sp), t1
	ldw	HPPA_FRAME_ARG(13)(sp), t2
	ldw	HPPA_FRAME_ARG(14)(sp), t3
	ldw	HPPA_FRAME_ARG(15)(sp), t4
	mtctl	t1, cr24
	mtctl	t2, cr25
	mtctl	t3, cr26
	mtctl	t4, cr27
	ldw	HPPA_FRAME_ARG(16)(sp), t1
	ldw	HPPA_FRAME_ARG(17)(sp), t2
	ldw	HPPA_FRAME_ARG(18)(sp), t3
	ldw	HPPA_FRAME_ARG(19)(sp), t4
	mtctl	t1, cr28
	mtctl	t2, cr29
	mtctl	t3, cr30
	mtctl	t4, cr31

	ldw	HPPA_FRAME_ARG(21)(sp), t1
	ldw	HPPA_FRAME_ARG(22)(sp), t2
	comb,=	r0, t1, pdc_call_unmapped3
	nop

	copy	ret0, t3
	ldw	HPPA_FRAME_ARG(23)(sp), arg0
	break	HPPA_BREAK_KERNEL, HPPA_BREAK_SET_PSW
	copy	t3, ret0

pdc_call_unmapped3
	ldw	HPPA_FRAME_PSP(sp), sp
	ldw	HPPA_FRAME_CRP(sp), rp
	bv	r0(rp)
	mtctl	t2, eiem
EXIT(pdc_call)

/*
 * Kernel Gateway Page (must be at known address)
 *	System Call Gate
 *	Signal Return Gate
 *
 * GATEway instructions have to be at a fixed known locations
 * because their addresses are hard coded in routines such as
 * those in the C library.
 */
	.align	NBPG
	.export	gateway_page, entry
gateway_page
	nop				/* @ 0.C0000000 (Nothing)  */
	gate,n	$bsd_syscall,r0		/* @ 0.C0000004 (HPUX/BSD) */
	nop				/* @ 0.C0000008 (HPOSF UNIX) */
	nop				/* @ 0.C000000C (HPOSF Mach) */
	nop
	nop
	nop
	nop

$bsd_syscall
	/*
	 * set up a space register and a protection id so that
	 * we can access kernel memory
	 */
	mfctl	eiem, r1
	mtctl	r0, eiem
	mtsp	r0, sr1
	mfctl	pidr1, ret0
	ldi	HPPA_PID_KERNEL, t2
	mtctl	t2, pidr1

	.import $syscall,code
	.call
	ldil	L%$syscall, t2
	be	R%$syscall(sr1, t2)
	nop ! nop ! nop ! nop

	.size	gateway_page, .-gateway_page
	.align	NBPG
	.export	gateway_page_end, entry
gateway_page_end

	.export $syscall,entry
	.proc
	.callinfo calls
	.entry
$syscall
	/*
	 *
	 * t1:	syscall number
	 * t2:	user
	 * t3:	args
	 * t4:	user stack
	 *
	 */
	mfctl	cr29, t2
	ldw	CI_CURPROC(sr1, t2), t2
	ldw	P_ADDR(sr1, t2), t3	/* XXX can use ,sl */

	/* calculate kernel sp, load, create kernel stack frame */
	ldo	NBPG(t3), t3
	stw	t3, P_MD_REGS(sr1, t2)
	ldo	TRAPFRAME_SIZEOF(t3), t3
	stw	t4, TF_R19 -TRAPFRAME_SIZEOF(sr1, t3)	/* t4 for vfork() */
	stw	t1, TF_R22 -TRAPFRAME_SIZEOF(sr1, t3)	/* syscall # */
	/* gotta save the args, in case we gonna restart */
	stw	arg3, TF_R23-TRAPFRAME_SIZEOF(sr1, t3)
	stw	arg2, TF_R24-TRAPFRAME_SIZEOF(sr1, t3)
	stw	arg1, TF_R25-TRAPFRAME_SIZEOF(sr1, t3)
	stw	arg0, TF_R26-TRAPFRAME_SIZEOF(sr1, t3)
	stw	r27, TF_R27-TRAPFRAME_SIZEOF(sr1, t3)	/* dp */
	stw	sp, TF_R30 -TRAPFRAME_SIZEOF(sr1, t3)	/* user stack */
	copy	t3, sp
	stw,ma	r0, HPPA_FRAME_SIZE+HPPA_FRAME_MAXARGS(sr1, sp)
	stw	r0, HPPA_FRAME_CRP(sr1, sp)
	mfctl	r29, t1
	ldw	CI_PSW(sr1, t1), t1
	stw	r1, TF_CR15-TRAPFRAME_SIZEOF(sr1, t3)	/* eiem ,bc */
	stw	t1, TF_CR22-TRAPFRAME_SIZEOF(sr1, t3)	/* ipsw */
	mfsp	sr3, t1
	stw	t1, TF_SR3-TRAPFRAME_SIZEOF(sr1, t3)
	stw	ret0, TF_CR8-TRAPFRAME_SIZEOF(sr1, t3)	/* pidr1 */
	/* now we can allow interrupts to happen */
	mtctl	r1, eiem

	/*
	 * we believe that any callee-save registers
	 * will be saved accordingly in either syscall()
	 * or deeper called functions and caller-save
	 * are saved in userland.
	 */
	stw	r2 , TF_R2 -TRAPFRAME_SIZEOF(sr1, t3)
	stw	r3 , TF_R3 -TRAPFRAME_SIZEOF(sr1, t3)
	copy	t3, r3
	/* save callee-save registers */
	stw	r4 , TF_R4 -TRAPFRAME_SIZEOF(sr1, t3)
	stw	r5 , TF_R5 -TRAPFRAME_SIZEOF(sr1, t3)
	stw	r6 , TF_R6 -TRAPFRAME_SIZEOF(sr1, t3)
	stw	r7 , TF_R7 -TRAPFRAME_SIZEOF(sr1, t3)
	stw	r8 , TF_R8 -TRAPFRAME_SIZEOF(sr1, t3)
	stw	r9 , TF_R9 -TRAPFRAME_SIZEOF(sr1, t3)
	stw	r10, TF_R10-TRAPFRAME_SIZEOF(sr1, t3)
	stw	r11, TF_R11-TRAPFRAME_SIZEOF(sr1, t3)
	stw	r12, TF_R12-TRAPFRAME_SIZEOF(sr1, t3)
	stw	r13, TF_R13-TRAPFRAME_SIZEOF(sr1, t3)
	stw	r14, TF_R14-TRAPFRAME_SIZEOF(sr1, t3)
	stw	r15, TF_R15-TRAPFRAME_SIZEOF(sr1, t3)
	stw	r16, TF_R16-TRAPFRAME_SIZEOF(sr1, t3)
	stw	r17, TF_R17-TRAPFRAME_SIZEOF(sr1, t3)
	stw	r18, TF_R18-TRAPFRAME_SIZEOF(sr1, t3)
	/*
	 * Save the rest of the CPU context
	 */
	mfsp	sr0, arg0				/* use ,bc */
	stw	arg0, TF_IISQH-TRAPFRAME_SIZEOF(sr1, t3)
	stw	arg0, TF_IISQT-TRAPFRAME_SIZEOF(sr1, t3)

	ldo	4(r31), arg1
	stw	r31, TF_IIOQH-TRAPFRAME_SIZEOF(sr1, t3)
	stw	arg1, TF_IIOQT-TRAPFRAME_SIZEOF(sr1, t3)

	stw	arg0, TF_CR20-TRAPFRAME_SIZEOF(sr1, t3)	/* use ,bc */
	stw	r31, TF_CR21-TRAPFRAME_SIZEOF(sr1, t3)

	ldil	L%TFF_LAST|TFF_SYS, arg1
	stw	r0, TF_CR19-TRAPFRAME_SIZEOF(sr1, t3)	/* iir */
	stw	arg1, TF_FLAGS-TRAPFRAME_SIZEOF(sr1, t3)

	mfsp	sr2, arg2
	mfsp	sr4, arg3
	stw	arg0, TF_SR0-TRAPFRAME_SIZEOF(sr1, t3)
	stw	arg0, TF_SR1-TRAPFRAME_SIZEOF(sr1, t3)
	stw	arg2, TF_SR2-TRAPFRAME_SIZEOF(sr1, t3)
	stw	arg3, TF_SR4-TRAPFRAME_SIZEOF(sr1, t3)

	mfsp	sr5, arg0
	mfsp	sr6, arg1
	mfsp	sr7, arg2
	mfctl	pidr2, arg3
	stw	arg0, TF_SR5-TRAPFRAME_SIZEOF(sr1, t3)
	stw	arg1, TF_SR6-TRAPFRAME_SIZEOF(sr1, t3)
	stw	arg2, TF_SR7-TRAPFRAME_SIZEOF(sr1, t3)
	stw	arg3, TF_CR9-TRAPFRAME_SIZEOF(sr1, t3)

#if pbably_not_worth_it
	mfctl	pidr3, arg2
	mfctl	pidr4, arg3
	stw	arg2, TF_CR12-TRAPFRAME_SIZEOF(sr1, t3)
	stw	arg3, TF_CR13-TRAPFRAME_SIZEOF(sr1, t3)
#endif

#ifdef DDB
	/*
	 * Save hpt mask and v2p translation table pointer
	 */
	mfctl	eirr, arg0
	mfctl	vtop, arg1
	stw	arg0, TF_CR23-TRAPFRAME_SIZEOF(sr1, t3)
	stw	arg1, TF_CR25-TRAPFRAME_SIZEOF(sr1, t3)

	mfctl	cr28, arg1
	stw	arg1, TF_CR28-TRAPFRAME_SIZEOF(sr1, t3)
#endif

	/* setup kernel context */
	mtsp	r0, sr0
	mtsp	r0, sr1
	mtsp	r0, sr2
	mtsp	r0, sr3
	mtsp	r0, sr4
	mtsp	r0, sr5
	mtsp	r0, sr6
	mtsp	r0, sr7

	ldo	-TRAPFRAME_SIZEOF(t3), arg0
	ldo	4(t3), arg1

	ldil	L%$global$,dp
	ldo	R%$global$(dp),dp

	/* do a syscall */
	.import	syscall,code
	ldil	L%syscall, r1
	ldo	R%syscall(r1), r1
	.call
	blr	r0, rp
	bv,n	0(r1)
	nop

	mfctl	cr29, r1
	ldw	CI_CURPROC(r1), r1
	ldw	P_MD_REGS(r1), t3

	.exit
	.procend
	/* FALLTHROUGH */

	.export	$syscall_return, entry
	.proc
	.callinfo no_calls
	.entry
$syscall_return
	/* t3 == VA trapframe */

	/* disable interrupts, just in case */
#ifdef MULTIPROCESSOR
	mfctl	eiem, t1
	stw	t1, TF_CR15(t3)
#endif
	mtctl	r0, eiem

	/*
	 * 1a. Copy a `phys' part of the frame into temp store
	 *	(see a note for trapall)
	 *	hopefully no page fault would happen on or after the copy,
	 *	and interrupts are disabled.
	 */
	mfctl	cr29, t2
	ldo	CI_TRAP_SAVE(t2), t2
	/* use ,bc each cache line */
	ldw  0(t3), r1 ! ldw  4(t3), t1 ! stw r1,  0(t2) ! stw t1,  4(t2)
	ldw  8(t3), r1 ! ldw 12(t3), t1 ! stw r1,  8(t2) ! stw t1, 12(t2)
	ldw 16(t3), r1 ! ldw 20(t3), t1 ! stw r1, 16(t2) ! stw t1, 20(t2)
	ldw 24(t3), r1 ! ldw 28(t3), t1 ! stw r1, 24(t2) ! stw t1, 28(t2)
	ldw 32(t3), r1 ! ldw 36(t3), t1 ! stw r1, 32(t2) ! stw t1, 36(t2)
	ldw 40(t3), r1 ! ldw 44(t3), t1 ! stw r1, 40(t2) ! stw t1, 44(t2)
	ldw 48(t3), r1 ! ldw 52(t3), t1 ! stw r1, 48(t2) ! stw t1, 52(t2)
	ldw 56(t3), r1 ! ldw 60(t3), t1 ! stw r1, 56(t2) ! stw t1, 60(t2)

	/* 1b. restore most of the general registers */
	ldw	TF_CR11(t3), t1
	mtctl	t1, sar
	ldw	TF_R1(t3), r1
	ldw	TF_R2(t3), r2
	ldw	TF_R3(t3), r3
	ldw	TF_R4(t3), r4
	ldw	TF_R5(t3), r5
	ldw	TF_R6(t3), r6
	ldw	TF_R7(t3), r7
	ldw	TF_R8(t3), r8
	ldw	TF_R9(t3), r9
	ldw	TF_R10(t3), r10
	ldw	TF_R11(t3), r11
	ldw	TF_R12(t3), r12
	ldw	TF_R13(t3), r13
	ldw	TF_R14(t3), r14
	ldw	TF_R15(t3), r15
	ldw	TF_R16(t3), r16
	ldw	TF_R17(t3), r17
	ldw	TF_R18(t3), r18
	ldw	TF_R19(t3), t4
	/*	r20(t3) is used as a temporary and will be restored later */
	/*	r21(t2) is used as a temporary and will be restored later */
	/*	r22(t1) is used as a temporary and will be restored later */
	ldw	TF_R23(t3), r23
	ldw	TF_R24(t3), r24
	ldw	TF_R25(t3), r25
	ldw	TF_R26(t3), r26
	ldw	TF_R27(t3), r27
	ldw	TF_R28(t3), r28
	ldw	TF_R29(t3), r29
	/*	r30 (sp) will be restored later */
	ldw	TF_R31(t3), r31

	/* 2. restore all the space regs and pid regs, except sr3, pidr1 */
	ldw	TF_SR0(t3), t1
	ldw	TF_SR1(t3), t2
	mtsp	t1, sr0
	mtsp	t2, sr1

	ldw	TF_SR2(sr3, t3), t1
	ldw	TF_SR4(sr3, t3), t2
	mtsp	t1, sr2
	mtsp	t2, sr4

	ldw	TF_SR5(sr3, t3), t1
	ldw	TF_SR6(sr3, t3), t2
	mtsp	t1, sr5
	mtsp	t2, sr6

	ldw	TF_SR7(sr3, t3), t1
	ldw	TF_CR9(sr3, t3), t2
	mtsp	t1, sr7
	mtctl	t2, pidr2

#if pbably_not_worth_it
	ldw	TF_CR12(sr3, t3), t1
	ldw	TF_CR13(sr3, t3), t2
	mtctl	t1, pidr3
	mtctl	t2, pidr4
#endif
	ldw	TF_CR0(sr3, t3), t1
	mtctl	t1, rctr
	ldw	TF_CR27(sr3, t3), t1
	ldw	TF_CR30(sr3, t3), t2
	mtctl	t1, cr27
	mtctl	t2, cr30

	/*
	 * clear the system mask, this puts us back into physical mode.
	 * reload trapframe pointer w/ correspondent PA value.
	 * sp will be left in virtual until restored from trapframe,
	 * since we don't use it anyway.
	 */
	ssm	0, r0
	mfctl	cr29, t3
	ldo	CI_TRAP_SAVE(t3), t3
	nop ! nop ! nop ! nop ! nop
	rsm	RESET_PSL, r0
$syscall_return_phys

	/* clear cr26 to avoid information leak */
	mtctl	r0, cr26

	/* finally we can restore the space and offset queues and the ipsw */
	ldw	TF_IISQH(t3), t1
	ldw	TF_IISQT(t3), t2
	mtctl	t1, pcsq
	mtctl	t2, pcsq

	ldw	TF_IIOQH(t3), t1
	ldw	TF_IIOQT(t3), t2
	mtctl	t1, pcoq
	mtctl	t2, pcoq

	ldw	TF_CR15(t3), t1
	ldw	TF_CR22(t3), t2
	mtctl	t1, eiem
	mtctl	t2, ipsw

	ldw	TF_SR3(t3), t1
	ldw	TF_CR8(t3), t2
	mtsp	t1, sr3
	mtctl	t2, pidr1

	ldw	TF_R22(t3), t1
	ldw	TF_R21(t3), t2
	ldw	TF_R30(t3), sp
	ldw	TF_R20(t3), t3

	rfi
	nop
	.exit
	.procend
	.size	$syscall, .-$syscall
$syscall_end

/*
 * interrupt vector table
 */
#define	TLABEL(name)	$trap$name
#define TRAP(name,num) \
	mtctl	r1, tr7			! \
	.call				! \
	.import TLABEL(name), code	! \
	b	TLABEL(name)		! \
	ldi	num, r1			! \
	.align	32

#define	ATRAP(name,num) \
	.export	TLABEL(name)$num, entry	! \
	.label	TLABEL(name)$num	! \
	TRAP(all,num)			! \
	.size	TLABEL(name)$num, .-TLABEL(name)$num

#define	CTRAP(name,num,pre) \
	.export	TLABEL(name)$num, entry	! \
	.label	TLABEL(name)$num	! \
	pre				! \
	TRAP(name,num)			! \
	.size	TLABEL(name)$num, .-TLABEL(name)$num

#define	STRAP(name,num,pre) \
	.export	TLABEL(name)$num, entry	! \
	.label	TLABEL(name)$num	! \
	pre				! \
	mtctl	r1, tr7			! \
	.export	trap_ep_##num, entry	! \
	.label	trap_ep_##num		! \
	.call				! \
	b	__CONCAT($name,_l)	! \
	ldi	num, r1			! \
	b	__CONCAT($name,_t)+8	! \
	b	__CONCAT($name,_s)+12	! \
	b	__CONCAT($name,_u)+16	! \
	.size	TLABEL(name)$num, .-TLABEL(name)$num

#define	ITLBPRE \
	mfctl	pcoq,r9 ! \
	mfctl	pcsq,r8
#define	DTLBPRE \
	mfctl	ior, r9 ! \
	mfctl	isr, r8	
	/* CR28XXX according to a popular belief cr28 should be read here */

#define	HPMCPRE	nop
#define	INTRPRE	\
	mfctl	eirr, r8	! \
	mtctl	r8, eirr

	.align NBPG
	.export $ivaaddr, entry
	.export hpmc_v, entry
$ivaaddr
	ATRAP(null,T_NONEXIST)		/*  0. invalid interrupt vector */
hpmc_v
	CTRAP(hpmc,T_HPMC,HPMCPRE)	/*  1. high priority machine check */
	ATRAP(power,T_POWERFAIL)	/*  2. power failure */
	ATRAP(recnt,T_RECOVERY)		/*  3. recovery counter trap */
	CTRAP(intr,T_INTERRUPT,INTRPRE)	/*  4. external interrupt */
	ATRAP(lpmc,T_LPMC)		/*  5. low-priority machine check */
	STRAP(itlb,T_ITLBMISS,ITLBPRE)	/*  6. instruction TLB miss fault */
	ATRAP(iprot,T_IPROT)		/*  7. instruction protection trap */
	ATRAP(ill,T_ILLEGAL)		/*  8. Illegal instruction trap */
	CTRAP(ibrk,T_IBREAK,)		/*  9. break instruction trap */
	ATRAP(privop,T_PRIV_OP)		/* 10. privileged operation trap */
	ATRAP(privr,T_PRIV_REG)		/* 11. privileged register trap */
	ATRAP(ovrfl,T_OVERFLOW)		/* 12. overflow trap */
	ATRAP(cond,T_CONDITION)		/* 13. conditional trap */
	CTRAP(excpt,T_EXCEPTION,)	/* 14. assist exception trap */
	STRAP(dtlb,T_DTLBMISS,DTLBPRE)	/* 15. data TLB miss fault */
	STRAP(itlbna,T_ITLBMISSNA,DTLBPRE)/* 16. ITLB non-access miss fault */
	STRAP(dtlbna,T_DTLBMISSNA,DTLBPRE)/* 17. DTLB non-access miss fault */
	ATRAP(dprot,T_DPROT)		/* 18. data protection trap
					      unaligned data reference trap */
	ATRAP(dbrk,T_DBREAK)		/* 19. data break trap */
	STRAP(tlbd,T_TLB_DIRTY,DTLBPRE)	/* 20. TLB dirty bit trap */
	ATRAP(pgref,T_PAGEREF)		/* 21. page reference trap */
	CTRAP(emu,T_EMULATION,)		/* 22. assist emulation trap */
	ATRAP(hpl,T_HIGHERPL)		/* 23. higher-privilege transfer trap */
	ATRAP(lpl,T_LOWERPL)		/* 24. lower-privilege transfer trap */
	ATRAP(tknbr,T_TAKENBR)		/* 25. taken branch trap */
	ATRAP(dacc,T_DATACC)		/* 26. data access rights trap */
	ATRAP(dpid,T_DATAPID)		/* 27. data protection ID trap */
	ATRAP(dalgn,T_DATALIGN)		/* 28. unaligned data ref trap */
	ATRAP(unk29,29)
	ATRAP(unk30,30)
	ATRAP(unk31,31)
	ATRAP(unk32,32)
	ATRAP(unk33,33)
	ATRAP(unk34,34)
	ATRAP(unk35,35)
	ATRAP(unk36,36)
	ATRAP(unk37,37)
	ATRAP(unk38,38)
	ATRAP(unk39,39)
	ATRAP(unk40,40)
	ATRAP(unk41,41)
	ATRAP(unk42,42)
	ATRAP(unk43,43)
	ATRAP(unk44,44)
	ATRAP(unk45,45)
	ATRAP(unk46,46)
	ATRAP(unk47,47)
	ATRAP(unk48,48)
	ATRAP(unk49,49)
	ATRAP(unk50,50)
	ATRAP(unk51,51)
	ATRAP(unk52,52)
	ATRAP(unk53,53)
	ATRAP(unk54,54)
	ATRAP(unk55,55)
	ATRAP(unk56,56)
	ATRAP(unk57,57)
	ATRAP(unk58,58)
	ATRAP(unk59,59)
	ATRAP(unk60,60)
	ATRAP(unk61,61)
	ATRAP(unk62,62)
	ATRAP(unk63,63)
					/* 64 */
	.size	$ivaaddr, .-$ivaaddr

	.export TLABEL(excpt), entry
ENTRY(TLABEL(excpt),0)
	/* assume we never get this one w/o fpu [enabled] */
	copy	rp, r1
	copy	arg0, r8
	mfctl	cr30, r9
#if HFP_REGS != 0
	ldo	HFP_REGS(r9), r9
#endif
	.import	fpu_save, code
	.call
	bl	fpu_save, rp
	copy	r9, arg0
	copy	r1, rp
	copy	r8, arg0
	mfctl	cr29, r1
	mtctl	r0, ccr		/* cause a reload after exception */
	ldw	CI_FPU_STATE(r1), r16
	stw	r0, CI_FPU_STATE(r1)
	stw	r0, HFP_CPU(r16)
	sync

	/* now, check for trap */
	ldw	0(r9), r1
	bb,>=,n	r1, HPPA_FPU_T_POS, excpt_notrap
	ldw	1*4(r9), r1
	comb,<>,n r0, r1, excpt_emulate
	ldw	2*4(r9), r1
	comb,<>,n r0, r1, excpt_emulate
	ldw	3*4(r9), r1
	comb,<>,n r0, r1, excpt_emulate
	ldw	4*4(r9), r1
	comb,<>,n r0, r1, excpt_emulate
	ldw	5*4(r9), r1
	comb,<>,n r0, r1, excpt_emulate
	ldw	6*4(r9), r1
	comb,<>,n r0, r1, excpt_emulate
	ldw	7*4(r9), r1

excpt_emulate
	bb,>=,n r1, 5, excpt_notrap	/* HPPA_FPU_UNMPL not set */

	ldw	0(r9), r16
	depi	0, HPPA_FPU_T_POS, 1, r16
	.import	$fpu_emulate, code
	b	$fpu_emulate
	stw	r16, 0(r9)

excpt_notrap
	sync
	b	TLABEL(all)
	ldi	T_EXCEPTION, r1
EXIT(TLABEL(excpt))

	.export TLABEL(emu), entry
ENTRY(TLABEL(emu),0)

	/*
	 * Switch FPU/SFU context
	 *
	 * isr:ior - data address
	 * iir - instruction to emulate
	 * iisq:iioq - address of instruction to emulate
	 *
	 * note: ISR and IOR contain valid data only if the
	 *	 instruction is a coprocessor load or store.
	 *
	 */

	mfctl	iir, r8
	extru	r8, 5, 6, r9	/* no sfu implementation right now */
	comib,=	4, r9, TLABEL(all)
	ldi	T_ILLEGAL, r1

	/*
	 * pass through for all coprocessors now and
	 * do not check the uid here.
	 * in case that piece does not exist emulate
	 * or the trap will be generated later.
	 */

	ldil	L%cpu_fpuena, r1
	ldw	R%cpu_fpuena(r1), r9
	comib,=	0, r9, $fpusw_emu

	/* if we are already enabled and hit again, emulate */
	mfctl	ccr, r1
	extru,<> r1, 25, 2, r0
	b,n	$fpusw_set
	nop

$fpusw_emu
	mfctl	cr29, r1
	mtctl	r0, ccr		/* cause a reload after exception */
	ldw	CI_FPU_STATE(r1), r16
	stw	r0, CI_FPU_STATE(r1)
	stw	r0, HFP_CPU(r16)
	sync
#if 0
	/* here we emulate the fld/fst */
	mfctl	iir, r1
	extru	r1, 5, 6, r1
	comib,=	0xb, r9, TLABEL(all)
	ldi	T_ILLEGAL, r1

	mfctl	iir, r1
	extru	r1, 5, 6, r1
	comib,=	0x9, r9, TLABEL(all)
	ldi	T_ILLEGAL, r1
#endif
	mfctl	iir, r1
	.import	$fpu_emulate, code
	b	$fpu_emulate
	nop

$fpusw_set
	/* enable coprocessor XXX */
	depi	3, 25, 2, r1
	mtctl	r1, ccr

	mfctl	cr29, r16
	mfctl	cr30, r9
	ldw	CI_FPU_STATE(r16), r16

	comb,=,n r16, r0, $fpusw_nosave
	comb,=,n r16, r9, $fpusw_done

	copy	arg0, r17
	copy	rp, r1
#if HFP_REGS != 0
	ldo	HFP_REGS(r16), r16
#endif
	.import	fpu_save, code
	.call
	bl	fpu_save, rp
	copy	r16, arg0
	copy	r1, rp
	copy	r17, arg0

	mfctl	cr29, r1
	ldw	CI_FPU_STATE(r1), r16
	stw	r0, CI_FPU_STATE(r1)
	stw	r0, HFP_CPU(r16)
	sync

$fpusw_nosave

#ifdef MULTIPROCESSOR
	/* See if this process has FPU context on another CPU. */
	ldw	HFP_CPU(r9), r1
	comb,=,n r1, r0, $fpusw_noshoot

	/* Perform FPU shootdown. */
	ldi	(1 << HPPA_IPI_FPU_SAVE), r1
	ldw	HFP_CPU(r9), r16
	stw	r1, CI_IPI(r16)
	ldi	1, r1
	ldw	CI_HPA(r16), r16
	stw	r1, 0(r16)

$fpusw_spin
	/* Wait for shootdown to complete. */
	sync
	ldw	HFP_CPU(r9), r1
	comb,<>,n r1, r0, $fpusw_spin

$fpusw_noshoot
#endif

	/* count switches */
	.import	uvmexp, data
	ldil	L%(uvmexp+FPSWTCH), r1
	ldw	R%(uvmexp+FPSWTCH)(r1), r16
	ldo	31*8+HFP_REGS(r9), r17
	ldo	1(r16), r16
	stw	r16, R%(uvmexp+FPSWTCH)(r1)

	fldds,ma -8(r17), fr31
	fldds,ma -8(r17), fr30
	fldds,ma -8(r17), fr29
	fldds,ma -8(r17), fr28
	fldds,ma -8(r17), fr27
	fldds,ma -8(r17), fr26
	fldds,ma -8(r17), fr25
	fldds,ma -8(r17), fr24
	fldds,ma -8(r17), fr23
	fldds,ma -8(r17), fr22
	fldds,ma -8(r17), fr21
	fldds,ma -8(r17), fr20
	fldds,ma -8(r17), fr19
	fldds,ma -8(r17), fr18
	fldds,ma -8(r17), fr17
	fldds,ma -8(r17), fr16
	fldds,ma -8(r17), fr15
	fldds,ma -8(r17), fr14
	fldds,ma -8(r17), fr13
	fldds,ma -8(r17), fr12
	fldds,ma -8(r17), fr11
	fldds,ma -8(r17), fr10
	fldds,ma -8(r17), fr9
	fldds,ma -8(r17), fr8
	fldds,ma -8(r17), fr7
	fldds,ma -8(r17), fr6
	fldds,ma -8(r17), fr5
	fldds,ma -8(r17), fr4
	fldds,ma -8(r17), fr3
	fldds,ma -8(r17), fr2
	fldds,ma -8(r17), fr1
	fldds     0(r17), fr0	/* fr0 must be restored last */

	mfctl	cr29, r1
	stw	r9, CI_FPU_STATE(r1)
	stw	r1, HFP_CPU(r9)
	sync

$fpusw_done
	rfir
	nop
EXIT(TLABEL(emu))

	/* Construct the virtual address tag. */
#define	VTAG ! \
	shd	r0, r9, 1, r16		/* r16[1..15] = off[0..14] */	! \
	dep	r8, 31, 16, r16		/* put in the space id */	! \
	depi	1, 0, 1, r16		/* and set the valid bit */

#if 0
	.export	dtlb_c, data
	BSS(dtlb_c, 8)
	.export	tlbd_c, data
	BSS(tlbd_c, 8)
	.export	itlb_c, data
	BSS(itlb_c, 8)

	.text
	/* XXX this touches tr5, which it should not, perhaps */

#define	TLB_STATS_PRE(t) \
	mfctl	itmr, r17	! \
	mtctl	r17, tr5
#define	TLB_STATS_AFT(t) \
	mfctl	itmr, r16			! \
	mfctl	tr5, r17			! \
	ldil	L%__CONCAT(t,_c), r25		! \
	ldo	R%__CONCAT(t,_c)(r25), r25	! \
	sub	r16, r17, r16			! \
	ldw	0(r25), r24			! \
	ldw	4(r25), r17			! \
	ldo	1(r24), r24			! \
	ldo	-2(r16), r16 /* for mtctl */	! \
	add	r16, r17, r17			! \
	stw	r24, 0(r25)			! \
	stw	r17, 4(r25)

#else
#define	TLB_STATS_PRE(t)	/**/
#define	TLB_STATS_AFT(t)	/**/
#endif

#if defined(HP7000_CPU) || defined(HP7100_CPU) || defined(HP7200_CPU)
#define	TLB_PULL(bits,lbl)							! \
	/* space:pgaddr -- r8:r9 */					! \
	mfctl	vtop, r16						! \
	ldwax,s	r8(r16), r17		/* space -> page directory */	! \
	extru	r9, 9, 10, r25						! \
	combt,=,n r0, r17, lbl						! \
	ldwax,s	r25(r17), r24		/* page -> page table */	! \
	extru	r9, 19, 10, r16						! \
	combt,=,n r0, r24, lbl						! \
	ldwax,s	r16(r24), r17		/* va -> pa:prot */		! \
	sh2addl	r16, r24, r25						! \
	combt,=,n r0, r17, lbl						! \
	copy	r17, r16						! \
	depi	(bits), 21+bits, 1+bits, r17				! \
	sub,=	r16, r17, r0		/* do not store if unchanged */	! \
	stwas	r17, 0(r25)		/* store back w/ the bits */	! \
	shd	r17, r0, 13, r25					! \
	dep	r8, 30, 15, r25		/* mix0r the pid from the sid */! \
	dep	r0, 31, 12, r17		/* needed ? */			! \
	addi	2, r25, r25						! \
	extru	r17, 24, 25, r17

	.align	32
LEAF_ENTRY($tlbd_s)
ALTENTRY($tlbd_t)
	TLB_STATS_PRE(tlbd)
	TLB_PULL(1, TLABEL(all))
	mfsp	sr1, r16
	mtsp	r8, sr1
	idtlba	r17,(sr1, r9)
	idtlbp	r25,(sr1, r9)
	mtsp	r16, sr1
	TLB_STATS_AFT(tlbd)
	rfir
	nop
EXIT($tlbd_s)

LEAF_ENTRY($itlb_s)
ALTENTRY($itlb_t)
	TLB_STATS_PRE(itlb)
	TLB_PULL(0, TLABEL(all))
	extru,=	r25, 5, 1, r0	/* gateway page needs to be public */
	depi	0, 30, 15, r25
	mfsp	sr1, r16
	mtsp	r8, sr1
	iitlba	r17,(sr1, r9)
	iitlbp	r25,(sr1, r9)
	mtsp	r16, sr1
	TLB_STATS_AFT(itlb)
	rfir
	nop
EXIT($itlb_s)

LEAF_ENTRY($dtlb_s)
ALTENTRY($dtlb_t)
	TLB_STATS_PRE(dtlb)
	TLB_PULL(0, TLABEL(all))
	mfsp	sr1, r16
	mtsp	r8, sr1
	idtlba	r17,(sr1, r9)
	idtlbp	r25,(sr1, r9)
	mtsp	r16, sr1
	TLB_STATS_AFT(dtlb)
	rfir
	nop
EXIT($dtlb_s)

LEAF_ENTRY($dtlbna_s)
ALTENTRY($itlbna_s)
ALTENTRY($dtlbna_t)
ALTENTRY($itlbna_t)
	TLB_STATS_PRE(dtlb)
	TLB_PULL(0, $dtlbna_t_fake)
	mfsp	sr1, r16
	mtsp	r8, sr1
	idtlba	r17,(sr1, r9)
	idtlbp	r25,(sr1, r9)
	mtsp	r16, sr1
	TLB_STATS_AFT(dtlb)
	rfir
	nop
$dtlbna_s_fake
$dtlbna_t_fake
	/* parse prober/w insns, have to decent to trap() to set regs proper */
	mfctl	iir, r16
	extru	r16, 6, 6, r24
	comib,=,n 1, r24, TLABEL(all)
	extru	r16, 24, 6, r24
	subi,<>	0x23, r24, r0
	b	TLABEL(all)
	/* otherwise generate a flush-only tlb entry */
	copy	r0, r17
	zdep	r8, 30, 15, r25
	depi	-13, 11, 7, r25
	ldo	2(r25), r25   /* 3? */
	mfsp	sr1, r16
	mtsp	r8, sr1
	idtlba	r17,(sr1, r9)
	idtlbp	r25,(sr1, r9)
	mtsp	r16, sr1
	TLB_STATS_AFT(dtlb)
	rfir
	nop
EXIT($dtlbna_s)

#endif /*  defined(HP7000_CPU) || defined(HP7100_CPU) || defined(HP7200_CPU) */

#if defined(HP7100LC_CPU) || defined(HP7300LC_CPU)

#define	IITLBAF(r)	.word	0x04000440 | ((r) << 16)
#define	IITLBPF(r)	.word	0x04000400 | ((r) << 16)
#define	IDTLBAF(r)	.word	0x04001440 | ((r) << 16)
#define	IDTLBPF(r)	.word	0x04001400 | ((r) << 16)

/*
 * possible optimizations:
 *	change pte to reduce number of shifts
 *	reorder to reduce stalls
 */
#define	TLB_PULL_L(bits,lbl)						! \
	/* space:pgaddr -- r8:r9 */					! \
	mfctl	vtop, r16						! \
	ldwx,s	r8(r16), r17		/* space -> page directory */	! \
	extru	r9, 9, 10, r25						! \
	combt,=,n r0, r17, lbl						! \
	ldwx,s	r25(r17), r24		/* page -> page table */	! \
	extru	r9, 19, 10, r16						! \
	combt,=,n r0, r24, lbl						! \
	ldwx,s	r16(r24), r17		/* va -> pa:prot */		! \
	sh2addl	r16, r24, r25						! \
	combt,=,n r0, r17, lbl						! \
	copy	r17, r16						! \
	depi	(bits), 21+bits, 1+bits, r17				! \
	sub,=	r16, r17, r0		/* do not store if unchanged */	! \
	stws	r17, 0(r25)		/* store back w/ the bits */	! \
	shd	r17, r0, 13, r25					! \
	dep	r8, 30, 15, r25		/* mix0r the pid from the sid */! \
	dep	r0, 31, 12, r17		/* needed ? */			! \
	addi	2, r25, r25						! \
	extru	r17, 24, 25, r17	/* tlbbtop(r17) */		! \
	sync

	.align	32
LEAF_ENTRY($tlbd_l)
	TLB_STATS_PRE(tlbd)
	TLB_PULL_L(1, TLABEL(all))
	IDTLBAF(17)
	IDTLBPF(25)
#ifdef USE_HPT
	/* invalidate instead of update */
	mfctl	cr28, r17
	ldw	0(r17), r24
	VTAG
	sub,<>	r16, r24, r0
	stw	r0, 0(r17)
#endif
	TLB_STATS_AFT(tlbd)
	rfir
	nop
EXIT($tlbd_l)

	/*
	 * from 7100lc ers, pg.6:
	 * we found a post-silicon bug that makes cr28
	 * unreliable for the itlb miss handler
	 */
LEAF_ENTRY($itlb_l)
	TLB_STATS_PRE(itlb)
	TLB_PULL_L(0, TLABEL(all))
	extru,=	r25, 5, 1, r0	/* gateway page needs to be public */
	depi	0, 30, 15, r25
	IITLBAF(17)
	IITLBPF(25)
	TLB_STATS_AFT(itlb)
	rfir
	nop
EXIT($itlb_l)

LEAF_ENTRY($dtlbna_l)
ALTENTRY($itlbna_l)
	TLB_STATS_PRE(dtlb)
	TLB_PULL_L(0, $dtlbna_l_fake)
	IDTLBAF(17)
	IDTLBPF(25)
	TLB_STATS_AFT(dtlb)
	rfir
	nop
$dtlbna_l_fake
	/* parse prober/w insns, have to decent to trap() to set regs proper */
	mfctl	iir, r16
	extru	r16, 6, 6, r24
	comib,=,n 1, r24, TLABEL(all)
	extru	r16, 24, 6, r24
	subi,<>	0x23, r24, r0
	b	TLABEL(all)
	/* otherwise generate a flush-only tlb entry */
	copy	r0, r17
	zdep	r8, 30, 15, r25
	depi	-13, 11, 7, r25
	ldo	2(r25), r25   /* 3? */
	IDTLBAF(17)
	IDTLBPF(25)
	TLB_STATS_AFT(dtlb)
	rfir
	nop
EXIT($dtlbna_l)

LEAF_ENTRY($dtlb_l)
	TLB_STATS_PRE(dtlb)
	TLB_PULL_L(0, TLABEL(all))
	IDTLBAF(17)
	IDTLBPF(25)
#ifdef USE_HPT
	/*
	 * cache the next page mapping in the hpt.
	 *
	 * mapping for a page at the end of each 128k is uncachable
	 * in the hvt since it'd be in the tlb itself and thus there
	 * is no reason to cache it!
	 * as a side effect this avoids recomputing hpt entry and
	 * retraversing the whole page table each time.
	 */

	ldo	PAGE_SIZE(r9), r9
	extru,<> r9, 20, 5, r0
	b,n	$dtlb_done_l	/* skip if no simple advance */
	/* do not check the PT overlap since the above
	 * check already guaranties that */

	/* ripped from TLB_PULL_L(0) */
	extru	r9, 19, 10, r16		/* r24 was loaded in the TLB_PULL_L */
	ldwx,s	r16(r24), r17		/* va -> pa:prot */
	sh2addl	r16, r24, r25
	combt,=,n r0, r17, $dtlb_done_l
	copy	r17, r16
	depi	0, 21, 1, r17
	sub,=	r16, r17, r0		/* do not store if unchanged */
	stws	r17, 0(r25)		/* store back w/ the bits */
	shd	r17, r0, 13, r25
	dep	r8, 30, 15, r25		/* mix0r the pid from the sid */
	dep	r0, 31, 12, r17		/* needed ? */
	addi	2, r25, r25
	extru	r17, 24, 25, r17
	sync

	mfctl	cr28, r24
	VTAG
	ldo	16(r24), r24
	stw	r16, 0(r24)
	stw	r25, 4(r24)
	stw	r17, 8(r24)
$dtlb_done_l
#endif
	TLB_STATS_AFT(dtlb)
	rfir
	nop
EXIT($dtlb_l)
#endif /* HP7100LC_CPU */

#if defined(HP8000_CPU) || defined(HP8200_CPU) || defined(HP8500_CPU)
	.level	2.0w

	/* xlate 32bit->64bit pte */
#define	TLB_PCX2PCXU \
	extrw,u	r25, 14, 13, r16		! \
	depdi	0, 31, 32, r17			! \
		/* fix io mappings */		! \
	extrd,s	r17, 42, 4, r1			! \
	addi,<>	1, r1, r0			! \
	depdi	-1, 38, 32, r17			! \
		/* fix prom mappings */		! \
	extrd,s	r17, 46, 8, r1			! \
	addi,<>	0x10, r1, r0			! \
	depdi	0, 38, 4, r17			! \
		/* weak ordering, dyn bp */	! \
	depwi	1, 31, 2, r16			! \
	depdi	0, 44, 30, r25			! \
	depd	r16, 14, 15, r25

LEAF_ENTRY($tlbd_u)
	TLB_STATS_PRE(tlbd)
	TLB_PULL_L(1, TLABEL(all))
	TLB_PCX2PCXU
	idtlbt	r17, r25
	TLB_STATS_AFT(tlbd)
	rfir
	nop
EXIT($tlbd_u)

LEAF_ENTRY($itlb_u)
	TLB_STATS_PRE(itlb)
	TLB_PULL_L(0, TLABEL(all))
	extru,=	r25, 5, 1, r0	/* gateway page needs to be public */
	depi	0, 30, 15, r25
	TLB_PCX2PCXU
	iitlbt	r17, r25
	TLB_STATS_AFT(itlb)
	rfir
	nop
EXIT($itlb_u)

LEAF_ENTRY($dtlbna_u)
ALTENTRY($itlbna_u)
	TLB_STATS_PRE(dtlb)
	TLB_PULL_L(0, $dtlbna_u_fake)
	TLB_PCX2PCXU
	idtlbt	r17, r25
	TLB_STATS_AFT(dtlb)
	rfir
	nop
$dtlbna_u_fake
	/* parse prober/w insns, have to decent to trap() to set regs proper */
	mfctl	iir, r16
	extru	r16, 6, 6, r24
	comib,=,n 1, r24, TLABEL(all)
	extru	r16, 24, 6, r24
	subi,<>	0x23, r24, r0
	b	TLABEL(all)
	/* otherwise generate a flush-only tlb entry */
	copy	r0, r17
	zdep	r8, 30, 15, r25
	depi	-13, 11, 7, r25
	ldo	2(r25), r25   /* 3? */
	idtlbt	r17, r25
	TLB_STATS_AFT(dtlb)
	rfir
	nop
EXIT($dtlbna_u)

LEAF_ENTRY($dtlb_u)
	TLB_STATS_PRE(dtlb)
	TLB_PULL_L(0, TLABEL(all))
	TLB_PCX2PCXU
	idtlbt	r17, r25
	TLB_STATS_AFT(dtlb)
	rfir
	nop
EXIT($dtlb_u)

	.level	1.1
#endif /* HP8000_CPU */

	.align	64
	.export	TLABEL(all), entry
ENTRY(TLABEL(all),0)
	/* r1 still has trap type */

	/*
	 * at this point we have:
	 *	psw copied into ipsw
	 *	psw = E(default), M(1 if HPMC, else 0)
	 *	PL = 0
	 *	r1, r8, r9, r16, r17, r24, r25 shadowed (maybe)
	 *	trap number in r1 (old r1 is saved in tr7)
	 */

	/* do not overwrite cr28 */
	mtctl	t3, cr26

	mfctl	cr29, t3
	ldo	CI_TRAP_SAVE(t3), t3
	stw	t1, TF_R22(t3)		/* use ,bc */
	stw	t2, TF_R21(t3)

	mfctl	cr26, t1
	stw	sp, TF_R30(t3)	/* sp */
	stw	t1, TF_R20(t3)	/* t3 */

	/*
	 * Now, save away other volatile state that prevents us from turning
	 * the PC queue back on, namely, the pc queue and ipsw, and the
	 * interrupt information.
	 */

	mfctl	eiem, t1
	mfctl	ipsw, t2
	stw	t1, TF_CR15(t3)		/* use ,bc */
	stw	t2, TF_CR22(t3)

	mfsp	sr3, t1
	mfctl	pidr1, t2
	stw	t1, TF_SR3(t3)
	stw	t2, TF_CR8(t3)

	/*
	 * Setup kernel context
	 */

	ldi	HPPA_PID_KERNEL,t1
	mtctl	t1, pidr1
	mtsp	r0, sr3

	/* this will enable interrupts after `cold' */
	mfctl	cr29, t1
	ldw	CI_PSW(t1), t2
	mtctl	r0, eiem
	mtctl	t2, ipsw

	mfctl	pcsq, t1
	mtctl	r0, pcsq
	mfctl	pcsq, t2
	stw	t1, TF_IISQH(t3)	/* use ,bc */
	stw	t2, TF_IISQT(t3)
	mtctl	r0, pcsq

	/*
	 * Set up the kernel stack pointer.  If the trap happened
	 * while we were in unprivileged code, or in privileged
	 * code in the SYSCALLGATE page, move to the kernel stack
	 * in curproc's PCB; otherwise, start a new stack frame
	 * on whatever kernel stack we're already on.
	 *
	 * This used to check only for a trap while we were in
	 * unprivileged code, but this ignored the possibility
	 * that a trap could come in during the period between
	 * a gateway instruction to raise privilege and the
	 * disabling of interrupts.  During this period we're
	 * still on the user's stack, and we must move to the
	 * kernel stack.
	 *
	 * - fredette@
	 */
	mfctl	pcoq, t1
	ldil	L%SYSCALLGATE, t2
	ldo	TF_PHYS-1(sp), sp
	dep	t1, 31, PGSHIFT, t2
	dep,<>	t1, 31, 2, r0
	comb,<>	t1, t2, $trap_from_kernel
	dep	r0, 31, 6, sp

	mfctl	cr29, t2
	ldw	CI_CURPROC(t2), t2
	depi	1, T_USER_POS, 1, r1
	depi	1, TFF_LAST_POS, 1, r1
	ldw	P_ADDR(t2), sp
	ldo	NBPG(sp), sp

$trap_from_kernel
	ldil	L%$trapnowvirt, t2
	ldo	R%$trapnowvirt(t2), t2
	mtctl	t2, pcoq
	stw	t1, TF_IIOQH(t3)
	ldo	4(t2), t2
	mfctl	pcoq, t1
	stw	t1, TF_IIOQT(t3)
	mtctl	t2, pcoq

	mfctl	isr, t1
	mfctl	ior, t2
	stw	t1, TF_CR20(t3)		/* use ,bc */
	stw	t2, TF_CR21(t3)

	mfctl	iir, t2
	stw	t2, TF_CR19(t3)
	stw	r1, TF_FLAGS(t3)

	mfctl	rctr, t1		/* gotta get it before R is up */

	copy	sp, t3
	ldo	HPPA_FRAME_SIZE+TRAPFRAME_SIZEOF(sp), sp
	rfir
	nop ! nop ! nop ! nop ! nop ! nop ! nop ! nop
$trapnowvirt
	/*
	 * t3 contains the virtual address of the trapframe
	 * sp is loaded w/ the right VA (we did not need it being physical)
	 */

	mfctl	ccr, t2
	stw	t1, TF_CR0(sr3, t3)
	stw	t2, TF_CR10(sr3, t3)

	mfsp	sr0, t1
	mfsp	sr1, t2
	stw	t1, TF_SR0(sr3, t3)
	stw	t2, TF_SR1(sr3, t3)

	mfsp	sr2, t1
	mfsp	sr4, t2
	stw	t1, TF_SR2(sr3, t3)
	stw	t2, TF_SR4(sr3, t3)

	mfsp	sr5, t2
	mfsp	sr6, t1
	stw	t2, TF_SR5(sr3, t3)
	stw	t1, TF_SR6(sr3, t3)

	mfsp	sr7, t1
	mfctl	pidr2, t2
	stw	t1, TF_SR7(sr3, t3)
	stw	t2, TF_CR9(sr3, t3)

	mtsp	r0, sr0
	mtsp	r0, sr1
	mtsp	r0, sr2
	mtsp	r0, sr4
	mtsp	r0, sr5
	mtsp	r0, sr6
	mtsp	r0, sr7

#if pbably_not_worth_it
	mfctl	pidr3, t1
	mfctl	pidr4, t2
	stw	t1, TF_CR12(t3)
	stw	t2, TF_CR13(t3)
#endif

	/*
	 * Save all general registers that we haven't saved already
	 */

	mfctl	sar, t1			/* use ,bc each cache line */
	stw	t1, TF_CR11(t3)
	stw	r1, TF_R1(t3)
	stw	r2, TF_R2(t3)
	stw	r3, TF_R3(t3)

	copy	sp, r3
	stw,mb	r0, -HPPA_FRAME_SIZE(r3)

	/*
	 * Copy partially saved state from the store into the frame
	 */
	mfctl	cr29, t2
	ldo	CI_TRAP_SAVE(t2), t2
	/* use ,bc each cache line */
	ldw  0(t2), r1 ! ldw  4(t2), t1 ! stw r1,  0(t3) ! stw t1,  4(t3)
	ldw  8(t2), r1 ! ldw 12(t2), t1 ! stw r1,  8(t3) ! stw t1, 12(t3)
	ldw 16(t2), r1 ! ldw 20(t2), t1 ! stw r1, 16(t3) ! stw t1, 20(t3)
	ldw 24(t2), r1 ! ldw 28(t2), t1 ! stw r1, 24(t3) ! stw t1, 28(t3)
	ldw 32(t2), r1 ! ldw 36(t2), t1 ! stw r1, 32(t3) ! stw t1, 36(t3)
	ldw 40(t2), r1 ! ldw 44(t2), t1 ! stw r1, 40(t3) ! stw t1, 44(t3)
	ldw 48(t2), r1 ! ldw 52(t2), t1 ! stw r1, 48(t3) ! stw t1, 52(t3)
	ldw 56(t2), r1 ! ldw 60(t2), t1 ! stw r1, 56(t3) ! stw t1, 60(t3)

	stw	r4, TF_R4(t3)
	stw	r5, TF_R5(t3)
	stw	r6, TF_R6(t3)
	stw	r7, TF_R7(t3)
	stw	r8, TF_R8(t3)
	stw	r9, TF_R9(t3)
	stw	r10, TF_R10(t3)
	stw	r11, TF_R11(t3)
	stw	r12, TF_R12(t3)
	stw	r13, TF_R13(t3)
	stw	r14, TF_R14(t3)
	stw	r15, TF_R15(t3)
	stw	r16, TF_R16(t3)
	stw	r17, TF_R17(t3)
	stw	r18, TF_R18(t3)
	stw	r19, TF_R19(t3)	/* t4 */
	stw	r23, TF_R23(t3)
	stw	r24, TF_R24(t3)
	stw	r25, TF_R25(t3)
	stw	r26, TF_R26(t3)
	stw	r27, TF_R27(t3)
	stw	r28, TF_R28(t3)
	stw	r29, TF_R29(t3)
	stw	r31, TF_R31(t3)

	/*
	 * Save the necessary control registers that have not already saved.
	 */

#ifdef DDB
	/*
	 * Save hpt mask and v2p translation table pointer
	 */
	mfctl	eirr, t1
	mfctl	vtop, t2
	stw	t1, TF_CR23(t3)
	stw	t2, TF_CR25(t3)

	mfctl	cr28, t2
	stw	t2, TF_CR28(t3)
#endif
	mfctl	cr27, t1
	mfctl	cr30, t2
	stw	t1, TF_CR27(t3)
	stw	t2, TF_CR30(t3)

	/*
	 * load the global pointer for the kernel
	 */

	ldil	L%$global$, dp
	ldo	R%$global$(dp), dp

	/*
	 * call the C routine trap().
	 * form trap type in the first argument to trap()
	 */
	ldw	TF_FLAGS(t3), arg0
	dep	r0, 24, 25, arg0
	copy	t3, arg1

	copy	arg0, r4
	copy	arg1, r5

	.import	trap, code
	ldil	L%trap,t1
	ldo	R%trap(t1),t1
	.call
	blr	r0,rp
	bv,n	r0(t1)
	nop

	copy	r5, t3

	/* see if curproc have changed */
	extru,<> r4, TFF_LAST_POS, 1, r0
	b	$syscall_return

	/* see if curproc have really changed */
	mfctl	cr29, t1
	ldw	CI_CURPROC(t1), t2
	sub,<>	r0, t2, r0
	ldw	P_MD_REGS(t2), t3

	/* means curproc has actually changed */
	b	$syscall_return
	nop
EXIT(TLABEL(all))

#if defined(HP7000_CPU)
/*
 * void desidhash_s(void)
 */
LEAF_ENTRY(desidhash_s)
	sync
	MFCPU_C_PCXST(DR_CPUCFG,22)	/* t1 */
	MFCPU_C_PCXST(DR_CPUCFG,22)
	nop
	nop
	depi	0, DR0_PCXS_DHE, 3, t1	/* 3: DR0_PCXS_DOMAIN|DR0_PCXS_IHE */
	depi	1, DR0_PCXS_EQWSTO, 1, t1
	/* clear `write to clear' bits so they don't get reset */
	depi	0, DR0_PCXS_DHPMC, 1, t1
	depi	0, DR0_PCXS_ILPMC, 1, t1
	sync
	MTCPU_PCXST(22,DR_CPUCFG)
	MTCPU_PCXST(22,DR_CPUCFG)
	nop
	nop
	bv	0(rp)
	extru	t1, 4, 5, ret0	/* return chip revision */
EXIT(desidhash_s)
#endif /* HP7000_CPU */

#if defined(HP7100_CPU) || defined(HP7200_CPU)
/*
 * void desidhash_t(void)
 */
LEAF_ENTRY(desidhash_t)
	sync
	MFCPU_C_PCXST(DR_CPUCFG,22)	/* t1 */
	MFCPU_C_PCXST(DR_CPUCFG,22)
	nop
	nop
	depi	0, DR0_PCXT_IHE, 1, t1
	depi	0, DR0_PCXT_DHE, 1, t1
	/* clear `write to clear' bits so they don't get reset */
	depi	0, DR0_PCXT_DHPMC, 1, t1
	depi	0, DR0_PCXT_ILPMC, 1, t1
	sync
	MTCPU_PCXST(22,DR_CPUCFG)
	MTCPU_PCXST(22,DR_CPUCFG)
	nop
	nop
	bv	0(rp)
	extru	t1, 4, 5, ret0	/* return chip revision */
EXIT(desidhash_t)
#endif /* HP7100_CPU || HP7200_CPU */

#ifdef HP7300LC_CPU
	.data
	BSS(eaio_l2_mask, 4)
LEAF_ENTRY(eaio_l2)
	ldil	L%eaio_l2_mask, t2
	ldw	R%eaio_l2_mask(t2), t1
	or	t1, arg0, t1
	MTCPU_PCXL(22, DR0_PCXL2_ACCEL_IO)
	nop
	nop
	bv	0(rp)
	stw	t1, R%eaio_l2_mask(t2)
EXIT(eaio_l2)
#endif /* HP7300LC_CPU */

#if defined(HP7100LC_CPU) || defined(HP7300LC_CPU)

/*
 * int
 * ibtlb_l(int i, pa_space_t sp, vaddr_t va, paddr_t pa, vsize_t sz, u_int prot)
 */
LEAF_ENTRY(ibtlb_l)
	rsm	(PSL_R|PSL_I), t4
	nop ! nop ! nop ! nop ! nop ! nop ! nop

	bv	0(rp)
	mtsm	t4
EXIT(ibtlb_l)

/*
 * int desidhash_l(void)
 */
LEAF_ENTRY(desidhash_l)
	MFCPU_C_PCXL(DR_CPUCFG,22)	/* t1 */
	nop
	nop
	depi	0, DR0_PCXL_L2IHASH_EN, 2, t1	/* 2: DR0_PCXL_L2DHASH_EN */
#if 0 /* better trust the PROM if it left some bits set here */
	depi	0, DR0_PCXL_DUAL_DIS, 2, t1
#endif
	/* clear `write to clear' bits so they don't get reset */
	depi	0, DR0_PCXL_L2IHPMC, 1, t1
	depi	0, DR0_PCXL_L2DHPMC, 1, t1
	depi	0, DR0_PCXL_L1IHPMC, 1, t1
	depi	0, DR0_PCXL_L2PARERR, 4, t1
	sync
	MTCPU_PCXL(22,DR_CPUCFG)
	nop
	nop
	bv	0(rp)
	extru	t1, 4, 5, ret0	/* return chip revision */
EXIT(desidhash_l)

#endif /* HP7100LC_CPU */

#if defined(HP8000_CPU) || defined(HP8200_CPU) || defined(HP8500_CPU)
	.level	2.0w
LEAF_ENTRY(desidhash_u)
	MFCPU_PCXU(2,28)
	depdi	0, 54, 1, r28
	MTCPU_PCXU(28,2)
	bv	r0(rp)
	copy	r0, ret0	/* XXX dunno how to get chip rev */
EXIT(desidhash_u)

LEAF_ENTRY(ibtlb_u)
	/* TODO insert a locked large tlb entry */
	bv	0(rp)
	nop
EXIT(ibtlb_u)

LEAF_ENTRY(pbtlb_u)
	/* TODO purge a locked tlb entry */
	bv	0(rp)
	nop
EXIT(pbtlb_u)
	.level	1.1
#endif /* HP8000_CPU */

/*
 * High Priority Machine Check Interrupt
 */
	.export	TLABEL(hpmc), entry
ENTRY(TLABEL(hpmc),0)
ALTENTRY(hpmc_tramp)

	mtsp	r0, sr0
	ldil	L%hppa_vtop, t1
	ldw	R%hppa_vtop(t1), t1
	mtctl	t1, CR_VTOP

	.import	hpmc_dump, code
	ldil	L%hpmc_dump, rp
	ldo	R%hpmc_dump(rp), rp
	mfctl	cr29, %arg2
	ldw	CI_PSW(%arg2), %arg2
	depi	0, PSL_I_POS, 1, %arg2
	stw	%arg2, CI_PSW(t1)
	ldil	L%emrg_stack, arg1
	b	$kernel_setup
	ldw	R%emrg_stack(arg1), arg1

	/* never returns, but still */
	ldil	L%HPPA_GBCAST, t1
	ldi	CMD_RESET, t2
	stw	t2, R%HPPA_GBCAST(t1)
hpmc_never_dies
	b	hpmc_never_dies
	nop
ALTENTRY(hpmc_tramp_end)
EXIT(TLABEL(hpmc))

/*
 * transfer of control handler
 */
ENTRY(hppa_toc,0)

	mtsp	r0, sr0
	ldil	L%hppa_vtop, t1
	ldw	R%hppa_vtop(t1), t1
	mtctl	t1, CR_VTOP

	/* TODO reload btlb */

	.import	boot, code
	ldil	L%boot, rp
	ldo	R%boot(rp), rp
	mfctl	cr29, %arg2
	ldw	CI_PSW(%arg2), %arg2
	depi	0, PSL_I_POS, 1, %arg2
	stw	%arg2, CI_PSW(t1)
	ldi	0, arg0
	ldil	L%emrg_stack, arg1
	b	$kernel_setup
	ldw	R%emrg_stack(arg1), arg1

ALTENTRY(hppa_toc_end)
	.word	0
EXIT(hppa_toc)

/*
 * power fail recovery handler
 */
ENTRY(hppa_pfr,0)

	mtsp	r0, sr0
	ldil	L%hppa_vtop, t1
	ldw	R%hppa_vtop(t1), t1
	mtctl	t1, CR_VTOP

	/* TODO reload btlb */

	.import	boot, code
	ldil	L%boot, rp
	ldo	R%boot(rp), rp
	mfctl	cr29, %arg2
	ldw	CI_PSW(%arg2), %arg2
	depi	0, PSL_I_POS, 1, %arg2
	stw	%arg2, CI_PSW(t1)
	ldi	RB_HALT|RB_POWERDOWN, arg0
	ldil	L%emrg_stack, arg1
	b	$kernel_setup
	ldw	R%emrg_stack(arg1), arg1

ALTENTRY(hppa_pfr_end)
	.word	0
EXIT(hppa_pfr)

#if 0
	.align	8
intr_ticks
	.word	0, 0

#define	INTR_PROF_PRE \
	mfctl	itmr, r9		! \
	mtctl	r9, tr5
#define	INTR_PROF_AFT \
	mfctl	itmr, r8		! \
	mfctl	tr5, r9			! \
	ldil	L%intr_ticks, r1	! \
	ldo	R%intr_ticks(r1), r1	! \
	sub	r8, r9, r8		! \
	ldw	0(r1), r16		! \
	ldw	4(r1), r17		! \
	add	r8, r16, r16		! \
	addi	1, r17, r17		! \
	stw	r16, 0(r1)		! \
	stw	r17, 4(r1)
#else
#define	INTR_PROF_PRE	/* */
#define	INTR_PROF_AFT	/* */
#endif

	.import	imask, data
	.import	intr_table, data
	.align	32
ENTRY(TLABEL(intr),0)
	/*
	 * r8 is set to eirr in the INTRPRE
	 */

	INTR_PROF_PRE

	ldil	L%intr_table + 32*32, r1
	ldo	R%intr_table + 32*32(r1), r1
	mfctl	cr29, r17
	b	$intr_cont
	ldw	CI_IPENDING(r17), r24

$intr_ffs
	addi	-32, r1, r1
	bb,>=	r8, 0, $intr_ffs
	zdep	r8, 30, 31, r8

	ldw	0(r1), r17
	bb,>=,n	r17, 23, $intr_nocall

	ldw	2*4(r1), r16	/* func */
	ldw	3*4(r1), r9	/* arg: ioreg */
	mtctl	r1, tr7
	bv	r0(r16)
	ldw	6*4(r1), r1	/* next: sub-intr_table */

$intr_nocall
	ldw	4*4(r1), r17	/* bit */
	or	r17, r24, r24	/* ipending */

	/* also return from nested handlers */
$intr_cont
	comb,<>,n r0, r8, $intr_ffs
	ldw	-32(r1), r0	/* preload cache */

	mfctl	cr29, r17
	stw	r24, CI_IPENDING(r17)
	ldw	CI_CPL(r17), r17
	ldil	L%imask, r16
	ldo	R%imask(r16), r16
	ldwx,s	r17(r16), r25

	INTR_PROF_AFT

	ldi	T_INTERRUPT, r1
	andcm,=	r24, r25, r0
	b	TLABEL(all)
	nop

	rfir
	nop
EXIT(TLABEL(intr))

/*
 * called with:
 *	r1	sub intr_table
 *	r9	ioregs
 *	r24	ipending (in/out)
 *	tr7	saved r1 (restore on return)
 * free:
 *	r9, r16, r17, r25
 */
	.align	32
LEAF_ENTRY(gsc_intr)
	ldw	0(r9), r16	/* irr */

	/* we know that first 5 bits are never used ... should skip */
$gsc_intr_loop
	comb,=,n r0, r16, $intr_cont
	mfctl	tr7, r1
$gsc_ffs
	addi	32, r1, r1
	bb,>=	r16, 31, $gsc_ffs
	shd	r0, r16, 1, r16

	ldo	-32(r1), r9
$gsc_share
	ldw	4*4(r9), r17	/* bit */
	ldw	5*4(r9), r9	/* share */

	comb,<>	r0, r9, $gsc_share
	or	r17, r24, r24	/* ipending */

	b,n	$gsc_intr_loop
EXIT(gsc_intr)

	/* see above for calling conventions */
	.align	32
LEAF_ENTRY(dino_intr)
	ldw	3*4(r9), r16	/* irr0 */

$dino_intr_loop
	comb,=,n r0, r16, $intr_cont
	mfctl	tr7, r1
$dino_ffs
	addi	32, r1, r1
	bb,>=	r16, 31, $dino_ffs
	shd	r0, r16, 1, r16

	ldo	-32(r1), r9
$dino_share
	ldw	4*4(r9), r17	/* bit */
	ldw	5*4(r9), r9	/* share */

	comb,<>	r0, r9, $dino_share
	or	r17, r24, r24	/* ipending */

	b,n	$dino_intr_loop
EXIT(dino_intr)

	.export	TLABEL(ibrk), entry
ENTRY(TLABEL(ibrk),0)
	/* If called by a user process then always pass it to trap() */
	mfctl	pcoq, r8
	extru,=	r8, 31, 2, r0
	b,n	$ibrk_bad

	/* don't accept breaks from data segments */
	.import etext
	ldil	L%etext, r9
	ldo	R%etext(r9), r9
	comb,>>=,n r8, r9, $ibrk_bad

	mfctl	iir, r8
	extru	r8, 31, 5, r9
	comib,<>,n HPPA_BREAK_KERNEL, r9, $ibrk_bad

	/* now process all those `break' calls we make */
	extru	r8, 18, 13, r9
	comib,=,n HPPA_BREAK_GET_PSW, r9, $ibrk_getpsw
	comib,=,n HPPA_BREAK_SET_PSW, r9, $ibrk_setpsw
	comib,=,n HPPA_BREAK_SPLLOWER, r9, $ibrk_spllower

$ibrk_bad
	/* illegal (unimplemented) break entry point */
	b	TLABEL(all)
	nop

$ibrk_getpsw
	b	$ibrk_exit
	mfctl	ipsw, ret0

$ibrk_setpsw
	mfctl	ipsw, ret0
	b	$ibrk_exit
	mtctl	arg0, ipsw

$ibrk_spllower
	/* skip the break */
	mtctl	r0, pcoq
	mfctl	pcoq, r9
	mtctl	r9, pcoq
	ldo	4(r9), r9
	mtctl	r9, pcoq

	mfctl	cr29, r17
	ldw	CI_IPENDING(r17), r8
	ldil	L%imask, r9
	ldo	R%imask(r9), r9
	ldw	CI_CPL(r17), ret0
	ldwx,s	arg0(r9), r16
	stw	arg0, CI_CPL(r17)
	ldi	T_INTERRUPT, r1
	andcm,=	r8, r16, r0
	b	TLABEL(all)
	nop
	rfir
	nop

	/* insert other fast breaks here */
	nop ! nop

$ibrk_exit
	/* skip the break */
	mtctl	r0, pcoq
	mfctl	pcoq, r9
	mtctl	r9, pcoq
	ldo	4(r9), r9
	mtctl	r9, pcoq

	rfir
	nop
EXIT(TLABEL(ibrk))

LEAF_ENTRY(fpu_exit)
	/* enable coprocessor XXX */
	depi	3, 25, 2, r1
	mtctl	r1, ccr

	ldil	L%fpu_scratch, %r25
	ldo	R%fpu_scratch(%r25), %r25
	fstds	%fr0, 0(%r25)
	sync
	bv	%r0(%rp)
	mtctl	r0, ccr
EXIT(fpu_exit)

LEAF_ENTRY(fpu_save)
	fstds,ma %fr0 , 8(arg0)
	fstds,ma %fr1 , 8(arg0)
	fstds,ma %fr2 , 8(arg0)
	fstds,ma %fr3 , 8(arg0)
	fstds,ma %fr4 , 8(arg0)
	fstds,ma %fr5 , 8(arg0)
	fstds,ma %fr6 , 8(arg0)
	fstds,ma %fr7 , 8(arg0)
	fstds,ma %fr8 , 8(arg0)
	fstds,ma %fr9 , 8(arg0)
	fstds,ma %fr10, 8(arg0)
	fstds,ma %fr11, 8(arg0)
	fstds,ma %fr12, 8(arg0)
	fstds,ma %fr13, 8(arg0)
	fstds,ma %fr14, 8(arg0)
	fstds,ma %fr15, 8(arg0)
	fstds,ma %fr16, 8(arg0)
	fstds,ma %fr17, 8(arg0)
	fstds,ma %fr18, 8(arg0)
	fstds,ma %fr19, 8(arg0)
	fstds,ma %fr20, 8(arg0)
	fstds,ma %fr21, 8(arg0)
	fstds,ma %fr22, 8(arg0)
	fstds,ma %fr23, 8(arg0)
	fstds,ma %fr24, 8(arg0)
	fstds,ma %fr25, 8(arg0)
	fstds,ma %fr26, 8(arg0)
	fstds,ma %fr27, 8(arg0)
	fstds,ma %fr28, 8(arg0)
	fstds,ma %fr29, 8(arg0)
	fstds,ma %fr30, 8(arg0)
	fstds    %fr31, 0(arg0)
	bv	r0(rp)
	sync
EXIT(fpu_save)

#ifdef FPEMUL
	/*
	 * Emulate FPU
	 *
	 * iisq:iioq - exception triggered instruction
	 */
ENTRY($fpu_emulate,320)
	copy	r31, r9

	mfctl	cr29, r31
	ldw	CI_STACK(r31), r31

	/* stw	r1 , TF_R1 (r31) shadowed */
	stw	r2 , TF_R2 (r31)
	stw	r3 , TF_R3 (r31)
	stw	r4 , TF_R4 (r31)
	stw	r5 , TF_R5 (r31)
	stw	r6 , TF_R6 (r31)
	stw	r7 , TF_R7 (r31)
	/* stw	r8 , TF_R8 (r31) shadowed */
	/* stw	r9 , TF_R9 (r31) shadowed */
	stw	r10, TF_R10(r31)
	stw	r11, TF_R11(r31)
	stw	r12, TF_R12(r31)
	stw	r13, TF_R13(r31)
	stw	r14, TF_R14(r31)
	stw	r15, TF_R15(r31)
	/* stw	r16, TF_R16(r31) shadowed */
	/* stw	r17, TF_R17(r31) shadowed */
	stw	r18, TF_R18(r31)
	stw	r19, TF_R19(r31)
	stw	r20, TF_R20(r31)
	stw	r21, TF_R21(r31)
	stw	r22, TF_R22(r31)
	stw	r23, TF_R23(r31)
	/* stw	r24, TF_R24(r31) shadowed */
	/* stw	r25, TF_R25(r31) shadowed */
	stw	r26, TF_R26(r31)
	stw	r27, TF_R27(r31)
	stw	r28, TF_R28(r31)
	stw	r29, TF_R29(r31)
	stw	sp, TF_R30(r31)
	stw	r9, TF_R31(r31)
	copy	r1, arg0
	mfctl	sar, r1
	stw	r1, TF_CR11(r31)
	stw	arg0, TF_CR19(r31)

	ldo	TRAPFRAME_SIZEOF(r31), r3
	ldo	TRAPFRAME_SIZEOF+HPPA_FRAME_SIZE(r31), sp

	ldil	L%$global$, dp
	ldo	R%$global$(dp), dp

	.import	fpu_emulate, code
	ldil	L%fpu_emulate,t1
	ldo	R%fpu_emulate(t1),t1
	mfctl	cr30, arg2
	.call
	blr	r0,rp
	bv,n	0(t1)
	nop

	mfctl	cr30, r25
	ldi	32, r1

	ldw	4(r25), r17	/* fpu exception reg 0 */
	zdep	ret0, 5, 6, r17	/* intentionally zero the insn */
	stw	r17, 4(r25)

	mfctl	cr29, r31
	ldw	CI_STACK(r31), r31

	ldw	TF_CR11(r31), r1
	ldw	TF_R2 (r31), r2
	ldw	TF_R3 (r31), r3
	mtsar	r1
	copy	ret0, r1
	ldw	TF_R4 (r31), r4
	ldw	TF_R5 (r31), r5
	ldw	TF_R6 (r31), r6
	ldw	TF_R7 (r31), r7
	/* ldw	TF_R8 (r31), r8 shadowed */
	/* ldw	TF_R9 (r31), r9 shadowed */
	ldw	TF_R10(r31), r10
	ldw	TF_R11(r31), r11
	ldw	TF_R12(r31), r12
	ldw	TF_R13(r31), r13
	ldw	TF_R14(r31), r14
	ldw	TF_R15(r31), r15
	/* ldw	TF_R16(r31), r16 shadowed */
	/* ldw	TF_R17(r31), r17 shadowed */
	ldw	TF_R18(r31), r18
	ldw	TF_R19(r31), r19
	ldw	TF_R20(r31), r20
	ldw	TF_R21(r31), r21
	ldw	TF_R22(r31), r22
	ldw	TF_R23(r31), r23
	/* ldw	TF_R24(r31), r24 shadowed */
	/* ldw	TF_R25(r31), r25 shadowed */
	ldw	TF_R26(r31), r26
	ldw	TF_R27(r31), r27
	ldw	TF_R28(r31), r28
	ldw	TF_R29(r31), r29
	ldw	TF_R30(r31), r30
	ldw	TF_R31(r31), r31

	bb,>=,n	r1, 24, $fpu_emulate_done

	b	TLABEL(all)
	ldi	T_EMULATION, r1

$fpu_emulate_done
	comb,<>	r0, r1, TLABEL(all)
	ldi	T_EXCEPTION, r1

	rfir
	nop
EXIT($fpu_emulate)

#endif /* FPEMUL */

	.import	dcache_stride, data
LEAF_ENTRY(fdcache)
	ldil	L%dcache_stride,t1
	ldw	R%dcache_stride(t1), arg3

	mtsp	arg0, sr1		/* move the space register to sr1 */
	add	arg1, arg2, arg0	/* get the last byte to flush in arg0 */

	zdep	arg3, 27, 28, t1	/* get size of a 16X loop in t1 */
	comb,<	arg2, t1, fdc_short	/* check for count < 16 * stride */
	addi	-1, t1, t1		/* compute size of large loop - 1 */

	andcm	arg2, t1, t1		/* L = count - (count mod lenbigloop) */
	add	arg1, t1, t1		/* ub for big loop is lb + L */

	fdc,m	arg3(sr1, arg1)		/* Start flushing first cache line. */
fdc_long
	fdc,m	arg3(sr1, arg1)
	fdc,m	arg3(sr1, arg1)
	fdc,m	arg3(sr1, arg1)
	fdc,m	arg3(sr1, arg1)
	fdc,m	arg3(sr1, arg1)
	fdc,m	arg3(sr1, arg1)
	fdc,m	arg3(sr1, arg1)
	fdc,m	arg3(sr1, arg1)
	fdc,m	arg3(sr1, arg1)
	fdc,m	arg3(sr1, arg1)
	fdc,m	arg3(sr1, arg1)
	fdc,m	arg3(sr1, arg1)
	fdc,m	arg3(sr1, arg1)
	fdc,m	arg3(sr1, arg1)
	fdc,m	arg3(sr1, arg1)
	comb,<<,n arg1, t1, fdc_long
	fdc,m	arg3(sr1, arg1)
fdc_short				/* flush one line at a time */
	comb,<<,n arg1, arg0, fdc_short
	fdc,m	arg3(sr1, arg1)

	addi	-1, arg0, arg1
	fdc	r0(sr1, arg1)

	sync
	syncdma
	bv	r0(r2)
	nop
EXIT(fdcache)

	.import	dcache_stride, data
LEAF_ENTRY(pdcache)
	ldil	L%dcache_stride,t1
	ldw	R%dcache_stride(t1), arg3

	mtsp	arg0, sr1		/* move the space register to sr1 */
	add	arg1, arg2, arg0	/* get the last byte to purge in arg0 */

	zdep	arg3, 27, 28, t1	/* get size of a 16X loop in t1 */
	comb,<	arg2, t1, pdc_short	/* check for count < 16 * stride */
	addi	-1, t1, t1		/* compute size of large loop - 1 */

	andcm	arg2, t1, t1		/* L = count - (count mod lenbigloop) */
	add	arg1, t1, t1		/* ub for big loop is lb + L */

	pdc,m	arg3(sr1, arg1)		/* Start purging first cache line. */
pdc_long
	pdc,m	arg3(sr1, arg1)
	pdc,m	arg3(sr1, arg1)
	pdc,m	arg3(sr1, arg1)
	pdc,m	arg3(sr1, arg1)
	pdc,m	arg3(sr1, arg1)
	pdc,m	arg3(sr1, arg1)
	pdc,m	arg3(sr1, arg1)
	pdc,m	arg3(sr1, arg1)
	pdc,m	arg3(sr1, arg1)
	pdc,m	arg3(sr1, arg1)
	pdc,m	arg3(sr1, arg1)
	pdc,m	arg3(sr1, arg1)
	pdc,m	arg3(sr1, arg1)
	pdc,m	arg3(sr1, arg1)
	pdc,m	arg3(sr1, arg1)
	comb,<<,n arg1, t1, pdc_long
	pdc,m	arg3(sr1, arg1)
pdc_short				/* purge one line at a time */
	comb,<<,n arg1, arg0, pdc_short
	pdc,m	arg3(sr1, arg1)

	addi	-1, arg0, arg1
	pdc	r0(sr1, arg1)

	sync
	syncdma
	bv	r0(r2)
	nop
EXIT(pdcache)

	.import	icache_stride, data
LEAF_ENTRY(ficache)
	ldil	L%icache_stride,t1
	ldw	R%icache_stride(t1), arg3

	mtsp	arg0, sr1		/* move the space register to sr1 */
	add	arg1, arg2, arg0	/* get the last byte to flush in arg0 */

	zdep	arg3, 27, 28, t1	/* get size of a 16X loop in t1 */
	comb,<	arg2, t1, fic_short	/* check for count < 16 * stride */
	addi	-1, t1, t1		/* compute size of large loop - 1 */

	andcm	arg2, t1, t1		/* L = count - (count mod lenbigloop) */
	add	arg1, t1, t1		/* ub for big loop is lb + L */

	fic,m	arg3(sr1, arg1)		/* Start flushing first cache line. */
fic_long
	fic,m	arg3(sr1, arg1)
	fic,m	arg3(sr1, arg1)
	fic,m	arg3(sr1, arg1)
	fic,m	arg3(sr1, arg1)
	fic,m	arg3(sr1, arg1)
	fic,m	arg3(sr1, arg1)
	fic,m	arg3(sr1, arg1)
	fic,m	arg3(sr1, arg1)
	fic,m	arg3(sr1, arg1)
	fic,m	arg3(sr1, arg1)
	fic,m	arg3(sr1, arg1)
	fic,m	arg3(sr1, arg1)
	fic,m	arg3(sr1, arg1)
	fic,m	arg3(sr1, arg1)
	fic,m	arg3(sr1, arg1)
	comb,<<,n arg1, t1, fic_long
	fic,m	arg3(sr1, arg1)
fic_short				/* flush one line at a time */
	comb,<<,n arg1, arg0, fic_short
	fic,m	arg3(sr1, arg1)

	addi	-1, arg0, arg1
	fic	r0(sr1, arg1)

	sync
	syncdma
	bv	r0(r2)
	nop
EXIT(ficache)

#ifdef DDB
LEAF_ENTRY(setjmp)
/*
 * Save the other general registers whose contents are expected to remain
 * across function calls.  According to the "HP 9000 Series 800 Assembly
 * Language Reference Manual", procedures can use general registers 19-26,
 * 28, 29, 1, and 31 without restoring them.  Hence, we do not save these.
 */
	stwm	r3,4(arg0)
	stwm	r4,4(arg0)
	stwm	r5,4(arg0)
	stwm	r6,4(arg0)
	stwm	r7,4(arg0)
	stwm	r8,4(arg0)
	stwm	r9,4(arg0)
	stwm	r10,4(arg0)
	stwm	r11,4(arg0)
	stwm	r12,4(arg0)
	stwm	r13,4(arg0)
	stwm	r14,4(arg0)
	stwm	r15,4(arg0)
	stwm	r16,4(arg0)
	stwm	r17,4(arg0)
	stwm	r18,4(arg0)
	stwm	r27,4(arg0)	/* Good idea to save the data pointer (dp) */
	stwm	rp,4(arg0)	/* Save the return pointer */
	stwm	sp,4(arg0)	/* Save the original stack pointer */

	bv	0(rp)
	copy	r0, ret0
EXIT(setjmp)

LEAF_ENTRY(longjmp)
/*
 * Restore general registers.
 */
	ldwm	4(arg0),r3
	ldwm	4(arg0),r4
	ldwm	4(arg0),r5
	ldwm	4(arg0),r6
	ldwm	4(arg0),r7
	ldwm	4(arg0),r8
	ldwm	4(arg0),r9
	ldwm	4(arg0),r10
	ldwm	4(arg0),r11
	ldwm	4(arg0),r12
	ldwm	4(arg0),r13
	ldwm	4(arg0),r14
	ldwm	4(arg0),r15
	ldwm	4(arg0),r16
	ldwm	4(arg0),r17
	ldwm	4(arg0),r18
	ldwm	4(arg0),r27
	ldwm	4(arg0),rp	/* Restore return address pointer, */
	ldwm	4(arg0),sp	/* stack pointer, */

	bv	0(rp)
	ldi	1, ret0
EXIT(longjmp)
#endif /* DDB */

	.align	32

LEAF_ENTRY(copy_on_fault)
	mtsp	r0, sr1
	mtsp	r0, sr2
	stw	r1, PCB_ONFAULT+U_PCB(r2)
	ldw	HPPA_FRAME_CRP(sp), rp
	ldo	-64(sp), sp
	bv	0(rp)
	ldi	EFAULT, %ret0
EXIT(copy_on_fault)

/*
 * int spstrcpy (pa_space_t ssp, const void *src, pa_space_t dsp, void *dst,
 *		 size_t size, size_t *rsize)
 * do a space to space strncpy, return actual copy size in the rsize;
 */
LEAF_ENTRY(spstrcpy)
	ldw	HPPA_FRAME_ARG(4)(sp), t2
	ldo	64(sp), sp
	add	t2, arg1, t2
	stw	rp, HPPA_FRAME_CRP(sp)
	/* setup fault handler */
	mfctl	cr29, t1
	ldw	CI_CURPROC(t1), t3
	ldil	L%copy_on_fault, t4
	ldw	P_ADDR(t3), r2
	ldo	R%copy_on_fault(t4), t4
	ldw	PCB_ONFAULT+U_PCB(r2), r1
	stw	t4, PCB_ONFAULT+U_PCB(r2)

	mtsp	arg0, sr1
	mtsp	arg2, sr2
	copy	arg1, arg0
	copy	r0, ret0

$spstrcpy_loop
	ldbs,ma	1(sr1, arg1), t1
	comb,=	r0, t1, $spstrcpy_exit
	stbs,ma	t1, 1(sr2, arg3)
	comb,<>,n t2, arg1, $spstrcpy_loop
	nop
	ldi	ENAMETOOLONG, ret0

$spstrcpy_exit
	mtsp	r0, sr1
	mtsp	r0, sr2
	stw	r1, PCB_ONFAULT+U_PCB(r2)
	ldw	HPPA_FRAME_CRP(sp), rp
	sub	arg1, arg0, arg1
	ldo	-64(sp), sp
	ldw	HPPA_FRAME_ARG(5)(sp), arg0
	sub,=	r0, arg0, r0
	stw	arg1, 0(arg0)
	bv	0(rp)
	nop
EXIT(spstrcpy)

/*
 * int spcopy32 (pa_space_t ssp, const uint32_t *src, pa_space_t dsp,
 *		uint32_t *dst)
 * do an atomic space to space copy of a futex
 */
LEAF_ENTRY(spcopy32)
	extru	arg1, 31, 2, t3
	extru	arg3, 31, 2, t4
	comb,<>,n 0, t3, $spcopy32_misaligned
	comb,<>,n 0, t4, $spcopy32_misaligned

	ldo	64(sp), sp
	stw	rp, HPPA_FRAME_CRP(sp)
	/* setup fault handler */
	mfctl	cr29, t1
	ldw	CI_CURPROC(t1), t3
	ldil	L%copy_on_fault, t2
	ldw	P_ADDR(t3), r2
	ldo	R%copy_on_fault(t2), t2
	ldw	PCB_ONFAULT+U_PCB(r2), r1
	stw	t2, PCB_ONFAULT+U_PCB(r2)

	mtsp	arg0, sr1
	mtsp	arg2, sr2

	ldw	0(sr1, arg1), t1
	stw	t1, 0(sr2, arg3)

	mtsp	r0, sr1
	mtsp	r0, sr2
	/* reset fault handler */
	stw	r1, PCB_ONFAULT+U_PCB(r2)
	ldw	HPPA_FRAME_CRP(sp), rp
	ldo	-64(sp), sp
	bv	0(rp)
	copy	r0, ret0

$spcopy32_misaligned
	bv	0(rp)
	ldi	EFAULT, ret0
EXIT(spcopy32)

/*
 * int cpu_switchto(struct proc *old, struct proc *new)
 * Switch from "old" proc to "new".
 */
	.align	32
ENTRY(cpu_switchto,128)
	copy	r3, r1
	stw	rp, HPPA_FRAME_CRP(sp)
	copy	sp, r3
	stwm	r1, HPPA_FRAME_SIZE+16*4(sp)

#ifdef DIAGNOSTIC
	b	kstack_check
	nop
switch_error
	copy	arg1, arg2
	copy	arg0, arg1
	ldil	L%panic, r1
	ldil	L%Lcspstr, arg0
	ldo	R%panic(r1), r1
	ldo	R%Lcspstr(arg0), arg0
	.call
	blr	%r0, rp
	bv,n	%r0(r1)
	nop
Lcspstr
	.asciz	"cpu_switch:old=%p, new=%p"
	.align	8
kstack_check
	/*
	 * The new process' kernel stack must be reasonable.
	 */
	ldw     P_ADDR(arg1), arg2
	ldw	U_PCB+PCB_KSP(arg2), t1
	ldo     NBPG(arg2), arg2
	comb,>>,n arg2, t1, switch_error
	nop
	sub     t1, arg2, t1
	ldil    L%USPACE, arg2
	ldo     R%USPACE(arg2), arg2
	comb,<<=,n arg2, t1, switch_error
	nop
kstack_ok
#endif

	/* Record new proc. */
	ldi	SONPROC, t1
	stb	t1, P_STAT(arg1)
	mfctl	cr29, t1
	stw	arg1, CI_CURPROC(t1)

	/* If old process exited, don't bother. */
	comb,=,n r0, arg0, switch_exited

	/*
	 * 2. save old proc context
	 *
	 * arg0: old proc
	 */
	ldw	P_ADDR(arg0), t1
	/* save callee-save registers */
	stw	r4,   1*4(r3)
	stw	sp, U_PCB+PCB_KSP(t1)
	stw	r5,   2*4(r3)
	stw	r6,   3*4(r3)
	stw	r7,   4*4(r3)
	stw	r8,   5*4(r3)
	stw	r9,   6*4(r3)
	stw	r10,  7*4(r3)
	stw	r11,  8*4(r3)
	stw	r12,  9*4(r3)
	stw	r13, 10*4(r3)
	stw	r14, 11*4(r3)
	stw	r15, 12*4(r3)
	stw	r16, 13*4(r3)
	stw	r17, 14*4(r3)
	stw	r18, 15*4(r3)
	fdc	r0(t1)
	stw	r0, HPPA_FRAME_ARG(1)(sp)	/* say no trampoline */
	sync

	/* don't need old curproc (arg0) starting from here */
switch_exited
	/*
	 * 3. restore new proc context
	 *
	 * arg1: new proc
	 */
	/* XXX disable interrupts? */
	ldw	P_ADDR(arg1), t2
	ldw	P_MD_REGS(arg1), t1
	ldw	U_PCB+PCB_KSP(t2), sp
	mtctl	r0, ccr			/* disable FPU */
	ldw	TF_CR30(t1), t2
	ldw	TF_CR9(t1), t3
	mtctl	t2, cr30
	mtctl	t3, pidr2
	/* XXX enable interrupts? */
	ldo	-(HPPA_FRAME_SIZE+16*4)(sp), r3
	ldw	HPPA_FRAME_ARG(0)(sp), arg0
	ldw	HPPA_FRAME_ARG(1)(sp), t4 /* in case we're on trampoline */
	sub,=	r0, t4, r0
	b	switch_return
	ldw	 1*4(r3), r4
	ldw	 2*4(r3), r5
	ldw	 3*4(r3), r6
	ldw	 4*4(r3), r7
	ldw	 5*4(r3), r8
	ldw	 6*4(r3), r9
	ldw	 7*4(r3), r10
	ldw	 8*4(r3), r11
	ldw	 9*4(r3), r12
	ldw	10*4(r3), r13
	ldw	11*4(r3), r14
	ldw	12*4(r3), r15
	ldw	13*4(r3), r16
	ldw	14*4(r3), r17
	ldw	15*4(r3), r18

switch_return
	ldw	HPPA_FRAME_CRP(r3), rp
	bv	0(rp)
	ldwm	-(HPPA_FRAME_SIZE+16*4)(sp), r3
EXIT(cpu_switchto)

LEAF_ENTRY(cpu_idle_enter)
	bv	0(rp)
	nop
EXIT(cpu_idle_enter)

LEAF_ENTRY(cpu_idle_cycle)
	bv	0(rp)
	nop
EXIT(cpu_idle_cycle)

LEAF_ENTRY(cpu_idle_leave)
	bv	0(rp)
	nop
EXIT(cpu_idle_leave)

ENTRY(proc_trampoline,0)
	copy	r0, r3
	copy	t4, r5
	copy	arg0, r4
	bl	proc_trampoline_mi, rp
	nop
	copy	r4, arg0
	copy	r5, t4
	.call
	blr	r0, rp
	bv,n	r0(t4)
	nop
	mfctl	cr29, t1
	ldw	CI_CURPROC(t1), t2
	.call
	b	$syscall_return
	ldw	P_MD_REGS(t2), t3
EXIT(proc_trampoline)

#ifdef MULTIPROCESSOR
/*
 * Trampoline to spin up secondary processors.
 */
ENTRY(hw_cpu_spinup_trampoline, 0)

	/*
	 * disable interrupts and turn off all bits in the psw so that
	 * we start in a known state.
	 */
	rsm	RESET_PSL, r0
	nop ! nop ! nop ! nop ! nop ! nop

	/* get things ready for the kernel to run in virtual mode */
	ldi	HPPA_PID_KERNEL, r1
	mtctl	r1, pidr1
	mtctl	r1, pidr2
#if pbably_not_worth_it
	mtctl	r0, pidr3
	mtctl	r0, pidr4
#endif
	mtsp	r0, sr0
	mtsp	r0, sr1
	mtsp	r0, sr2
	mtsp	r0, sr3
	mtsp	r0, sr4
	mtsp	r0, sr5
	mtsp	r0, sr6
	mtsp	r0, sr7

	/*
	 * disable all coprocessors
	 */
	mtctl   r0, ccr

	/*
	 * to keep the spl() routines consistent we need to put the correct
	 * spl level into eiem, and reset any pending interrupts
	 */
	ldi	-1, r1
	mtctl	r0, eiem
	mtctl	r1, eirr

	/*
	 * load address of interrupt vector table
	 */
	ldil	L%$ivaaddr, t2
	ldo	R%$ivaaddr(t2), t2
	mtctl	t2, iva

	/*
	 * set up the dp pointer so that we can do quick references off of it
	 */
	ldil	L%$global$,dp
	ldo	R%$global$(dp),dp

	/*
	 * Store address of cpu_info in CR29.
	 */
	ldil	L%cpu_hatch_info, r3
	ldw	R%cpu_hatch_info(r3), r3
	mtctl	r3, cr29

	/*
	 * Setup the stack frame for us to call C with and mark this as the
	 * first frame on the stack.
	 */
	ldw	CI_STACK(r3), sp
	stw,ma	r0, HPPA_FRAME_SIZE(sp)
	stw	r0, HPPA_FRAME_CRP(sp)
	stw	r0, HPPA_FRAME_PSP(sp)

	ldil	L%TFF_LAST, t1
	stw	t1, TF_FLAGS-TRAPFRAME_SIZEOF(sp)

	/* Provide CPU with page tables. */
	ldil	L%hppa_vtop, t1
	ldw	R%hppa_vtop(t1), t1
	mtctl	t1, CR_VTOP

	/* Turn on the Q bit so that we can handle TLB traps. */
	ldil	L%$q_enabled, t1
	ldo	R%$q_enabled(t1), t1
	mtctl	r0, pcsq
	mtctl	r0, pcsq
	mtctl	t1, pcoq
	ldo	4(t1), t1
	mtctl	t1, pcoq
	ldi	PSL_Q|PSL_I, t2
	mtctl	t2, ipsw
	rfi
	nop

$q_enabled

	/* Call C routine to setup CPU. */
	ldil	L%cpu_hw_init, r1
	ldo	R%cpu_hw_init(r1), r1
	.import cpu_hw_init, code
	.call
	blr	r0, rp
	bv,n	(r1)
	nop

	/* Switch CPU mode. */
	ldil	L%$cpu_spinup_vm, t1
	ldo	R%$cpu_spinup_vm(t1), t1
	mtctl	r0, pcsq
	mtctl	r0, pcsq
	mtctl	t1, pcoq
	ldo	4(t1), t1
	mtctl	t1, pcoq
	mfctl	cr29, t2
	ldw	CI_PSW(t2), t2
	mtctl	t2, ipsw
	rfi
	nop

$cpu_spinup_vm

	/*
	 * Okay, time to return to the land of C.
	 */
	b	cpu_hatch
	nop

EXIT(hw_cpu_spinup_trampoline)
#endif

/*
 * Signal "trampoline" code. Invoked from RTE setup by sendsig().
 */
	.section .rodata
	.align 4
	.export	sigcode, entry
	.label sigcode
	.proc
	.callinfo frame=0,calls, save_rp, save_sp
	.entry
sigcode:
	bb,>=,n	arg3, 30, sigcode_call
	dep	r0, 31, 2, arg3
	ldw	4(arg3), r19
	ldw	0(arg3), arg3
sigcode_call
	.call
	ble	0(sr0, arg3)
	copy	r31, rp

	ldil	L%SYSCALLGATE, r1
	copy	r4, arg0
	.call
	.globl	sigcodecall
sigcodecall:
	ble	4(sr7, r1)
	 ldi	SYS_sigreturn, t1
	.globl  sigcoderet
sigcoderet:
	break	0,0
ALTENTRY(esigcode)

EXIT(sigcode)
	.globl	sigfill
sigfill:
	break	0,0
esigfill:
	.align	4
	.globl	sigfillsiz
sigfillsiz:
	.word	esigfill - sigfill

	.text

	.end
