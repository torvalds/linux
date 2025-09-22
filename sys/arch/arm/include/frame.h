/*	$OpenBSD: frame.h,v 1.14 2022/12/08 01:25:44 guenther Exp $	*/
/*	$NetBSD: frame.h,v 1.9 2003/12/01 08:48:33 scw Exp $	*/

/*
 * Copyright (c) 1994-1997 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
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
 *	This product includes software developed by Brini.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * frame.h
 *
 * Stack frames structures
 *
 * Created      : 30/09/94
 */

#ifndef _ARM_FRAME_H_
#define _ARM_FRAME_H_

#ifndef _LOCORE

#include <sys/signal.h>

/*
 * Trap frame.  Pushed onto the kernel stack on a trap (synchronous exception).
 */

typedef struct trapframe {
	register_t tf_spsr;
	register_t tf_r0;
	register_t tf_r1;
	register_t tf_r2;
	register_t tf_r3;
	register_t tf_r4;
	register_t tf_r5;
	register_t tf_r6;
	register_t tf_r7;
	register_t tf_r8;
	register_t tf_r9;
	register_t tf_r10;
	register_t tf_r11;
	register_t tf_r12;
	register_t tf_usr_sp;
	register_t tf_usr_lr;
	register_t tf_svc_sp;
	register_t tf_svc_lr;
	register_t tf_pc;
	register_t tf_pad;
} trapframe_t;

/* Register numbers */
#define tf_r13 tf_usr_sp
#define tf_r14 tf_usr_lr
#define tf_r15 tf_pc

/* Determine if a fault came from user mode */
#define	TRAP_USERMODE(tf)	((tf->tf_spsr & PSR_MODE) == PSR_USR32_MODE)

/*
 * Signal frame.  Pushed onto user stack before calling sigcode.
 */

struct sigframe {
	int	sf_signum;
	siginfo_t *sf_sip;
	struct	sigcontext *sf_scp;
	sig_t	sf_handler;
	struct	sigcontext sf_sc;
	siginfo_t sf_si;
};

/* the pointers are used in the trampoline code to locate the ucontext */
#if 0
struct sigframe_siginfo {
	siginfo_t	sf_si;		/* actual saved siginfo */
	ucontext_t	sf_uc;		/* actual saved ucontext */
};
#endif

#if 0
#ifdef _KERNEL
void sendsig_sigcontext(const ksiginfo_t *, const sigset_t *);
#endif
#endif

#endif /* _LOCORE */

#ifndef _LOCORE

/*
 * System stack frames.
 */

typedef struct irqframe {
	unsigned int if_spsr;
	unsigned int if_r0;
	unsigned int if_r1;
	unsigned int if_r2;
	unsigned int if_r3;
	unsigned int if_r4;
	unsigned int if_r5;
	unsigned int if_r6;
	unsigned int if_r7;
	unsigned int if_r8;
	unsigned int if_r9;
	unsigned int if_r10;
	unsigned int if_r11;
	unsigned int if_r12;
	unsigned int if_usr_sp;
	unsigned int if_usr_lr;
	unsigned int if_svc_sp;
	unsigned int if_svc_lr;
	unsigned int if_pc;
	unsigned int if_pad;
} irqframe_t;

#define clockframe irqframe

/*
 * Switch frame
 */

struct switchframe {
	u_int	sf_pad;
	u_int	sf_r4;
	u_int	sf_r5;
	u_int	sf_r6;
	u_int	sf_r7;
	u_int	sf_pc;
};

/*
 * Stack frame. Used during stack traces (db_trace.c)
 */
struct frame {
	u_int	fr_fp;
	u_int	fr_sp;
	u_int	fr_lr;
	u_int	fr_pc;
};

#else /* _LOCORE */

#define	AST_LOCALS							 \
.Laflt_astpending:							;\
	.word	astpending

#define	DO_AST								 \
	ldr	r0, [sp]		/* Get the SPSR from stack */	;\
	mrs	r4, cpsr		/* save CPSR */			;\
	and	r0, r0, #(PSR_MODE)	/* Returning to USR mode? */	;\
	teq	r0, #(PSR_USR32_MODE)					;\
	ldreq	r5, .Laflt_astpending					;\
	bne	2f			/* Nope, get out now */		;\
	bic	r4, r4, #(PSR_I)					;\
1:	orr	r0, r4, #(PSR_I)	/* Disable IRQs */		;\
	msr	cpsr_c, r0						;\
	ldr	r1, [r5]		/* Pending AST? */		;\
	teq	r1, #0x00000000						;\
	beq	2f			/* Nope. Just bail */		;\
	mov	r1, #0x00000000						;\
	str	r1, [r5]		/* Clear astpending */		;\
	msr	cpsr_c, r4		/* Restore interrupts */	;\
	mov	r0, sp							;\
	adr	lr, 1b							;\
	b	ast			/* ast(frame) */		;\
2:

