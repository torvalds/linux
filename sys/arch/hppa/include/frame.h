/*	$OpenBSD: frame.h,v 1.19 2012/06/21 00:56:59 guenther Exp $	*/

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


#ifndef _MACHINE_FRAME_H_
#define _MACHINE_FRAME_H_

/*
 * Call frame definitions
 */
#define	HPPA_FRAME_NARGS	(12)
#define	HPPA_FRAME_MAXARGS	(HPPA_FRAME_NARGS * 4)
#define	HPPA_FRAME_ARG(n)	(-(32 + 4*((n) + 1)))
#define	HPPA_FRAME_CARG(n,sp)	((register_t *)((sp) + HPPA_FRAME_ARG(n)))
#define	HPPA_FRAME_SIZE		(64)
#define	HPPA_FRAME_PSP		(-4)
#define	HPPA_FRAME_EP		(-8)
#define	HPPA_FRAME_CLUP		(-12)
#define	HPPA_FRAME_SL		(-16)
#define	HPPA_FRAME_CRP		(-20)
#define	HPPA_FRAME_ERP		(-24)
#define	HPPA_FRAME_ESR4		(-28)
#define	HPPA_FRAME_EDP		(-32)

/*
 * Macros to decode processor status word.
 */
#define	HPPA_PC_PRIV_MASK    3
#define	HPPA_PC_PRIV_KERN    0
#define	HPPA_PC_PRIV_USER    3
#define	USERMODE(pc)    ((((register_t)pc) & HPPA_PC_PRIV_MASK) != HPPA_PC_PRIV_KERN)
#define	KERNMODE(pc)	(((register_t)pc) & ~HPPA_PC_PRIV_MASK)

#ifndef _LOCORE
/*
 * the trapframe is divided into two parts:
 *	one is saved while we are in the physical mode (beginning of the trap),
 *	and should be kept as small as possible, since all the interrupts will
 *	be lost during this phase, also it must be 64-bytes aligned, per
 *	pa-risc stack conventions, and its dependencies in the code (;
 *	the other part is filled out when we are already in the virtual mode,
 *	are able to catch interrupts (they are kept pending) and perform
 *	other trap activities (like tlb misses).
 */
struct trapframe {
	/* the `physical' part of the trapframe */
	unsigned long	tf_t1;		/* r22 */
	unsigned long	tf_t2;		/* r21 */
	unsigned long	tf_sp;		/* r30 */
	unsigned long	tf_t3;		/* r20 */
	unsigned long	tf_iisq_head;	/* cr17 */
	unsigned long	tf_iisq_tail;
	unsigned long	tf_iioq_head;	/* cr18 */
	unsigned long	tf_iioq_tail;
	unsigned long	tf_eiem;	/* cr15 */
	unsigned long	tf_ipsw;	/* cr22 */
	unsigned long	tf_sr3;
	unsigned long	tf_pidr1;	/* cr8 */
	unsigned long	tf_isr;		/* cr20 */
	unsigned long	tf_ior;		/* cr21 */
	unsigned long	tf_iir;		/* cr19 */
	unsigned long	tf_flags;

	/* here starts the `virtual' part */
	unsigned long	tf_sar;		/* cr11 */
	unsigned long	tf_r1;
	unsigned long	tf_rp;          /* r2 */
	unsigned long	tf_r3;          /* frame pointer when -g */
	unsigned long	tf_r4;
	unsigned long	tf_r5;
	unsigned long	tf_r6;
	unsigned long	tf_r7;
	unsigned long	tf_r8;
	unsigned long	tf_r9;
	unsigned long	tf_r10;
	unsigned long	tf_r11;
	unsigned long	tf_r12;
	unsigned long	tf_r13;
	unsigned long	tf_r14;
	unsigned long	tf_r15;
	unsigned long	tf_r16;
	unsigned long	tf_r17;
	unsigned long	tf_r18;
	unsigned long	tf_t4;		/* r19 */
	unsigned long	tf_arg3;	/* r23 */
	unsigned long	tf_arg2;	/* r24 */
	unsigned long	tf_arg1;	/* r25 */
	unsigned long	tf_arg0;	/* r26 */
	unsigned long	tf_dp;		/* r27 */
	unsigned long	tf_ret0;	/* r28 */
	unsigned long	tf_ret1;	/* r29 */
	unsigned long	tf_r31;
	unsigned long	tf_sr0;
	unsigned long	tf_sr1;
	unsigned long	tf_sr2;
	unsigned long	tf_sr4;
	unsigned long	tf_sr5;
	unsigned long	tf_sr6;
	unsigned long	tf_sr7;
	unsigned long	tf_pidr2;	/* cr9 */
	unsigned long	tf_pidr3;	/* cr12 */
	unsigned long	tf_pidr4;	/* cr13 */
	unsigned long	tf_rctr;	/* cr0 */
	unsigned long	tf_ccr;		/* cr10 */
	unsigned long	tf_eirr;	/* cr23 - DDB */
	unsigned long	tf_vtop;	/* cr25 - DDB */
	unsigned long	tf_cr27;
	unsigned long	tf_cr28;	/*      - DDB */
	unsigned long	tf_cr30;	/* uaddr */

	unsigned long	tf_pad[3];	/* pad to 256 bytes */
};

#ifdef _KERNEL
int	setstack(struct trapframe *, u_long, register_t);
#endif /* _KERNEL */

#endif /* !_LOCORE */

#endif /* !_MACHINE_FRAME_H_ */
