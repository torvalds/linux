/*	$NetBSD: exception.S,v 1.13 2003/10/31 16:30:15 scw Exp $	*/

/*-
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
 * exception.S
 *
 * Low level handlers for exception vectors
 *
 * Created      : 24/09/94
 *
 * Based on kate/display/abort.s
 *
 */

#include "assym.inc"

#include <machine/asm.h>
#include <machine/armreg.h>
#include <machine/asmacros.h>
#include <machine/trap.h>

__FBSDID("$FreeBSD$");

#ifdef KDTRACE_HOOKS
	.bss
	.align 4
	.global _C_LABEL(dtrace_invop_jump_addr)
_C_LABEL(dtrace_invop_jump_addr):
	.word 0
	.word 0
#endif

	.text
	.align	2

/*
 * ASM macros for pushing and pulling trapframes from the stack
 *
 * These macros are used to handle the irqframe and trapframe structures
 * defined above.
 */

/*
 * PUSHFRAME - macro to push a trap frame on the stack in the current mode
 * Since the current mode is used, the SVC lr field is not defined.
 *
 * NOTE: r13 and r14 are stored separately as a work around for the
 * SA110 rev 2 STM^ bug
 */
#if __ARM_ARCH < 6
#define PUSHFRAME							   \
	sub	sp, sp, #4;		/* Align the stack */		   \
	str	lr, [sp, #-4]!;		/* Push the return address */	   \
	sub	sp, sp, #(4*17);	/* Adjust the stack pointer */	   \
	stmia	sp, {r0-r12};		/* Push the user mode registers */ \
	add	r0, sp, #(4*13);	/* Adjust the stack pointer */	   \
	stmia	r0, {r13-r14}^;		/* Push the user mode registers */ \
	mov	r0, r0;			/* NOP for previous instruction */ \
	mrs	r0, spsr;		/* Put the SPSR on the stack */	   \
	str	r0, [sp, #-4]!;						   \
	ldr	r0, =ARM_RAS_START;					   \
	mov	r1, #0;							   \
	str	r1, [r0];						   \
	mov	r1, #0xffffffff;					   \
	str	r1, [r0, #4];
#else
#define PUSHFRAME							   \
	sub	sp, sp, #4;		/* Align the stack */		   \
	str	lr, [sp, #-4]!;		/* Push the return address */	   \
	sub	sp, sp, #(4*17);	/* Adjust the stack pointer */	   \
	stmia	sp, {r0-r12};		/* Push the user mode registers */ \
	add	r0, sp, #(4*13);	/* Adjust the stack pointer */	   \
	stmia	r0, {r13-r14}^;		/* Push the user mode registers */ \
	mov	r0, r0;			/* NOP for previous instruction */ \
	mrs	r0, spsr;		/* Put the SPSR on the stack */	   \
	str	r0, [sp, #-4]!;
#endif

/*
 * PULLFRAME - macro to pull a trap frame from the stack in the current mode
 * Since the current mode is used, the SVC lr field is ignored.
 */

#if __ARM_ARCH < 6
#define PULLFRAME							   \
	ldr	r0, [sp], #4;		/* Get the SPSR from stack */	   \
	msr	spsr_fsxc, r0;						   \
	ldmia	sp, {r0-r14}^;		/* Restore registers (usr mode) */ \
	mov	r0, r0;			/* NOP for previous instruction */ \
	add	sp, sp, #(4*17);	/* Adjust the stack pointer */	   \
 	ldr	lr, [sp], #4;		/* Pull the return address */	   \
	add	sp, sp, #4		/* Align the stack */
#else
#define PULLFRAME							   \
	ldr	r0, [sp], #4	;	/* Get the SPSR from stack */	   \
	msr	spsr_fsxc, r0;						   \
	clrex;								   \
	ldmia   sp, {r0-r14}^;		/* Restore registers (usr mode) */ \
	mov	r0, r0;			/* NOP for previous instruction */ \
	add	sp, sp, #(4*17);	/* Adjust the stack pointer */	   \
 	ldr	lr, [sp], #4;		/* Pull the return address */	   \
	add	sp, sp, #4		/* Align the stack */
#endif

/*
 * PUSHFRAMEINSVC - macro to push a trap frame on the stack in SVC32 mode
 * This should only be used if the processor is not currently in SVC32
 * mode. The processor mode is switched to SVC mode and the trap frame is
 * stored. The SVC lr field is used to store the previous value of
 * lr in SVC mode.
 *
 * NOTE: r13 and r14 are stored separately as a work around for the
 * SA110 rev 2 STM^ bug
 */
#if __ARM_ARCH < 6
#define PUSHFRAMEINSVC							   \
	stmdb	sp, {r0-r3};		/* Save 4 registers */		   \
	mov	r0, lr;			/* Save xxx32 r14 */		   \
	mov	r1, sp;			/* Save xxx32 sp */		   \
	mrs	r3, spsr;		/* Save xxx32 spsr */		   \
	mrs	r2, cpsr; 		/* Get the CPSR */		   \
	bic	r2, r2, #(PSR_MODE);	/* Fix for SVC mode */		   \
	orr	r2, r2, #(PSR_SVC32_MODE);				   \
	msr	cpsr_c, r2;		/* Punch into SVC mode */	   \
	mov	r2, sp;			/* Save	SVC sp */		   \
	bic	sp, sp, #7;		/* Align sp to an 8-byte addrress */  \
	sub	sp, sp, #(4 * 17);	/* Pad trapframe to keep alignment */ \
				    /* and for dtrace to emulate push/pop */  \
	str	r0, [sp, #-4]!;		/* Push return address */	   \
	str	lr, [sp, #-4]!;		/* Push SVC lr */		   \
	str	r2, [sp, #-4]!;		/* Push SVC sp */		   \
	msr	spsr_fsxc, r3;		/* Restore correct spsr */	   \
	ldmdb	r1, {r0-r3};		/* Restore 4 regs from xxx mode */ \
	sub	sp, sp, #(4*15);	/* Adjust the stack pointer */	   \
	stmia	sp, {r0-r12};		/* Push the user mode registers */ \
	add	r0, sp, #(4*13);	/* Adjust the stack pointer */	   \
	stmia	r0, {r13-r14}^;		/* Push the user mode registers */ \
	mov	r0, r0;			/* NOP for previous instruction */ \
	ldr	r5, =ARM_RAS_START;	/* Check if there's any RAS */	   \
	ldr	r4, [r5, #4];		/* reset it to point at the     */ \
	cmp	r4, #0xffffffff;	/* end of memory if necessary;  */ \
	movne	r1, #0xffffffff;	/* leave value in r4 for later  */ \
	strne	r1, [r5, #4];		/* comparison against PC.      */ \
	ldr	r3, [r5];		/* Retrieve global RAS_START    */ \
	cmp	r3, #0;			/* and reset it if non-zero.    */ \
	movne	r1, #0;			/* If non-zero RAS_START and    */ \
	strne	r1, [r5];		/* PC was lower than RAS_END,   */ \
	ldrne	r1, [r0, #16];		/* adjust the saved PC so that  */ \
	cmpne	r4, r1;			/* execution later resumes at   */ \
	strhi	r3, [r0, #16];		/* the RAS_START location.      */ \
	mrs	r0, spsr;						   \
	str	r0, [sp, #-4]!
#else
#define PUSHFRAMEINSVC							   \
	stmdb	sp, {r0-r3};		/* Save 4 registers */		   \
	mov	r0, lr;			/* Save xxx32 r14 */		   \
	mov	r1, sp;			/* Save xxx32 sp */		   \
	mrs	r3, spsr;		/* Save xxx32 spsr */		   \
	mrs	r2, cpsr;		/* Get the CPSR */		   \
	bic	r2, r2, #(PSR_MODE);	/* Fix for SVC mode */		   \
	orr	r2, r2, #(PSR_SVC32_MODE);				   \
	msr	cpsr_c, r2;		/* Punch into SVC mode */	   \
	mov	r2, sp;			/* Save	SVC sp */		   \
	bic	sp, sp, #7;		/* Align sp to an 8-byte addrress */  \
	sub	sp, sp, #(4 * 17);	/* Pad trapframe to keep alignment */ \
				    /* and for dtrace to emulate push/pop */  \
	str	r0, [sp, #-4]!;		/* Push return address */	   \
	str	lr, [sp, #-4]!;		/* Push SVC lr */		   \
	str	r2, [sp, #-4]!;		/* Push SVC sp */		   \
	msr	spsr_fsxc, r3;		/* Restore correct spsr */	   \
	ldmdb	r1, {r0-r3};		/* Restore 4 regs from xxx mode */ \
	sub	sp, sp, #(4*15);	/* Adjust the stack pointer */	   \
	stmia	sp, {r0-r12};		/* Push the user mode registers */ \
	add	r0, sp, #(4*13);	/* Adjust the stack pointer */	   \
	stmia	r0, {r13-r14}^;		/* Push the user mode registers */ \
	mov	r0, r0;			/* NOP for previous instruction */ \
	mrs	r0, spsr;		/* Put the SPSR on the stack */	   \
	str	r0, [sp, #-4]!
#endif

/*
 * PULLFRAMEFROMSVCANDEXIT - macro to pull a trap frame from the stack
 * in SVC32 mode and restore the saved processor mode and PC.
 * This should be used when the SVC lr register needs to be restored on
 * exit.
 */

#if __ARM_ARCH < 6
#define PULLFRAMEFROMSVCANDEXIT						   \
	ldr	r0, [sp], #4;		/* Get the SPSR from stack */	   \
	msr	spsr_fsxc, r0;		/* restore SPSR */		   \
	ldmia	sp, {r0-r14}^;		/* Restore registers (usr mode) */ \
	mov	r0, r0;	  		/* NOP for previous instruction */ \
	add	sp, sp, #(4*15);	/* Adjust the stack pointer */	   \
	ldmia	sp, {sp, lr, pc}^	/* Restore lr and exit */
#else
#define PULLFRAMEFROMSVCANDEXIT						   \
	ldr	r0, [sp], #4;		/* Get the SPSR from stack */	   \
	msr	spsr_fsxc, r0;		/* restore SPSR */		   \
	clrex;								   \
	ldmia	sp, {r0-r14}^;		/* Restore registers (usr mode) */ \
	mov	r0, r0;	  		/* NOP for previous instruction */ \
	add	sp, sp, #(4*15);	/* Adjust the stack pointer */	   \
	ldmia	sp, {sp, lr, pc}^	/* Restore lr and exit */
#endif

/*
 * Unwind hints so we can unwind past functions that use
 * PULLFRAMEFROMSVCANDEXIT. They are run in reverse order.
 * As the last thing we do is restore the stack pointer
 * we can ignore the padding at the end of struct trapframe.
 */
#define	UNWINDSVCFRAME							   \
	.save {r13-r15};		/* Restore sp, lr, pc */	   \
	.pad #(2*4);			/* Skip user sp and lr */	   \
	.save {r0-r12};			/* Restore r0-r12 */		   \
	.pad #(4)			/* Skip spsr */

#define	DO_AST								   \
	ldr	r0, [sp];		/* Get the SPSR from stack */	   \
	mrs	r4, cpsr;		/* save CPSR */			   \
	orr	r1, r4, #(PSR_I|PSR_F);					   \
	msr	cpsr_c, r1;		/* Disable interrupts */	   \
	and	r0, r0, #(PSR_MODE);	/* Returning to USR mode? */	   \
	teq	r0, #(PSR_USR32_MODE);					   \
	bne	2f;			/* Nope, get out now */		   \
	bic	r4, r4, #(PSR_I|PSR_F);					   \
1:	GET_CURTHREAD_PTR(r5);						   \
	ldr	r1, [r5, #(TD_FLAGS)];					   \
	and	r1, r1, #(TDF_ASTPENDING|TDF_NEEDRESCHED);		   \
	teq	r1, #0;							   \
	beq	2f;			/* Nope. Just bail */		   \
	msr	cpsr_c, r4;		/* Restore interrupts */	   \
	mov	r0, sp;							   \
	bl	_C_LABEL(ast);		/* ast(frame) */		   \
	orr	r0, r4, #(PSR_I|PSR_F);					   \
	msr	cpsr_c, r0;						   \
	b	1b;							   \
2:


/*
 * Entry point for a Software Interrupt (SWI).
 *
 * The hardware switches to svc32 mode on a swi, so we're already on the
 * right stack; just build a trapframe and call the handler.
 */
ASENTRY_NP(swi_entry)
	PUSHFRAME			/* Build the trapframe on the */
	mov	r0, sp			/* scv32 stack, pass it to the */
	bl	_C_LABEL(swi_handler)	/* swi handler. */
	/*
	 * The fork_trampoline() code in swtch.S aranges for the MI fork_exit()
	 * to return to swi_exit here, to return to userland.  The net effect is
	 * that a newly created thread appears to return from a SWI just like
	 * the parent thread that created it.
	 */
ASEENTRY_NP(swi_exit)
	DO_AST				/* Handle pending signals. */
	PULLFRAME			/* Deallocate trapframe. */
	movs	pc, lr			/* Return to userland. */
	STOP_UNWINDING			/* Don't unwind into user mode. */
EEND(swi_exit)
END(swi_entry)

/*
 * Standard exception exit handler.
 *
 * This is used to return from all exceptions except SWI.  It uses DO_AST and
 * PULLFRAMEFROMSVCANDEXIT and can only be called if the exception entry code
 * used PUSHFRAMEINSVC.
 *
 * If the return is to user mode, this uses DO_AST to deliver any pending
 * signals and/or handle TDF_NEEDRESCHED first.
 */
ASENTRY_NP(exception_exit)
	DO_AST				/* Handle pending signals. */
	PULLFRAMEFROMSVCANDEXIT		/* Return. */
	UNWINDSVCFRAME			/* Special unwinding for exceptions. */
END(exception_exit)

/*
 * Entry point for a Prefetch Abort exception.
 *
 * The hardware switches to the abort mode stack; we switch to svc32 before
 * calling the handler, then return directly to the original mode/stack
 * on exit (without transitioning back through the abort mode stack).
 */
ASENTRY_NP(prefetch_abort_entry)
#ifdef __XSCALE__
	nop				/* Make absolutely sure any pending */
	nop				/* imprecise aborts have occurred. */
#endif
	sub	lr, lr, #4		/* Adjust the lr. Transition to scv32 */
	PUSHFRAMEINSVC			/* mode stack, build trapframe there. */
	adr	lr, exception_exit	/* Return from handler via standard */
	mov	r0, sp			/* exception exit routine.  Pass the */
	mov	r1, #1			/* Type flag */
	b	_C_LABEL(abort_handler)
END(prefetch_abort_entry)

/*
 * Entry point for a Data Abort exception.
 *
 * The hardware switches to the abort mode stack; we switch to svc32 before
 * calling the handler, then return directly to the original mode/stack
 * on exit (without transitioning back through the abort mode stack).
 */
ASENTRY_NP(data_abort_entry)
#ifdef __XSCALE__
	nop				/* Make absolutely sure any pending */
	nop				/* imprecise aborts have occurred. */
#endif
	sub	lr, lr, #8		/* Adjust the lr. Transition to scv32 */
	PUSHFRAMEINSVC			/* mode stack, build trapframe there. */
	adr	lr, exception_exit	/* Exception exit routine */
	mov	r0, sp			/* Trapframe to the handler */
	mov	r1, #0			/* Type flag */
	b	_C_LABEL(abort_handler)
END(data_abort_entry)

/*
 * Entry point for an Undefined Instruction exception.
 *
 * The hardware switches to the undefined mode stack; we switch to svc32 before
 * calling the handler, then return directly to the original mode/stack
 * on exit (without transitioning back through the undefined mode stack).
 */
ASENTRY_NP(undefined_entry)
	PUSHFRAMEINSVC			/* mode stack, build trapframe there. */
	mov	r4, r0			/* R0 contains SPSR */
	adr	lr, exception_exit      /* Return from handler via standard */
	mov	r0, sp			/* exception exit routine. pass frame */

	ldr	r2, [sp, #(TF_PC)]	/* load pc */
#if __ARM_ARCH >= 7
	tst	r4, #(PSR_T)		/* test if PSR_T */
	subne	r2, r2, #(THUMB_INSN_SIZE)
	subeq	r2, r2, #(INSN_SIZE)
#else
	sub	r2, r2, #(INSN_SIZE)	/* fix pc */
#endif
	str	r2, [sp, #TF_PC]	/* store pc */

#ifdef KDTRACE_HOOKS
	/* Check if dtrace is enabled */
	ldr	r1, =_C_LABEL(dtrace_invop_jump_addr)
	ldr	r3, [r1]
	cmp	r3, #0
	beq	undefinedinstruction

	and	r4, r4, #(PSR_MODE)	/* Mask out unneeded bits */
	cmp	r4, #(PSR_USR32_MODE)	/* Check if we came from usermode */
	beq	undefinedinstruction

	ldr	r4, [r2]		/* load instrution */
	ldr	r1, =FBT_BREAKPOINT	/* load fbt inv op */
	cmp	r1, r4
	bne	undefinedinstruction

	bx	r3			/* call invop_jump_addr */
#endif
	b	undefinedinstruction	/* call stadnard handler */
END(undefined_entry)

/*
 * Entry point for a normal IRQ.
 *
 * The hardware switches to the IRQ mode stack; we switch to svc32 before
 * calling the handler, then return directly to the original mode/stack
 * on exit (without transitioning back through the IRQ mode stack).
 */
ASENTRY_NP(irq_entry)
	sub	lr, lr, #4		/* Adjust the lr. Transition to scv32 */
	PUSHFRAMEINSVC			/* mode stack, build trapframe there. */
	adr	lr, exception_exit	/* Return from handler via standard */
	mov	r0, sp			/* exception exit routine.  Pass the */
	b	_C_LABEL(intr_irq_handler)/* trapframe to the handler. */
END(irq_entry)

/*
 * Entry point for an FIQ interrupt.
 *
 * We don't currently support FIQ handlers very much.  Something can
 * install itself in the FIQ vector using code (that may or may not work
 * these days) in fiq.c.  If nobody does that and an FIQ happens, this
 * default handler just disables FIQs and otherwise ignores it.
 */
ASENTRY_NP(fiq_entry)
	mrs	r8, cpsr		/* FIQ handling isn't supported, */
	bic	r8, #(PSR_F)		/* just disable FIQ and return.  */
	msr	cpsr_c, r8		/* The r8 we trash here is the  */
	subs	pc, lr, #4		/* banked FIQ-mode r8. */
END(fiq_entry)

/*
 * Entry point for an Address Exception exception.
 * This is an arm26 exception that should never happen.
 */
ASENTRY_NP(addr_exception_entry)
	mov	r3, lr
	mrs	r2, spsr
	mrs	r1, cpsr
	adr	r0, Laddr_exception_msg
	b	_C_LABEL(panic)
Laddr_exception_msg:
	.asciz	"Address Exception CPSR=0x%08x SPSR=0x%08x LR=0x%08x\n"
	.balign	4
END(addr_exception_entry)

/*
 * Entry point for the system Reset vector.
 * This should never happen, so panic.
 */
ASENTRY_NP(reset_entry)
	mov	r1, lr
	adr	r0, Lreset_panicmsg
	b	_C_LABEL(panic)
	/* NOTREACHED */
Lreset_panicmsg:
	.asciz	"Reset vector called, LR = 0x%08x"
	.balign	4
END(reset_entry)

/*
 * page0 and page0_data -- An image of the ARM vectors which is copied to
 * the ARM vectors page (high or low) as part of CPU initialization.  The
 * code that does the copy assumes that page0_data holds one 32-bit word
 * of data for each of the predefined ARM vectors.  It also assumes that
 * page0_data follows the vectors in page0, but other stuff can appear
 * between the two.  We currently leave room between the two for some fiq
 * handler code to be copied in.
 */
	.global	_C_LABEL(page0), _C_LABEL(page0_data)

_C_LABEL(page0):
	ldr	pc, .Lreset_entry
	ldr	pc, .Lundefined_entry
	ldr	pc, .Lswi_entry
	ldr	pc, .Lprefetch_abort_entry
	ldr	pc, .Ldata_abort_entry
	ldr	pc, .Laddr_exception_entry
	ldr	pc, .Lirq_entry
.fiqv:	ldr	pc, .Lfiq_entry
	.space 256	/* room for some fiq handler code */

_C_LABEL(page0_data):
.Lreset_entry:		.word	reset_entry
.Lundefined_entry:	.word	undefined_entry
.Lswi_entry:		.word	swi_entry
.Lprefetch_abort_entry:	.word	prefetch_abort_entry
.Ldata_abort_entry:	.word	data_abort_entry
.Laddr_exception_entry:	.word	addr_exception_entry
.Lirq_entry:		.word	irq_entry
.Lfiq_entry:		.word	fiq_entry

/*
 * These items are used by the code in fiq.c to install what it calls the
 * "null" handler.  It's actually our default vector entry that just jumps
 * to the default handler which just disables FIQs and returns.
 */
	.global _C_LABEL(fiq_nullhandler_code), _C_LABEL(fiq_nullhandler_size)

_C_LABEL(fiq_nullhandler_code):
	.word	.fiqv
_C_LABEL(fiq_nullhandler_size):
	.word	4


