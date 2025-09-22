/*	$OpenBSD: locore0.S,v 1.1 2017/06/05 18:59:06 deraadt Exp $	*/

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

	.import	$global$, data
	.import pdc, data
	.import	boothowto, data
	.import	bootdev, data
	.import	esym, data
	.import cpu_info, data
	.import	proc0, data
	.import	proc0paddr, data
	.import proc0fpstate, data

#define	EMRG_STACKSIZE	(1*NBPG)
#define	FPEMU_STACKSIZE	(1*NBPG)

	.data

	BSS(pdc_stack, 4)	/* temp stack for PDC call */
	BSS(emrg_stack, 4)	/* stack for HPMC/TOC/PWRF */

	.export	kernelmapped, data
	BSS(kernelmapped, 4)	/* set when kernel is mapped */
	.export fpu_enable, data
	BSS(fpu_enable, 4)	/* bits to set in the ccr to enable fpu */
	BSS(cpu_fpuena, 4)	/* enable FPU, otherwise force emulate */
	BSS(fpu_scratch, 16)	/* FPU scratch space, enough for a quad */
	.export hppa_vtop, data
	BSS(hppa_vtop, 4)	/* a vtop translation table addr (pa=va) */

	.text
	.import	$kernel_setup, entry

/*
 * This is the starting location for the kernel
 */
ENTRY($start,0)
/*
 *	start(pdc, boothowto, bootdev, esym, bootapiver, argv, argc)
 *
 *	pdc - PDC entry point
 *	boothowto - boot flags (see "reboot.h")
 *	bootdev - boot device (index into bdevsw)
 *	esym - end of symbol table (or &end if not present)
 *	bootapiver - /boot API version
 *	argv - options block passed from /boot
 *	argc - the length of the block
 */

	/*
	 * save the pdc, boothowto, bootdev and esym arguments
	 */
	ldil	L%pdc,r1
	stw	arg0,R%pdc(r1)
	ldil	L%boothowto,r1
	stw	arg1,R%boothowto(r1)
	ldil	L%bootdev,r1
	stw	arg2,R%bootdev(r1)
	ldil	L%esym,r1
	stw	arg3,R%esym(r1)

	/* Align arg3, which is the start of available memory */
	ldo	NBPG-1(arg3), arg3
	dep	r0, 31, PGSHIFT, arg3

	/* assuming size being page-aligned */
#define	STACK_ALLOC(n,s)	\
	ldil	L%(n), t1	! \
	ldil	L%(s), t2	! \
	stw	arg3, R%(n)(t1)	! \
	add	arg3, t2, arg3

	STACK_ALLOC(pdc_stack, PDC_STACKSIZE)
	STACK_ALLOC(emrg_stack, EMRG_STACKSIZE)

#undef	STACK_ALLOC

	/* zero fake trapframe and proc0 u-area */
	copy	arg3, t2
	ldi	NBPG+TRAPFRAME_SIZEOF, t1
$start_zero_tf
	stws,ma r0, 4(t2)
	addib,>= -8, t1, $start_zero_tf
	stws,ma r0, 4(t2)	/* XXX could use ,bc here, but gas is broken */

	/*
	 * kernel stack lives here (arg3 is page-aligned esym)
	 * initialize the pcb
	 * arg0 will be available space for hppa_init()
	 */
	ldo	NBPG+TRAPFRAME_SIZEOF(arg3), sp
	stw	r0, U_PCB+PCB_ONFAULT(arg3)
	stw	r0, U_PCB+PCB_SPACE(arg3)	/* XXX HPPA_SID_KERNEL == 0 */
	ldil	L%(USPACE+NBPG), arg0		/* normal U plus red zone */
	add	arg0, arg3, arg0
	ldil	L%proc0paddr, t1
	stw	arg3, R%proc0paddr(t1)
	ldil	L%proc0, t2
	stw	arg3, R%proc0+P_ADDR(t2)
	ldo	-TRAPFRAME_SIZEOF(sp), t3
	stw	t3, R%proc0+P_MD_REGS(t2)

	ldil	L%TFF_LAST, t1
	stw	t1, TF_FLAGS-TRAPFRAME_SIZEOF(sp)
	ldil	L%proc0fpstate, t1
	ldo	R%proc0fpstate(t1), t1
	stw	t1, TF_CR30-TRAPFRAME_SIZEOF(sp)
	mtctl	t1, cr30

	/*
	 * disable all coprocessors
	 */
	mtctl	r0, ccr

	/* Store pointer to cpu_info in CR29. */
	ldil	L%cpu_info, r1
	ldo	R%cpu_info(r1), r1
	mtctl	r1, cr29

#ifdef MULTIPROCESSOR
	/* Setup SMP rendezvous address. */
	ldil	L%hw_cpu_spinup_trampoline, r1
	ldo	R%hw_cpu_spinup_trampoline(r1), r1
	stw	r1, 0x10(0)	/* MEM_RENDEZ */
	stw	r0, 0x28(0)	/* MEM_RENDEZ */
#endif

	copy	sp, arg1
	ldil	L%$qisnowon, rp
	ldo	R%$qisnowon(rp), rp
	b	$kernel_setup
	ldi	PSL_Q|PSL_I, arg2

$qisnowon
	/*
	 * call C routine hppa_init() to initialize VM
	 */
	ldil	L%hppa_init, r1
	ldo	R%hppa_init(r1), r1
	.import hppa_init, code
	.call
	blr	r0, rp
	bv,n	(r1)
	nop

	/*
	 * Cannot change the queues or IPSW with the Q-bit on
	 */
	rsm	RESET_PSL, r0
	nop ! nop ! nop ! nop ! nop ! nop ! nop

	/*
	 * We need to do an rfi to get the C bit set
	 */
	mtctl	r0, pcsq
	mtctl	r0, pcsq
	ldil	L%$virtual_mode, t1
	ldo	R%$virtual_mode(t1), t1
	mtctl	t1, pcoq
	ldo	4(t1), t1
	mtctl	t1, pcoq
	mfctl	r29, t1
	ldw	CI_PSW(t1), t2
	mtctl	t2, ipsw
	rfi
	nop

$virtual_mode
	ldil	L%kernelmapped, t1
	stw	t1, R%kernelmapped(t1)

#ifdef DDB
	.import	db_enter, code
	/* have to call debugger from here, from virtual mode */
	ldil	L%boothowto, r1
	ldw	R%boothowto(r1), r1
	bb,>=	r1, 25, $noddb
	nop

	break	HPPA_BREAK_KERNEL, HPPA_BREAK_KGDB
$noddb
#endif

	.import main,code
	ldil	L%main, r1
	ldo	R%main(r1), r1
$callmain
	.call
	blr	r0, rp
	bv,n	(r1)
	nop

	/* should never return... */
	bv	(rp)
	nop
EXIT($start)