/*
 * ASM macros for pushing and pulling trapframes from the stack
 *
 * These macros are used to handle the irqframe and trapframe structures
 * defined above.
 */

/*
 * CLREX - On ARMv7 machines that support atomic instructions, we need
 * to clear the exclusive monitors on kernel exit, so that a userland
 * atomic store can't succeed due to an unrelated outstanding atomic
 * operation. ARM also highly recommends clearing the monitor on data
 * aborts, as the monitor state after taking a data abort is unknown.
 * Issuing a clrex on kernel entry and on kernel exit is the easiest
 * way to take care of both issues and to make sure that the kernel
 * and userland do not leave any outstanding reserves active.
 */

/*
 * PUSHFRAME - macro to push a trap frame on the stack in the current mode
 * Since the current mode is used, the SVC lr field is not defined.
 */

#define PUSHFRAME							   \
	clrex;								   \
	sub	sp, sp, #4;		/* Align the stack */		   \
	str	lr, [sp, #-4]!;		/* Push the return address */	   \
	sub	sp, sp, #(4*17);	/* Adjust the stack pointer */	   \
	stmia	sp, {r0-r14}^;		/* Push the user mode registers */ \
	mov	r0, r0;			/* NOP for previous instruction */ \
	mrs	r0, spsr;		/* Put the SPSR on the stack */	   \
	str	r0, [sp, #-4]!

/*
 * PULLFRAME - macro to pull a trap frame from the stack in the current mode
 * Since the current mode is used, the SVC lr field is ignored.
 */

#define PULLFRAME							   \
	clrex;								   \
	ldr	r0, [sp], #0x0004;	/* Get the SPSR from stack */	   \
	msr	spsr_fsxc, r0;						   \
	ldmia	sp, {r0-r14}^;		/* Restore registers (usr mode) */ \
	mov	r0, r0;			/* NOP for previous instruction */ \
	add	sp, sp, #(4*17);	/* Adjust the stack pointer */	   \
	ldr	lr, [sp], #0x0004;	/* Pull the return address */	   \
	add	sp, sp, #4		/* Align the stack */

/*
 * PUSHFRAMEINSVC - macro to push a trap frame on the stack in SVC32 mode
 * This should only be used if the processor is not currently in SVC32
 * mode. The processor mode is switched to SVC mode and the trap frame is
 * stored. The SVC lr field is used to store the previous value of
 * lr in SVC mode.
 */

#define PUSHFRAMEINSVC							   \
	clrex;								   \
	stmdb	sp, {r0-r3};		/* Save 4 registers */		   \
	mov	r0, lr;			/* Save xxx32 r14 */		   \
	mov	r1, sp;			/* Save xxx32 sp */		   \
	mrs	r3, spsr;		/* Save xxx32 spsr */		   \
	mrs	r2, cpsr; 		/* Get the CPSR */		   \
	bic	r2, r2, #(PSR_MODE);	/* Fix for SVC mode */		   \
	orr	r2, r2, #(PSR_SVC32_MODE);				   \
	msr	cpsr_c, r2;		/* Punch into SVC mode */	   \
	mov	r2, sp;			/* Save	SVC sp */		   \
	bic	sp, sp, #7;		/* Align sp to an 8-byte address */   \
	sub	sp, sp, #4;		/* Pad trapframe to keep alignment */ \
	str	r0, [sp, #-4]!;		/* Push return address */	   \
	str	lr, [sp, #-4]!;		/* Push SVC lr */		   \
	str	r2, [sp, #-4]!;		/* Push SVC sp */		   \
	msr	spsr_fsxc, r3;		/* Restore correct spsr */	   \
	ldmdb	r1, {r0-r3};		/* Restore 4 regs from xxx mode */ \
	sub	sp, sp, #(4*15);	/* Adjust the stack pointer */	   \
	stmia	sp, {r0-r14}^;		/* Push the user mode registers */ \
	mov	r0, r0;			/* NOP for previous instruction */ \
	mrs	r0, spsr;		/* Put the SPSR on the stack */	   \
	str	r0, [sp, #-4]!

/*
 * PULLFRAMEFROMSVCANDEXIT - macro to pull a trap frame from the stack
 * in SVC32 mode and restore the saved processor mode and PC.
 * This should be used when the SVC lr register needs to be restored on
 * exit.
 */

#define PULLFRAMEFROMSVCANDEXIT						   \
	clrex;								   \
	ldr	r0, [sp], #0x0004;	/* Get the SPSR from stack */	   \
	msr	spsr_fsxc, r0;		/* restore SPSR */		   \
	ldmia	sp, {r0-r14}^;		/* Restore registers (usr mode) */ \
	mov	r0, r0;			/* NOP for previous instruction */ \
	add	sp, sp, #(4*15);	/* Adjust the stack pointer */	   \
	ldmia	sp, {sp, lr, pc}^	/* Restore lr and exit */

#endif /* _LOCORE */

#endif /* _ARM_FRAME_H_ */
