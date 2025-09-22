/*   $OpenBSD: eh_common.S,v 1.67 2025/05/17 08:28:14 aoyama Exp $	*/
/*
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * Copyright (c) 1996 Nivas Madhur
 * Copyright (c) 1998 Steve Murphree, Jr.
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

/*
 * NOTICE: This is not a standalone file.  To use it, define the PFSR_SAVE
 * macro (if supporting 88100 processors) and #include this file in your
 * port's eh.S:
 * #include <m88k/m88k/eh_common.S>
 */

/*
 * In the following discussion, references are made to:
 *    MC88100 - RISC MICROPROCESSOR USER'S MANUAL
 * (second edition). Reference in []s refer to section numbers.
 *
 * This discussion assumes that you are at least vaguely familiar with 88100
 * exception handling (chapter 6), the BSD kernel, and that you have a brain
 * (and use it while reading this).
 *
 * I also assume (and hope) that you're not offended by frequent misspellings.
 *
 * Jeffrey Friedl
 * jfriedl@rna.ncl.omron.co.jp
 * December, 1989
 *  -------------------------------------------------------------------
 *
 * EXCEPTIONS, INTERRUPTS, and TRAPS
 * ---------------------------------
 * This is the machine exception handler.
 * In the MC88100, various "conditions" cause an exception, where
 * processing momentarily jumps here to "service" the exception,
 * and then continues where it left off.
 *
 * There are a number of different types of exceptions.
 * For example, exception #6 is the privilege violation exception which
 * is raised when the user tries to execute a supervisor-only instruction.
 *
 * Exception #1 is the interrupt exception, and is raised when an
 * outside device raises the INT line on the CPU.  This happens,
 * for example, when the clock signals that it is time for a context
 * switch, or perhaps the disk drive signaling that some operation
 * is complete.
 *
 * Traps are also exceptions.  Traps are ways for user programs to request
 * kernel operations.  For example, "tcnd eq0, r0, 450" will raise
 * exception 450, the system call exception.
 *
 *
 * SERVICING AN EXCEPTION
 * -----------------------
 * When an exception occurs, each control register is saved in its
 * respective shadow register and execution continues from the
 * appropriate exception handler.  The exception handler must
 *      - save the context from the time of the exception
 *      - service the exception
 *      - restore the context (registers, etc)
 *      - pick up from where the exception occurred.
 *
 * The context is saved on a stack. Actually, in the user_state area
 * in the PCB if the exception happens in user mode.
 *
 * Servicing the exception is usually straightforward and in fact not dealt
 * with very much here.  Usually a C routine is called to handle it.
 * For example, when a privilege exception is raised, the routine that sends
 * an "illegal instruction" signal to the offending process is called.
 *
 * When the exception has been serviced, the context is restored from the
 * stack and execution resumes from where it left off.
 *
 * In more detail:
 *
 * Saving the exception-time context.
 * ---------------------------------
 *     In saving the exception-time context, we copy the shadow and general
 * purpose registers to memory.  Since one exception may occur while
 * servicing another, the memory used to save the exception-time context may
 * not be static (i.e. the same every time).  Thus, memory on a stack is set
 * aside for the exception frame (area where the exception-time context is
 * saved). The same stack is also used when C routines are called (to
 * service the exception).
 *
 *    Each process has a stack in kernel space (called the "kernel stack",
 * short for "process's kernel stack) as well as the user space stack.  When
 * entering the kernel from user space, the kernel stack is unused.  On this
 * stack we save the exception state and (most likely call a C routine to)
 * service the exception.
 *
 * Before servicing an exception, several issues must be addressed.
 *
 * 1) When an interrupt is recognized by the hardware, the data pipeline is
 *    allowed to clear.  However, if one of these data accesses faults (bad
 *    reference, or a reference to a page which needs to be swapped in), that
 *    reference, as well as any others in the pipeline at the time (at most
 *    three total) are left there, to be taken care of by the exception
 *    handler [6.4.1].  This involves swapping in the proper page and
 *    manually doing the appropriate load or store.
 *
 *    The other (at most, two other) data accesses that might have been in
 *    the pipeline must also be manually completed (even though they may not
 *    be at fault [yes, that's a bad pun, thank you]).
 *
 * 2) If any of the (at most three) uncompleted data access in the pipeline
 *    are loads (from memory to a register), then the bit for the destination
 *    register is set in the SSBR.  Since the hardware will never complete
 *    that load (since we do it manually), the hardware will never clear that
 *    SSBR bit.  Thus, we must clear it manually.  If this isn't done, the
 *    system will hang waiting for a bit to clear that will never.
 *
 * 3) If the exception is the privilege violation exception, the bounds
 *    check exception, or the misaligned access exception, the
 *    destination register bit in the SSBR may need to be cleared.
 *
 * 4) If the exception is one of the floating exceptions, then the
 *    destination register for that floating process won't be written,
 *    and the SSBR must be cleared explicitly.
 *
 * 5) The FPU must be enabled (as it is disabled by the exception processing
 *    hardware) and allowed to complete actions in progress. This is
 *    so that it may be used in the servicing of any instruction.
 *    When the FPU is being restarted, operations attempting to complete
 *    may themselves fault (raising another exception).
 *
 * More on Restarting the FPU
 * --------------------------
 *   The manual [section 6.4.3.4] gives only minor mention to this
 * rather complex task.  Before the FPU is restarted all SSBR bits are
 * cleared for actions that the exception handler completes (as mentioned
 * above) so that the SSBR is clear unless there are FPU operations that
 * have not actually been completed (and hence not written to the registers).
 * Also, all control registers (at least all those that we care about) are
 * saved to the stack exception frame before the FPU is restarted (this
 * is important... the reason comes later).
 *
 * The FPU is restarted by doing an rte to a trap-not-taken (the rte
 * actually enables the fpu because we ensure that the EPSR has the
 * FPU-enable bit on; the trap-not-taken ensures anything in the FPU
 * completes by waiting until scoreboard register is clear).
 *
 * At the time the FPU is restarted (the rte to the trap-not-taken) the FPU
 * can write to ANY of the general registers.  Thus, we must make sure that
 * all general registers (r1..r31) are in their pre-exception state so that
 * when saved to the exception frame after the FPU is enabled, they properly
 * reflect any changes made by the FPU in being restarted.
 *
 * Because we can't save the pointer to the exception frame in a general
 * register during the FPU restart (it could get overwritten by the FPU!),
 * we save it in a control register, SR3, during the restart.
 *
 * HOWEVER .....
 *
 * Because other uncompleted actions in the FPU may fault when the FPU is
 * restarted, a new exception may be raised during the restart. This may
 * happen recursively a number of times. Thus, during a restart, ANY register
 * whatsoever may be modified, including control registers.  Because of this
 * we must make sure that the exception handler preserves SR3 throughout
 * servicing an exception so that, if the exception had been raised during
 * an FPU restart, it is returned unmolested when control returns to the FPU
 * restart.
 *
 * Thus: if an exception is from kernel space, we MUST preserve SR3.
 * (if it from user space, no FPU-enable can be in progress and SR3 is
 *  unimportant).
 *
 * Now is a good time to recap SR1..SR3 usage:
 *   SR1 - CPU flags (exception handler flags)
 *   SR2 - generally free
 *   SR3 - free only if the exception is from user mode
 *
 * Once the FPU has been restarted, the general registers are saved to the
 * exception frame.  If the exception is not the interrupt exception,
 * interrupts are enabled and any faulted data accesses (see above) are
 * serviced.  In either case, the exception is then serviced (usually by
 * calling a C routine).  After servicing, any faulted data accesses are
 * serviced (if it had been the interrupt exception).  The context is then
 * restored and control returns to where the exception occurred.
 *
 */

#include "assym.h"

#include <machine/param.h>
#include <machine/asm.h>
#include <machine/trap.h>

/*
 * SR1 - CPU FLAGS REGISTER
 *
 * SR1 contains flags about the current CPU status.
 *
 * The bit FLAG_IGNORE_DATA_EXCEPTION indicates that any data exceptions
 * 	should be ignored (well, at least treated in a special way).
 * The bit FLAG_ENABLING_FPU indicates that the exception handler is
 * 	in the process of enabling the FPU (so that an exception can
 * 	be serviced).  This is needed because enabling the FPU can
 *	cause other exceptions to happen, and the whole system is
 *	in a rather precarious state and so special cautions must
 * 	be taken.
 */
#define FLAG_IGNORE_DATA_EXCEPTION	0
#define FLAG_ENABLING_FPU		1
#define FLAG_FROM_KERNEL		2

/* GENeral REGister OFFset into the E.F. (exception frame) */
#define GENREG_OFF(num)		(EF_R0 + (num) * 4)

/* Invoke a C function with 2 arguments */
#define	CALL(NAME, ARG1, ARG2) \
	or	%r2, %r0, ARG1; \
	bsr.n	NAME; \
	 or	%r3, %r0, ARG2

/* Invoke a function and return elsewhere */
/* CAREFUL: needs to have `RET' after the XCALL in memory */
#define	XCALL(NAME, RET) \
	bsr.n	NAME; \
	 addu	%r1, %r1, RET - . - 4

/*
 * Some registers used during the setting up of the new exception frame.
 * Don't choose r1, r30, or r31 for any of them.
 *
 * Also, if any are 'r2' or 'r3', be careful using with CALL above!
 */
#define	FLAGS			%r2
#define	TMP			%r3
#define	TMP2			%r10
#define	TMP3			%r11
#define	SAVE_TMP2(ef)		st	%r10, ef, GENREG_OFF(10)
#define	SAVE_TMP3(ef)		st	%r11, ef, GENREG_OFF(11)
#define	RESTORE_TMP2(ef)	ld	%r10, ef, GENREG_OFF(10)
#define	RESTORE_TMP3(ef)	ld	%r11, ef, GENREG_OFF(11)

/*
 * EF_SR3
 *   A place to save the exception-time SR3 from just after the
 *   time when an exception is raised until just after the FPU
 *   has been restarted.  This does not necessarily conflict with
 *   the general registers (though it can if you're not careful)
 *   and so we can use a spot later used to save a general register.
 */
#define	EF_SR3		GENREG_OFF(5)

	.text
	.align 3

/*
 *
 * #define PREP881x0(NAME, NUM, SSBR_STUFF, FLAG_CHECK)
 *
 * This is the "exception processing preparation" common to all exception
 * processing.  It is used in the following manner:
 *
 *	ASGLOBAL(foo_handler)
 *		PREP881x0("foo", 11, SSBR_Stuff, Precheck_Stuff)
 *		or	%r2, %r0, T_FOO_FAULT
 *		or	%r3, %r0, %r30
 *		XCALL(trapXXX, check_ast)
 *
 * This defines the exception handler for the "foo" exception.
 * The arguments are:
 * NAME
 *	String for debugging (more info later)
 * NUM
 *	The exception number [see the manual, Table 6-1]
 * SSBR_STUFF
 *	If the exception might leave some bits in the SSBR set,
 *	this should indicate how they are cleared.
 * FLAG_PRECHECK
 *	This is for the data access exception only. See it for
 *	more info.
 *
 * What's in between PREP881x0() and check_ast (usually a XCALL)
 * is the actual servicing of the interrupt.  During this time, any
 * register may be used freely as they've all been saved in the
 * exception frame (which is pointed to by r30).
 */

#ifdef M88100
#define PREP88100(NAME, NUM, SSBR_STUFF, FLAG_PRECHECK) \
	xcr	FLAGS, FLAGS, SR1			; \
	FLAG_PRECHECK					  \
	/* the bsr later clobbers r1, so save now */	  \
	stcr	%r1, SR2	/* r1 now free */	; \
	/* set or clear the FLAG_FROM_KERNEL bit */	  \
	ldcr	%r1, EPSR				; \
	bb0.n	PSR_SUPERVISOR_MODE_BIT, %r1, 1f	; \
	 clr	FLAGS, FLAGS, 1<FLAG_FROM_KERNEL>	; \
	set	FLAGS, FLAGS, 1<FLAG_FROM_KERNEL>	; \
	/* get a stack (exception frame) */		  \
1:	bsr	m88100_setup_phase_one			; \
	/* TMP2 now free -- use to set EF_VECTOR */	  \
	or	TMP2, %r0, NUM				; \
	st	TMP2, %r31, EF_VECTOR			; \
	/* Clear any bits in the SSBR (held in TMP) */	  \
	/* SSBR_STUFF may be empty, though.         */	  \
	SSBR_STUFF					  \
	/* call setup_phase_two to restart the FPU  */	  \
	/* and to save all general registers.       */	  \
	bsr	m88100_setup_phase_two
#endif

#ifdef M88110
#define PREP88110(NAME, NUM, FLAG_PRECHECK) \
	xcr	FLAGS, FLAGS, SR1			; \
	FLAG_PRECHECK					  \
	/* the bsr later clobbers r1, so save now */	; \
	stcr	%r1, SR2	/* r1 now free */	; \
	/* set or clear the FLAG_FROM_KERNEL bit */	; \
	ldcr	%r1, EPSR 				; \
	clr	FLAGS, FLAGS, 1<FLAG_FROM_KERNEL> 	; \
	bb0	PSR_SUPERVISOR_MODE_BIT, %r1, 1f 	; \
	set	FLAGS, FLAGS, 1<FLAG_FROM_KERNEL>	; \
	/* get a stack and an exception frame */	; \
1:	bsr	m88110_setup_phase_one			; \
	/* TMP2 now free -- use to set EF_VECTOR */	; \
	or	TMP2, %r0, NUM				; \
	/* call setup_phase_two to save all general */	; \
	/* registers.                               */	; \
	st	TMP2, %r30, EF_VECTOR			; \
	bsr	m88110_setup_phase_two
#endif

/* Some defines for use with PREP88100() */
#define	Clear_SSBR_Dest \
	bsr	clear_dest_ssbr_bit;
#define	M88100_Data_Precheck \
	bb1.n	FLAG_IGNORE_DATA_EXCEPTION, FLAGS, \
	    m88100_ignore_data_exception;
#define	M88110_Data_Precheck \
	bb1.n	FLAG_IGNORE_DATA_EXCEPTION, FLAGS, \
	    m88110_ignore_data_exception;

#ifdef M88100
/*
 * 88100 exception handlers
 */

/* unknown exception handler */
GLOBAL(unknown_handler)
	PREP88100("unknown", 0,,)
	or	%r2, %r0, T_UNKNOWNFLT
	or	%r3, %r0, %r30
	XCALL(m88100_trap, check_ast)

/* interrupt exception handler */
GLOBAL(interrupt_handler)
	PREP88100("interrupt", 1,,)
	or	%r2, %r0, %r30
	XCALL(interrupt, check_ast)

/* instruction access exception handler */
GLOBAL(instruction_access_handler)
	PREP88100("inst", 2,,)
	or	%r2, %r0, T_INSTFLT
	or	%r3, %r0, %r30
	XCALL(m88100_trap, check_ast)

/*
 * data access exception handler --
 *  See badaddr() below for info about Data_Precheck.
 */
GLOBAL(data_exception_handler_bootstrap)
	PREP88100("data", 3,, M88100_Data_Precheck)
	/* No need to call m88100_trap(T_DATAFLT) as PREP will do this for us */
	br	check_ast
GLOBAL(data_exception_handler)
	PREP88100("data", 3,,)
	/* No need to call m88100_trap(T_DATAFLT) as PREP will do this for us */
	br	check_ast

/* misaligned access exception handler */
GLOBAL(misaligned_handler)
	PREP88100("misalign", 4, Clear_SSBR_Dest,)
	or	%r2, %r0, T_MISALGNFLT
	or	%r3, %r0, %r30
	XCALL(m88100_trap, check_ast)

/* unimplemented opcode exception handler */
GLOBAL(unimplemented_handler)
	PREP88100("unimp", 5,,)
	or	%r2, %r0, T_ILLFLT
	or	%r3, %r0, %r30
	XCALL(m88100_trap, check_ast)

/*
 * Some versions of the chip have a bug whereby false privilege
 * violation exceptions are raised. If the valid bit in the SXIP is clear,
 * it is false.  If so, just return.  The code before PREP handles this....
 */
GLOBAL(privilege_handler)
	stcr	%r1, SR2	/* hold r1 for a moment */
	ldcr	%r1, SXIP	/* look at the sxip... valid bit set? */
	bb1.n	RTE_VALID_BIT, %r1, 1f	/* skip over if a valid exception */
	 ldcr	%r1, SR2	/* restore r1 */
	RTE
1:	PREP88100("privilege", 6, Clear_SSBR_Dest,)
	or	%r2, %r0, T_PRIVINFLT
	or	%r3, %r0, %r30
	XCALL(m88100_trap, check_ast)

/* bounds checking exception handler */
GLOBAL(bounds_handler)
	PREP88100("bounds", 7, Clear_SSBR_Dest,)
	or	%r2, %r0, T_BNDFLT
	or	%r3, %r0, %r30
	XCALL(m88100_trap, check_ast)

/* integer divide-by-zero exception handler */
GLOBAL(divide_handler)
	PREP88100("divide", 8, Clear_SSBR_Dest,)
	or	%r2, %r0, T_ZERODIV
	or	%r3, %r0, %r30
	XCALL(m88100_trap, check_ast)

/* integer overflow exception handler */
GLOBAL(overflow_handler)
	PREP88100("overflow", 9,,)
	or	%r2, %r0, T_OVFFLT
	or	%r3, %r0, %r30
	XCALL(m88100_trap, check_ast)

/* Floating-point precise handler */
#define	FPp_SSBR_STUFF \
	bsr	clear_FPp_ssbr_bit;
GLOBAL(fp_precise_handler)
	PREP88100("FPU precise", 114, FPp_SSBR_STUFF,)
	or	%r2, %r0, T_FPEPFLT
	or	%r3, %r0, %r30
	XCALL(m88100_trap, check_ast)

/* Floating-point imprecise handler */
#define	FPi_SSBR_STUFF \
	bsr	clear_FPi_ssbr_bit;
GLOBAL(fp_imprecise_handler)
	PREP88100("FPU imprecise", 115, FPi_SSBR_STUFF,)
	or	%r2, %r0, T_FPEIFLT
	or	%r3, %r0, %r30
	XCALL(m88100_trap, check_ast)

/* trap 450: system calls */
GLOBAL(syscall_handler)
	PREP88100("syscall", 450,,)
	ld	%r2, %r30, GENREG_OFF(13)
	or	%r3, %r0, %r30
	XCALL(m88100_syscall, check_ast)

/* trap 451: cache flush (necessary for trampolines) */
GLOBAL(cache_flush_handler)
	PREP88100("cache_flush", 451,,)
	or	%r2, %r0, %r30
	XCALL(cache_flush, check_ast)

GLOBAL(sigsys)
	PREP88100("sigsys", 501,,)
	or	%r2, %r0, T_SIGSYS
	or	%r3, %r0, %r30
	XCALL(m88100_trap, check_ast)

GLOBAL(stepbpt)
	PREP88100("stepbpt", 504,,)
	or	%r2, %r0, T_STEPBPT
	or	%r3, %r0, %r30
	XCALL(m88100_trap, check_ast)

GLOBAL(userbpt)
	PREP88100("userbpt", 511,,)
	or	%r2, %r0, T_USERBPT
	or	%r3, %r0, %r30
	XCALL(m88100_trap, check_ast)

#ifdef DDB
GLOBAL(break)
	PREP88100("break", 130,,)
	or	%r2, %r0, T_KDB_BREAK
	or	%r3, %r0, %r30
	XCALL(m88100_trap, check_ast)

GLOBAL(trace)
	PREP88100("trace", 131,,)
	or	%r2, %r0, T_KDB_TRACE
	or	%r3, %r0, %r30
	XCALL(m88100_trap, check_ast)

GLOBAL(entry)
	PREP88100("kdb", 132,,)
	or	%r2, %r0, T_KDB_ENTRY
	or	%r3, %r0, %r30
	XCALL(m88100_trap, check_ast)
#endif

/*
 * The error exception and reset exception handler.
 *
 * The error exception is raised when any other non-trap exception is raised
 * while shadowing is off. This is Bad News.
 *
 * The reset exception is raised when the RST signal is asserted (machine
 * is reset), the value of VBR is changed after exceptions are enabled,
 * or when a jmp, br/bsr to addr 0 (accidents do happen :-)
 * To tell the difference, you should check the value of r1 and the valid
 * bit of SXIP.
 * Upon a real reset, VBR is set to zero (0), so code must be at addr 0
 * to handle it!!!
 *
 * The shadow registers are not valid in this case (shadowing was off, if this
 * was an error exception, and may not be on, if this was a reset exception).
 * R1-R31 may be interesting though, so we'll save them.
 *
 * We'll not worry about trashing r26-29 here,
 * since they aren't generally used.
 */
GLOBAL(error_handler)
	br.n	1f
	 or	%r29, %r0, 10
GLOBAL(reset_handler)
	or	%r29, %r0, 0
1:
	or	%r26, %r0,  %r31	/* save old stack */
	or.u	%r31, %r0,  %hi16(initstack_end)
	or	%r31, %r31, %lo16(initstack_end)

#ifdef DEBUG
	/* zero the stack, so we'll know what we're lookin' at */
	or.u	%r27, %r0,  %hi16(initstack)
	or	%r27, %r27, %lo16(initstack)
1:	cmp	%r28, %r27, %r31
	bb1	ge,   %r28, 2f	/* branch if at the end of the stack */
	st	%r0,  %r0,  %r27
	br.n	1b
	 addu	%r27, %r27, 4	/* bump up */
2:	/* stack has been cleared */
#endif

	/* ensure that stack is 8-byte aligned */
	clr	%r31, %r31, 3<0>	/* round down to 8-byte boundary */

	/* create exception frame on stack */
	subu	%r31, %r31, TRAPFRAME_SIZEOF	/* r31 now our E.F. */

	/* save old R31 and other R registers */
	st.d	%r0 , %r31, GENREG_OFF(0)
	st.d	%r2 , %r31, GENREG_OFF(2)
	st.d	%r4 , %r31, GENREG_OFF(4)
	st.d	%r6 , %r31, GENREG_OFF(6)
	st.d	%r8 , %r31, GENREG_OFF(8)
	st.d	%r10, %r31, GENREG_OFF(10)
	st.d	%r12, %r31, GENREG_OFF(12)
	st.d	%r14, %r31, GENREG_OFF(14)
	st.d	%r16, %r31, GENREG_OFF(16)
	st.d	%r18, %r31, GENREG_OFF(18)
	st.d	%r20, %r31, GENREG_OFF(20)
	st.d	%r22, %r31, GENREG_OFF(22)
	st.d	%r24, %r31, GENREG_OFF(24)
	st	%r30, %r31, GENREG_OFF(30)
	st	%r26, %r31, GENREG_OFF(31)

	/* save shadow registers (are OLD if error_handler, though) */
	ldcr	%r10, EPSR
	st	%r10, %r31, EF_EPSR
	ldcr	%r10, SXIP
	st	%r10, %r31, EF_SXIP
	ldcr	%r10, SNIP
	st	%r10, %r31, EF_SNIP
	ldcr	%r10, SR1
	st	%r10, %r31, EF_FLAGS
	ldcr	%r10, SFIP
	st	%r10, %r31, EF_SFIP
	ldcr	%r10, SSBR
	st	%r10, %r31, EF_SSBR
	stcr	%r0,  SSBR	/* won't want shadow bits bothering us later */

	ldcr	%r10, DMT0
	st	%r10, %r31, EF_DMT0
	ldcr	%r11, DMD0
	st	%r11, %r31, EF_DMD0
	ldcr	%r12, DMA0

	st	%r12, %r31, EF_DMA0
	ldcr	%r10, DMT1
	st	%r10, %r31, EF_DMT1
	FLUSH_PIPELINE
	ldcr	%r11, DMD1
	st 	%r11, %r31, EF_DMD1
	ldcr	%r12, DMA1
	st	%r12, %r31, EF_DMA1

	ldcr	%r10, DMT2
	st	%r10, %r31, EF_DMT2
	ldcr	%r11, DMD2
	st	%r11, %r31, EF_DMD2
	ldcr	%r12, DMA2
	st	%r12, %r31, EF_DMA2

	/* shove sr2 into EF_FPLS1 */
	ldcr	%r10, SR2
	st	%r10, %r31, EF_FPLS1

	/* shove sr3 into EF_FPHS2 */
	ldcr	%r10, SR3
	st	%r10, %r31, EF_FPHS2

	/* save error vector */
	st	%r29, %r31, EF_VECTOR

	/*
	 * Cheap way to enable FPU and start shadowing again.
	 */
	ldcr	%r10, PSR
	clr	%r10, %r10, 1<PSR_FPU_DISABLE_BIT>	/* enable the FPU */
	clr	%r10, %r10, 1<PSR_SHADOW_FREEZE_BIT>	/* and shadowing */
	stcr	%r10, PSR
	FLUSH_PIPELINE

	/* put pointer to regs into r30... r31 will become a simple stack */
	or	%r30, %r31, %r0

	subu	%r31, %r31, 0x10	/* make some breathing space */
	st	%r30, %r31, 0x0c	/* store frame pointer on the stack */
#ifdef DDB
	st	%r30, %r31, 0x08	/* store again for the debugger to recognize */
	or.u	%r20, %r0,  %hi16(0x87654321)
	or	%r20, %r20, %lo16(0x87654321)
	st	%r20, %r31, 0x04
	st	%r20, %r31, 0x00
#endif

	bsr.n	error_fatal
	 or	%r2, %r0, %r30

	/* turn interrupts back on */
	ldcr	%r1, PSR
	clr	%r1, %r1, 1<PSR_INTERRUPT_DISABLE_BIT>
	stcr	%r1, PSR
	FLUSH_PIPELINE

1:
	br	1b
	/* NOTREACHED */
#endif	/* M88100 */

/*
 * This is part of badaddr (below).
 */
#ifdef M88100
ASLOCAL(m88100_ignore_data_exception)
	/*
	 * SR1: previous FLAGS reg
	 * SR2: free
	 * SR3: must preserve
	 * FLAGS: CPU status flags
	 */
	xcr	FLAGS, FLAGS, SR1	/* replace SR1, FLAGS */

	/*
	 * For more info, see badaddr() below.
	 *
	 * We just want to jump to "badaddr__return_nonzero" below.
	 *
	 * We don't worry about trashing r2 here because we're
	 * jumping back to the function badaddr() where we're allowed
	 * to blast r2..r9 as we see fit.
	 */

	/* the "+2" below is to set the VALID bit. */
	or.u	%r2, %r0, %hi16(badaddr__return_nonzero + 2)
	or	%r2, %r2, %lo16(badaddr__return_nonzero + 2)
	stcr	%r2, SNIP	/* Make it the next instruction to execute */
	addu	%r2, %r2, 4
	stcr	%r2, SFIP	/* and the next one after that, too. */
	stcr	%r0, SSBR	/* make the scoreboard happy. */
	RTE
#endif /* M88100 */

#ifdef M88110
/*
 * This is part of badaddr (below).
 */
ASLOCAL(m88110_ignore_data_exception)
	/*
	 * SR1: previous FLAGS reg
	 * SR2: free
	 * SR3: must preserve
	 * FLAGS: CPU status flags
	 */
	xcr	FLAGS, FLAGS, SR1	/* replace SR1, FLAGS */

	/*
	 * For more info, see badaddr() below.
	 *
	 * We just want to jump to "badaddr__return_nonzero" below.
	 *
	 * We don't worry about trashing R2 here because we're
	 * jumping back to the function badaddr() where we're allowed
	 * to blast r2..r9 as we see fit.
	 */

	or.u	%r2, %r0, %hi16(badaddr__return_nonzero)
	or	%r2, %r2, %lo16(badaddr__return_nonzero)
	stcr	%r2, EXIP	/* Make it the next instruction to execute */
	stcr	%r0, DSR	/* Clear exception status */
	RTE
#endif /* M88110 */

/*
 * extern int badaddr(vaddr_t addr, int len)
 *
 * Returns true (non-zero) if the given LEN bytes starting at ADDR are
 * not all currently accessible by the kernel.
 *
 * If all LEN bytes starting at ADDR are accessible, zero is returned.
 *
 * Len may be be 1, 2, 4 or 8.
 *
 * This is implemented by setting a special flag in SR1 before trying to access
 * the given address. If a data access exception is raised, the address
 * is inaccessible. The exception handler will notice the special CPU flag
 * and not try to swap the address in. Rather, it will return to
 * "badaddr__return_nonzero" in this routine so that we may return non-zero
 * to the calling routine.
 *
 * If no fault is raised, we continue to where we return zero to the calling
 * routine (after removing the special CPU flag).
 */

GLOBAL(badaddr)
	/*
	 * Disable interrupts ... don't want a context switch while we're
	 * doing this! Also, save the old PSR in R8 to restore later.
	 */
	ldcr	%r8, PSR
	set	%r4, %r8, 1<PSR_INTERRUPT_DISABLE_BIT>
	stcr	%r4, PSR
	FLUSH_PIPELINE

	ldcr	%r5, SR1
	set	%r5, %r5, 1<FLAG_IGNORE_DATA_EXCEPTION>
	/* resetting r5 to SR1 done in the delay slot below. */

	/* make sure the upper 28 bits of the size are zero... */
	ext	%r6, %r3, 0<4>
	bcnd.n	ne0, %r6, badaddr__return_nonzero
	 stcr	%r5, SR1

	or.u	%r6, %r0, %hi16(badaddr_switch)
	or	%r6, %r6, %lo16(badaddr_switch)
	lda	%r3, %r6[%r3]
	jmp	%r3

ASLOCAL(badaddr_switch)
	br	badaddr__return_nonzero
	br	badaddr__b
	br	badaddr__h
	br	badaddr__return_nonzero
	br	badaddr__w
	br	badaddr__return_nonzero
	br	badaddr__return_nonzero
	br	badaddr__return_nonzero
	br	badaddr__d
	br	badaddr__return_nonzero
	br	badaddr__return_nonzero
	br	badaddr__return_nonzero
	br	badaddr__return_nonzero
	br	badaddr__return_nonzero
	br	badaddr__return_nonzero
	br	badaddr__return_nonzero

	/*
	 * The load attempts below will either fault or not. If they fault,
	 * execution will go to data_access_handler, then to
	 * ignore_data_exception, then to badaddr__return_nonzero, which
	 * will return to the calling function.
	 * If there is no fault, execution will just continue as normal.
	 */

ASLOCAL(badaddr__b)
	FLUSH_PIPELINE
	ld.b	%r6, %r2, 0
	FLUSH_PIPELINE
	br.n	badaddr__return
	 or	%r2, %r0, %r0

ASLOCAL(badaddr__h)
	/* It's a bad address if it's misaligned. */
	bb1	0, %r2, badaddr__return_nonzero

	FLUSH_PIPELINE
	ld.h	%r6, %r2, 0
	FLUSH_PIPELINE
	br.n	badaddr__return
	 or	%r2, %r0, %r0

ASLOCAL(badaddr__w)
	/* It's a bad address if it's misaligned. */
	bb1	0, %r2, badaddr__return_nonzero
	bb1	1, %r2, badaddr__return_nonzero

	FLUSH_PIPELINE
	ld	%r6, %r2, 0
	FLUSH_PIPELINE
	br.n	badaddr__return
	 or	%r2, %r0, %r0	/* indicate a zero (address not bad) return.*/

ASLOCAL(badaddr__d)
	/* It's a bad address if it's misaligned. */
	bb1	0, %r2, badaddr__return_nonzero
	bb1	1, %r2, badaddr__return_nonzero
	bb1	2, %r2, badaddr__return_nonzero

	FLUSH_PIPELINE
	ld.d	%r6, %r2, 0
	FLUSH_PIPELINE
	br.n	badaddr__return
	 or	%r2, %r0, %r0	/* indicate a zero (address not bad) return.*/

ASLOCAL(badaddr__return_nonzero)
	or	%r2, %r0, 1
	/* FALLTHROUGH */

ASLOCAL(badaddr__return)
	ldcr	%r4, SR1
	clr	%r4, %r4, 1<FLAG_IGNORE_DATA_EXCEPTION>
	stcr	%r4, SR1

	/*
	 * Restore the PSR to what it was before.
	 * The only difference is that we might be enabling interrupts
	 * (which we turned off above).  If interrupts were already off,
	 * we do not want to turn them on now, so we just restore from
	 * where we saved it.
	 */
	stcr	%r8, PSR
	FLUSH_PIPELINE
	jmp	%r1

#ifdef M88100
ASLOCAL(m88100_setup_phase_one)
	/*
	 * SR1: saved copy of exception-time register now holding FLAGS
	 * SR2: saved copy of exception-time r1
	 * SR3: must be preserved .. may be the exception-time stack
	 * r1: return address to calling exception handler
	 * FLAGS: CPU status flags
	 *
	 * immediate goal:
	 *   Decide where we're going to put the exception frame.
	 *   Might be at the end of R31, SR3, or the process pcb.
	 */

	/* Check if we are coming in from a FPU restart exception.
	   If so, the pcb will be in SR3 */
	NOP
	xcr	%r1, %r1, SR2
	NOP
	NOP
	NOP

	bb1	FLAG_ENABLING_FPU, FLAGS, m88100_use_SR3_pcb
	/* are we coming in from user mode? If so, pick up process pcb */
	bb0	FLAG_FROM_KERNEL, FLAGS, m88100_pickup_stack

	/* Interrupt in kernel mode, not FPU restart */
	/*
	 * SR1: saved copy of exception-time register now holding FLAGS
	 * SR2: return address to the calling exception handler
	 * SR3: must be preserved; may be important for other exceptions
	 * FLAGS: CPU status flags
	 *
	 * immediate goal:
	 *   We're already on the kernel stack, but not having
	 *   needed to use SR3. We can just make room on the
	 *   stack (r31) for our exception frame.
	 */
	subu	%r31, %r31, TRAPFRAME_SIZEOF	/* r31 now our E.F. */
	st	FLAGS,%r31, EF_FLAGS		/* save flags */
	st	%r1,  %r31, GENREG_OFF(1)	/* save prev. r1 (now free)*/

	ldcr	%r1,  SR3			/* save previous SR3 */
	st	%r1,  %r31, EF_SR3

	addu	%r1,  %r31, TRAPFRAME_SIZEOF	/* save previous r31 */
	br.n	m88100_have_pcb
	 st 	%r1,  %r31, GENREG_OFF(31)

ASLOCAL(m88100_use_SR3_pcb)
	/*
	 * SR1: saved copy of exception-time register now holding FLAGS
	 * SR2: return address to the calling exception handler
	 * SR3: must be preserved; exception-time stack pointer
	 * FLAGS: CPU status flags
	 *
	 * immediate goal:
	 *   An exception occurred while enabling the FPU. Since r31
	 *   is the user's r31 while enabling the FPU, we had put
	 *   our pcb pointer into SR3, so make room from
	 *   there for our stack pointer.
	 *   We need to check if SR3 is the old stack pointer or the
	 *   pointer off to the user pcb. If it pointing to the user
	 *   pcb, we need to pick up the kernel stack. Otherwise
	 *   we need to allocate a frame upon it.
	 *   We look at the EPSR to see if it was from user mode
	 *   Unfortunately, we have no registers free at the moment
	 *   But we know register 0 in the pcb frame will always be
	 *   zero, so we can use it as scratch storage.
	 */
	xcr	%r30, %r30, SR3			/* r30 = old exception frame */
	st	%r1,  %r30, GENREG_OFF(0)	/* free up r1 */
	ld	%r1,  %r30, EF_EPSR		/* get back the epsr */
	bb0.n	PSR_SUPERVISOR_MODE_BIT, %r1, 1f/* if user mode */
	 ld	%r1,  %r30, GENREG_OFF(0)	/* restore r1 */
	/* we were in kernel mode - dump frame upon the stack */
	st	%r0,  %r30, GENREG_OFF(0)	/* repair old frame */
	subu	%r30, %r30, TRAPFRAME_SIZEOF	/* r30 now our E.F. */
	st	FLAGS,%r30, EF_FLAGS		/* save flags */
	st	%r1,  %r30, GENREG_OFF(1)	/* save prev r1 (now free) */

	st	%r31, %r30, GENREG_OFF(31)	/* save previous r31 */
	or	%r31, %r0,  %r30		/* make r31 our pointer. */
	addu	%r30, %r30, TRAPFRAME_SIZEOF	/* r30 now has previous SR3 */
	st	%r30, %r31, EF_SR3		/* save previous SR3 */
	br.n	m88100_have_pcb
	 xcr	%r30, %r30, SR3			/* restore r30 */
1:
	/* we took an exception while restarting the FPU from user space.
	 * Consequently, we never picked up a stack. Do so now.
	 * R1 is currently free (saved in the exception frame pointed at by
	 * r30) */
	ldcr	%r1,  CPU
	ld	%r1,  %r1,  CI_CURPCB
	addu	%r1,  %r1,  USPACE - TRAPFRAME_SIZEOF
	st	FLAGS,%r1,  EF_FLAGS		/* store flags */
	st	%r31, %r1,  GENREG_OFF(31)	/* store r31 - now free */
	st	%r30, %r1,  EF_SR3		/* store old SR3 (pcb) */
	or	%r31, %r1,  %r0			/* make r31 our exception fp */
	ld	%r1,  %r30, GENREG_OFF(0)	/* restore old r1 */
	st	%r0,  %r30, GENREG_OFF(0)	/* repair that frame */
	st	%r1,  %r31, GENREG_OFF(1)	/* store r1 */
	br.n	m88100_have_pcb
	 xcr	%r30, %r30, SR3			/* restore r30 */

ASLOCAL(m88100_pickup_stack)
	/*
	 * SR1: saved copy of exception-time register now holding FLAGS
	 * SR2: return address to the calling exception handler
	 * SR3: free
	 * FLAGS: CPU status flags
	 *
	 * immediate goal:
	 *   Since we're servicing an exception from user mode, we
	 *   know that SR3 is free.  We use it to free up a temp.
	 *   register to be used in getting the process pcb
	 */
	stcr	%r31, SR3	/* save previous r31 */

	/* switch to the process kernel stack. */
	ldcr	%r31, CPU
	ld	%r31, %r31, CI_CURPCB
	addu	%r31, %r31, PCB_USER_STATE	/* point to user save area */

	/*
	 * WARNING! Using pcb->user_state as the exception frame
	 * AND stack pointer, means we can not afford using the stack
	 * until we have saved enough and can go back to the top of the u area,
	 * after the FPU is enabled.
	 */

	st	FLAGS,%r31, EF_FLAGS		/* save flags */
	st	%r1,  %r31, GENREG_OFF(1)	/* save prev. r1 (now free) */
	ldcr	%r1,  SR3			/* save previous r31 */
	st	%r1,  %r31, GENREG_OFF(31)
	/* FALLTHROUGH */

ASLOCAL(m88100_have_pcb)
	/*
	 * SR1: saved copy of exception-time register now holding FLAGS
	 * SR2: return address to the calling exception handler
	 * SR3: free
	 * r1:  free
	 * FLAGS: CPU status flags
	 * r31: our exception frame
	 * Valid in the exception frame:
	 *   Exception-time r1, r31, FLAGS.
	 *   Exception SR3, if appropriate.
	 *
	 * immediate goal:
	 *   Save the shadow registers that need to be saved to
	 *   the exception frame.
	 */
	stcr	TMP, SR3	/* free up TMP, TMP2, TMP3 */
	SAVE_TMP2(%r31)
	SAVE_TMP3(%r31)

	/* save some exception-time registers to the exception frame */
	ldcr	TMP,  EPSR
	st	TMP,  %r31, EF_EPSR
	ldcr	TMP3, SNIP
	st	TMP3, %r31, EF_SNIP
	ldcr	TMP2, SFIP
	st	TMP2, %r31, EF_SFIP
	/* get and store the cpu_info pointer */
	ldcr	TMP,  CPU
	st	TMP,  %r31, EF_CPU

	/*
	 * Save Pbus fault status register from data and inst CMMU.
	 * We can afford calling a function since r1 is safe to use here.
	 */
GLOBAL(pfsr_save)
	PFSR_SAVE
ASLOCAL(pfsr_done)
	
	ldcr	TMP,  SSBR
	ldcr	TMP2, SXIP
	ldcr	TMP3, DMT0
	st	TMP2, %r31, EF_SXIP

/*
 * The above shadow registers are obligatory for any and all
 * exceptions.  Now, if the data access pipeline is not clear,
 * we must save the DMx shadow registers, as well as clear
 * the appropriate SSBR bits for the destination registers of
 * loads or xmems.
 */
	bb0.n	DMT_VALID_BIT, TMP3, 8f
	 st	TMP3, %r31, EF_DMT0

	ldcr	TMP2, DMT1
	ldcr	TMP3, DMT2
	st	TMP2, %r31, EF_DMT1
	st	TMP3, %r31, EF_DMT2

	ldcr	TMP2, DMA0
	ldcr	TMP3, DMA1
	st	TMP2, %r31, EF_DMA0
	st	TMP3, %r31, EF_DMA1

	ldcr	TMP2, DMA2
	ldcr	TMP3, DMD0
	st	TMP2, %r31, EF_DMA2
	st	TMP3, %r31, EF_DMD0

	FLUSH_PIPELINE
	ldcr	TMP2, DMD1
	ldcr	TMP3, DMD2
	st	TMP2, %r31, EF_DMD1
	st	TMP3, %r31, EF_DMD2

	/* make sure an exception in fpu_enable will not see our DMT0 */
	stcr	%r0,   DMT0

	/*
 	 * need to clear "appropriate" bits in the SSBR before
 	 * we restart the FPU
 	 */
	ld	TMP2, %r31, EF_DMT0
	bb1	DMT_LOCK_BIT,  TMP2, 1f
	bb1	DMT_WRITE_BIT, TMP2, 2f
1:
	extu	TMP2, TMP2, DMT_DREG_WIDTH <DMT_DREG_OFFSET>
	set	TMP2, TMP2, 1<5>
	clr	TMP,  TMP,  TMP2
2:
	ldcr	TMP2, DMT1
	bb0	DMT_VALID_BIT, TMP2, 4f
	bb1	DMT_LOCK_BIT,  TMP2, 3f
	bb1	DMT_WRITE_BIT, TMP2, 4f
3:
	extu	TMP2, TMP2, DMT_DREG_WIDTH <DMT_DREG_OFFSET>
	set	TMP2, TMP2, 1<5>
	clr	TMP,  TMP,  TMP2
4:
	ldcr	TMP2, DMT2
	bb0	DMT_VALID_BIT, TMP2, 8f
	bb1	DMT_LOCK_BIT,  TMP2, 5f
	bb1	DMT_WRITE_BIT, TMP2, 8f
	bb1	DMT_DOUBLE_BIT,TMP2, 6f
5:
	extu	TMP2, TMP2, DMT_DREG_WIDTH <DMT_DREG_OFFSET>
	br.n	7f
	 set	TMP2, TMP2, 1<5>	/* single */
6:
	extu	TMP2, TMP2, DMT_DREG_WIDTH <DMT_DREG_OFFSET>
	set	TMP2, TMP2, 1<6>	/* double */
7:
	clr	TMP,  TMP,  TMP2
8:
	/*
	 * SR1: saved copy of exception-time register now holding FLAGS
	 * SR2: return address to the calling exception handler
	 * SR3: saved TMP
	 * r1:  free
	 * TMP: possibly revised SSBR
	 * TMP2: free
	 * TMP3: free
	 * FLAGS: CPU status flags
	 * r31: exception frame
	 * Valid in the exception frame:
	 *   Exception-time r1, r31, FLAGS.
	 *   Exception-time TMP2, TMP3.
	 *   Exception-time epsr, sfip, snip, sxip.
	 *   Dmt0.
	 *   Other data pipeline control registers, if appropriate.
	 *   Exception SR3, if appropriate.
	 */
	ldcr	%r1,  SR2
	jmp	%r1	/* allow the handler to clear more SSBR bits */

ASLOCAL(clear_FPi_ssbr_bit)
	/*
	 * Clear floating point imprecise ssbr bits.
	 * Also, save appropriate FPU control registers to the E.F.
	 *
	 * r1:  return address to calling exception handler
	 * TMP: (possibly) revised ssbr
	 * TMP2: free
	 * TMP3: free
	 */
	fldcr	TMP2, FPSR
	fldcr	TMP3, FPCR
	st	TMP2, %r31, EF_FPSR
	st	TMP3, %r31, EF_FPCR

	fldcr	TMP2, FPECR
	fldcr	TMP3, FPRH
	st	TMP2, %r31, EF_FPECR
	st	TMP3, %r31, EF_FPRH

	fldcr 	TMP2, FPIT
	fldcr	TMP3, FPRL
	st	TMP2, %r31, EF_FPIT
	st	TMP3, %r31, EF_FPRL

	/*
	 * We only need clear the bit in the SSBR for the
	 * 2nd reg of a double result [see section 6.8.5]
	 */
#define FPIT_SIZE_BIT   10
	bb0	FPIT_SIZE_BIT, TMP2, 1f
	extu	TMP2, TMP2, 5<0>	/* get the reg. */
	set	TMP2, TMP2, 1<6>	/* set width */
	clr	TMP,  TMP,  TMP2
1:
	jmp	%r1


ASLOCAL(clear_FPp_ssbr_bit)
	/*
	 * Clear floating point precise ssbr bits.
	 * Also, save appropriate FPU control registers to the E.F.
	 *
	 * r1:  return address to calling exception handler
	 * TMP: (possibly) revised ssbr
	 * TMP2: free
	 * TMP3: free
	 */
	fldcr	TMP2, FPSR
	fldcr	TMP3, FPCR
	st	TMP2, %r31, EF_FPSR
	st	TMP3, %r31, EF_FPCR

	fldcr	TMP3, FPECR
	st	TMP3, %r31, EF_FPECR
	fldcr	TMP2, FPHS1
	fldcr	TMP3, FPHS2
	st	TMP2, %r31, EF_FPHS1
	st	TMP3, %r31, EF_FPHS2

	fldcr	TMP2, FPLS1
	fldcr	TMP3, FPLS2
	st	TMP2, %r31, EF_FPLS1
	st	TMP3, %r31, EF_FPLS2

	fldcr	TMP2, FPPT
	st	TMP2, %r31, EF_FPPT

#define FPPT_SIZE_BIT   5
	bb1.n	FPPT_SIZE_BIT, TMP2, 2f
	 extu	TMP3, TMP2, 5<0>	/* get FP operation dest reg */
	br.n	3f
	 set	TMP3, TMP3, 1<5>	/* size=1 - clear one bit for float */
2:
	set	TMP3, TMP3, 1<6>	/* size=2 - clear two bits for double */
3:
	jmp.n	%r1
	 clr	TMP,  TMP,  TMP3	/* clear bit(s) in ssbr. */


ASLOCAL(clear_dest_ssbr_bit)
	/*
	 * There are various cases where an exception can leave the
	 * destination register's bit in the SB set.
	 * Examples:
	 *   misaligned or privilege exception on a LD or XMEM
	 *   DIV or DIVU by zero.
	 *
	 * I think that if the instruction is LD.D, then two bits must
	 * be cleared.
	 *
	 * Even though there are a number of instructions/exception
	 * combinations that could fire this code up, it's only required
	 * to be run for the above cases.  However, I don't think it'll
	 * ever be a problem to run this in other cases (ST instructions,
	 * for example), so I don't bother checking.  If we had to check
	 * for every possible instruction, this code would be much larger.
	 *
	 * The only checking, then, is to see if it's a LD.D or not.
	 *
	 * At the moment....
	 *  r1:  return address to calling exception handler
	 *  TMP: (possibly) revised ssbr
	 *  TMP2: free
	 *  TMP3: free
	 */

	ldcr	TMP3, EPSR	/* going to check: user or system memory? */
	ldcr	TMP2, SXIP	/* get the instruction's address */
	bb1.n	PSR_SUPERVISOR_MODE_BIT, TMP3, 2f
	 clr	TMP2, TMP2, 2<0>	/* get rid of valid and error bits. */

	/* user space load here */
#if ERRATA__XXX_USR
	NOP
	ld.usr	TMP2, TMP2, %r0	/* get the instruction itself */
	NOP
	NOP
	NOP
	br    3f
#else
	br.n  3f
	 ld.usr	TMP2, TMP2, %r0	/* get the instruction itself */
#endif

2:	/* system space load here */
	ld	TMP2, TMP2, %r0	/* get the instruction itself */

3:	/* now we have the instruction..... */
	/*
	 * Now see if it's a double load
	 * There are three forms of double load [IMM16, scaled, unscaled],
	 * which can be checked by matching against two templates:
	 *                    -- 77776666555544443333222211110000 --
	 *   if (((instruction & 11111100000000000000000000000000) ==
	 *                       00010000000000000000000000000000) ||
	 *       ((instruction & 11111100000000001111110011100000) ==
	 *                       11110100000000000001000000000000))
	 *   {
	 *      It's a load double, so
	 *      clear two SSBR bits.
	 *   } else {
	 *      It's not a load double.
	 *      Must be a load single, xmem, or st
	 *      Thus, clear one SSBR bit.
	 *   }
	 */
	/* check the first pattern for ld.d */
	extu	TMP3, TMP2, 16<16>	/* get the upper 16 bits */
	mask	TMP3, TMP3, 0xfc00	/* apply the mask */
	cmp	TMP3, TMP3, 0x1000	/* if equal, it's a load double */
	bb1	eq,   TMP3, 2f

	/* still could be -- check the second pattern for ld.d */
	/* look at the upper 16 bits first */
	extu	TMP3, TMP2, 16<16>	/* get the upper 16 bits */
	mask	TMP3, TMP3, 0xfc00	/* apply the mask */
	cmp	TMP3, TMP3, 0xf400	/* if equal, might be a load double */
	bb1	ne,   TMP3, 1f		/* not equal, must be single */

	/* now look at the lower 16 bits */
	extu	TMP3, TMP2, 16<0>	/* get the lower 16 bits */
	mask	TMP3, TMP3, 0xfce0	/* apply the mask */
	cmp	TMP3, TMP3, 0x1000	/* if equal, it's a load double */
	bb1	eq,   TMP3, 2f

1:	/* misaligned single */
	extu	TMP2, TMP2, 5<21>	/* get the destination register */
	br.n	3f
	 set	TMP2, TMP2, 1<5>	/* set size=1 */

2:	/* misaligned double */
	extu	TMP2, TMP2, 5<21>	/* get the destination register */
	set	TMP2, TMP2, 1<6>	/* set size=2 -- clear two bits */
3:
	jmp.n	%r1
	 clr	TMP,  TMP,  TMP2	/* clear bit(s) in ssbr. */

ASLOCAL(m88100_setup_phase_two)
	/*
	 * SR1: saved copy of exception-time register now holding FLAGS
	 * SR2: free
	 * SR3: saved TMP
	 * r1:  return address to calling exception handler
	 * TMP: possibly revised SSBR
	 * TMP2: free
	 * TMP3: free
	 * FLAGS: CPU status flags
	 * r31: our exception frame
	 *    Valid in the exception frame:
	 *   Exception-time r1, r31, FLAGS.
	 *   Exception-time TMP2, TMP3.
	 *   Exception-time epsr, sfip, snip, sxip.
	 *   Exception number (EF_VECTOR).
	 *   Dmt0
	 *   Other data pipeline control registers, if appropriate.
	 *   FPU control registers, if appropriate.
	 *   Exception SR3, if appropriate.
	 *
	 * immediate goal:
	 *   restore the system to the exception-time state (except
	 *   SR3 will be OUR stack pointer) so that we may restart the FPU.
	 */

	stcr	TMP,  SSBR	/* done with SSBR, TMP now free */
	RESTORE_TMP2(%r31)	/* done with extra temp regs */
	RESTORE_TMP3(%r31)	/* done with extra temp regs */

	/* Get the current PSR and modify for the rte to enable the FPU */
	ldcr	TMP,  PSR
	clr	TMP,  TMP, 1<PSR_FPU_DISABLE_BIT>	/* enable the FPU */
	clr	TMP,  TMP, 1<PSR_SHADOW_FREEZE_BIT>	/* and shadowing */
	stcr	TMP,  EPSR

	/* the "+2" below is to set the VALID_BIT */
	or.u	TMP,  %r0, %hi16(m88100_fpu_enable + 2)
	or	TMP,  TMP, %lo16(m88100_fpu_enable + 2)
	stcr	TMP,  SNIP
	addu	TMP,  TMP, 4
	stcr	TMP,  SFIP

	set	FLAGS, FLAGS, 1<FLAG_ENABLING_FPU>
	xcr	FLAGS, FLAGS, SR1
	st	%r1,   %r31,  EF_RET		/* save the return address */
	ld	%r1,   %r31,  GENREG_OFF(1)	/* get original r1 */

	xcr	TMP,   %r31,  SR3	/* TMP now restored. R31 now saved in SR3 */
	ld	%r31,  %r31,  GENREG_OFF(31)	/* get original r31 */

	/*
	 * SR1: CPU flags
	 * SR2: free
	 * SR3: pointer to our exception frame (our stack pointer)
	 * r1 through r31: original exception-time values
	 *
	 * Valid in the exception frame:
	 *   Exception-time FLAGS.
	 *   Exception-time epsr, sfip, snip, sxip.
	 *   Exception number (EF_VECTOR).
	 *   Dmt0
	 *   Other data pipeline control registers, if appropriate.
	 *   FPU control registers, if appropriate.
	 *   Exception SR3, if appropriate.
	 *   Held temporarily in the exception frame:
	 *   Return address to the calling exception handler.
	 *
	 * immediate goal:
	 *   Do an RTE to restart the fpu and jump to "fpu_enable"
	 *   Another exception (or exceptions) may be raised in
	 *   this, which is why FLAG_ENABLING_FPU is set in SR1.
	 */

	RTE	/* jumps to "m88100_fpu_enable" to enable the FPU. */

ASLOCAL(m88100_fpu_enable)
	FLUSH_PIPELINE
	xcr	TMP,  TMP,  SR3			/* get E.F. pointer */
	st	%r30, TMP,  GENREG_OFF(30)	/* save previous r30, r31 */
	st	%r31, TMP,  GENREG_OFF(31)	/* save previous r30, r31 */
	or	%r31, TMP,  %r0			/* transfer E.F. pointer to r31 */
	ld	TMP,  %r31, EF_SR3		/* get previous SR3 */

	/* make sure that the FLAG_ENABLING_FPU bit is off */
	xcr	FLAGS,FLAGS,SR1
	clr	FLAGS,FLAGS,1<FLAG_ENABLING_FPU>
	xcr	FLAGS,FLAGS,SR1

	xcr	TMP,  TMP,  SR3	/* replace TMP, SR3 */

	/* now save all regs to the exception frame. */
	st	%r0,  %r31, GENREG_OFF(0)
	st	%r1,  %r31, GENREG_OFF(1)
	st	%r2,  %r31, GENREG_OFF(2)
	st	%r3,  %r31, GENREG_OFF(3)
	st	%r4,  %r31, GENREG_OFF(4)
	st	%r5,  %r31, GENREG_OFF(5)
	st	%r6,  %r31, GENREG_OFF(6)
	st	%r7,  %r31, GENREG_OFF(7)
	st	%r8,  %r31, GENREG_OFF(8)
	st	%r9,  %r31, GENREG_OFF(9)
	st	%r10, %r31, GENREG_OFF(10)
	st	%r11, %r31, GENREG_OFF(11)
	st	%r12, %r31, GENREG_OFF(12)
	st	%r13, %r31, GENREG_OFF(13)
	st	%r14, %r31, GENREG_OFF(14)
	st	%r15, %r31, GENREG_OFF(15)
	st	%r16, %r31, GENREG_OFF(16)
	st	%r17, %r31, GENREG_OFF(17)
	st	%r18, %r31, GENREG_OFF(18)
	st	%r19, %r31, GENREG_OFF(19)
	st	%r20, %r31, GENREG_OFF(20)
	st	%r21, %r31, GENREG_OFF(21)
	st	%r22, %r31, GENREG_OFF(22)
	st	%r23, %r31, GENREG_OFF(23)
	st	%r24, %r31, GENREG_OFF(24)
	st	%r25, %r31, GENREG_OFF(25)
	st	%r26, %r31, GENREG_OFF(26)
	st	%r27, %r31, GENREG_OFF(27)
	st	%r28, %r31, GENREG_OFF(28)
	st	%r29, %r31, GENREG_OFF(29)

	/*
	 * SR1: free
	 * SR2: free
	 * SR3: previous exception-time SR3
	 * r1: return address to the calling exception handler
	 * r2 through r30: free
	 * r31: our exception frame
	 *
	 * Valid in the exception frame:
	 *   Exception-time r0 through r31.
	 *   Exception-time FLAGS.
	 *   Exception-time epsr, sfip, snip, sxip.
	 *   Exception number (EF_VECTOR).
	 *   Dmt0
	 *   Other data pipeline control registers, if appropriate.
	 *   FPU control registers, if appropriate.
	 *   Exception SR3, if appropriate.
	 *
	 * immediate goal:
	 *   Pick up a stack if we came in from user mode.
	 *   Put a copy of the exception frame pointer into r30
	 *   Bump the stack a doubleword and write the exception frame pointer.
	 *   If not an interrupt exception, turn on interrupts and service any
	 *     outstanding data access exceptions.
	 *   Return to calling exception handler to service the exception.
	 */

	/*
	 * If it's not the interrupt exception, enable interrupts and
	 * take care of any data access exceptions......
	 */
	or	%r30, %r0,  %r31	/* get a copy of the e.f. pointer */
	ld	%r6,  %r31, EF_EPSR
	bb1	PSR_SUPERVISOR_MODE_BIT, %r6, 1f	/* if in kernel mode */

	ldcr	%r31, CPU
	ld	%r31, %r31, CI_CURPCB
	addu	%r31, %r31, USPACE	/* point at proper end */
1:

	/* get and save IPL */
	bsr	getipl
	st	%r2, %r30, EF_MASK

	/*
	 * here - r30 holds a pointer to the exception frame.
	 * r31 is a pointer to the kernel stack/interrupt stack.
	 */
	subu	%r31, %r31, 8	/* make some breathing space */
	st	%r30, %r31, 0	/* store frame pointer on the stack */
#ifdef DDB
	st	%r30, %r31, 4	/* store it for the debugger to recognize */
#endif

	ld	%r6,  %r30, EF_EPSR
	ld	%r2,  %r30, EF_VECTOR
	bcnd.n	eq0,  %r2,  8f			/* error exception */
	 ld	%r14, %r30, EF_RET

	/*
	 * Do not process possible data exceptions here if this is an interrupt.
	 * Instead, the interrupt handler will take care of this by itself.
	 */
	cmp	%r3,  %r2,  1			/* is an interrupt? */
	bb1.n	eq,   %r3,  8f			/* skip if so */

#ifdef DDB
	 cmp	%r3,  %r2,  130			/* DDB break exception */
	bb1.n	eq,   %r3,  8f
	 cmp	%r3,  %r2,  132			/* DDB entry exception */
	bb1	eq,   %r3,  8f
#else
	 NOP
#endif

	/* turn interrupts back on unless they were not enabled when the
	   trap occurred */
	bb1.n	PSR_INTERRUPT_DISABLE_BIT, %r6, 7f
	 ld	%r3,  %r30, EF_DMT0

	ldcr	%r2,  PSR
	clr	%r2,  %r2,  1<PSR_INTERRUPT_DISABLE_BIT>
	stcr	%r2,  PSR
	FLUSH_PIPELINE
7:
	/* service any outstanding data pipeline stuff */
	bb0	DMT_VALID_BIT, %r3, 8f

	/*
	 * r30 can be clobbered by calls. So stuff its value into a preserved
	 * register, say r15. R14 is in use (see return_to_... below).
	 */
	or	%r15, %r0,  %r30
	CALL(m88100_trap, T_DATAFLT, %r15)
	or	%r30, %r0,  %r15

8:
	jmp	%r14	/* loaded above */
#endif /* M88100 */

#ifdef M88110
/*
 * 88110 exception handlers
 */

/* unknown exception handler */
GLOBAL(m88110_unknown_handler)
	PREP88110("unknown", 0,)
	or	%r2, %r0, T_UNKNOWNFLT
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)

/* interrupt exception handler */
GLOBAL(m88110_interrupt_handler)
	PREP88110("interrupt", 1,)
	or	%r2, %r0, %r30
	XCALL(interrupt, check_ast)

/* instruction access exception handler */
GLOBAL(m88110_instruction_access_handler)
	PREP88110("inst", 2,)
	or	%r2, %r0, T_INSTFLT
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)
/*
 * data access exception handler --
 *  See badaddr() below for info about Data_Precheck.
 */
GLOBAL(m88110_data_exception_handler_bootstrap)
	PREP88110("data", 3, M88110_Data_Precheck)
	or	%r2, %r0, T_DATAFLT
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)
GLOBAL(m88110_data_exception_handler)
	PREP88110("data", 3,)
	or	%r2, %r0, T_DATAFLT
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)

/* misaligned access exception handler */
GLOBAL(m88110_misaligned_handler)
	PREP88110("misalign", 4,)
	or	%r2, %r0, T_MISALGNFLT
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)

/* unimplemented opcode exception handler */
GLOBAL(m88110_unimplemented_handler)
	PREP88110("unimp", 5,)
	or	%r2, %r0, T_ILLFLT
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)

/* privilege exception handler */
GLOBAL(m88110_privilege_handler)
	PREP88110("privilege", 6,)
	or	%r2, %r0, T_PRIVINFLT
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)

/* bounds checking exception handler */
GLOBAL(m88110_bounds_handler)
	PREP88110("bounds", 7,)
	or	%r2, %r0, T_BNDFLT
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)

/* integer divide-by-zero exception handler */
GLOBAL(m88110_divide_handler)
	PREP88110("divide", 8,)
	or	%r2, %r0, T_ZERODIV
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)

/* integer overflow exception handler */
GLOBAL(m88110_overflow_handler)
	PREP88110("overflow", 9,)
	or	%r2, %r0, T_OVFFLT
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)

/* Floating-point precise handler */
GLOBAL(m88110_fpu_handler)
	PREP88110("FPU", 114,)
	or	%r2, %r0, T_FPEPFLT
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)

/* non-maskable interrupt handler (IPIs, ABORT button) */
GLOBAL(m88110_nonmaskable)
	PREP88110("NMI", 11,)
	or	%r2, %r0, %r30
	XCALL(nmi, nmi_return)

/* software walk data MMU read miss handler */
GLOBAL(m88110_data_read_miss)
	PREP88110("88110 data read miss", 12,)
	or	%r2, %r0, T_110_DRM
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)

/* software walk data MMU write miss handler */
GLOBAL(m88110_data_write_miss)
	PREP88110("88110 data write miss", 13,)
	or	%r2, %r0, T_110_DWM
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)

/* software walk inst MMU ATC miss handler */
GLOBAL(m88110_inst_atc_miss)
	PREP88110("88110 inst ATC miss", 14,)
	or	%r2, %r0, T_110_IAM
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)

/* trap 450: system calls */
GLOBAL(m88110_syscall_handler)
	PREP88110("syscall", 450,)
	ld	%r2, %r30, GENREG_OFF(13)
	or	%r3, %r0, %r30
	XCALL(m88110_syscall, check_ast)

/* trap 451: cache flush (necessary for trampolines) */
GLOBAL(m88110_cache_flush_handler)
	PREP88110("cache_flush", 451,)
	or	%r2, %r0, %r30
	XCALL(cache_flush, check_ast)

GLOBAL(m88110_sigsys)
	PREP88110("sigsys", 501,)
	or	%r2, %r0, T_SIGSYS
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)

GLOBAL(m88110_stepbpt)
	PREP88110("stepbpt", 504,)
	or	%r2, %r0, T_STEPBPT
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)

GLOBAL(m88110_userbpt)
	PREP88110("userbpt", 511,)
	or	%r2, %r0, T_USERBPT
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)

#ifdef DDB
GLOBAL(m88110_break)
	PREP88110("break", 130,)
	or	%r2, %r0, T_KDB_BREAK
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)

GLOBAL(m88110_trace)
	PREP88110("trace", 131,)
	or	%r2, %r0, T_KDB_TRACE
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)

GLOBAL(m88110_entry)
	PREP88110("kdb", 132,)
	or	%r2, %r0, T_KDB_ENTRY
	or	%r3, %r0, %r30
	XCALL(m88110_trap, check_ast)
#endif

/*
 * The error exception and reset exception handler.
 *
 * The error exception is raised when any other non-trap exception is raised
 * while shadowing is off. This is Bad News.
 *
 * The reset exception is raised when the RST signal is asserted (machine
 * is reset), the value of VBR is changed after exceptions are enabled,
 * or when a jmp, br/bsr to addr 0 (accidents do happen :-)
 * Upon a real reset, VBR is set to zero (0), so code must be at addr 0
 * to handle it!!!
 *
 * The shadow registers are not valid in this case (shadowing was off, if this
 * was an error exception, and may not be on, if this was a reset exception).
 * R1-R31 may be interesting though, so we'll save them.
 *
 * We'll not worry about trashing r26-29 here,
 * since they aren't generally used.
 */
GLOBAL(m88110_error_handler)
	or	%r29, %r0, 10
	br	1f
GLOBAL(m88110_reset_handler)
	or	%r29, %r0, 0
1:
	or	%r26, %r0,  %r31	/* save old stack */
	or.u	%r31, %r0,  %hi16(initstack_end)
	or	%r31, %r31, %lo16(initstack_end)

#ifdef DEBUG
	/* zero the stack, so we'll know what we're lookin' at */
	or.u	%r27, %r0,  %hi16(initstack)
	or	%r27, %r27, %lo16(initstack)
1:	cmp	%r28, %r27, %r31
	bb1	ge,   %r28, 2f	/* branch if at the end of the stack */
	st	%r0,  %r0,  %r27
	addu	%r27, %r27, 4	/* bump up */
	br	1b
2:	/* stack has been cleared */
#endif

	/* ensure that stack is 8-byte aligned */
	clr	%r31, %r31, 3<0>	/* round down to 8-byte boundary */

	/* create exception frame on stack */
	subu	%r31, %r31, TRAPFRAME_SIZEOF	/* r31 now our E.F. */

	/* save old R31 and other R registers */
	st.d	%r0,  %r31, GENREG_OFF(0)
	st.d	%r2,  %r31, GENREG_OFF(2)
	st.d	%r4,  %r31, GENREG_OFF(4)
	st.d	%r6,  %r31, GENREG_OFF(6)
	st.d	%r8,  %r31, GENREG_OFF(8)
	st.d	%r10, %r31, GENREG_OFF(10)
	st.d	%r12, %r31, GENREG_OFF(12)
	st.d	%r14, %r31, GENREG_OFF(14)
	st.d	%r16, %r31, GENREG_OFF(16)
	st.d	%r18, %r31, GENREG_OFF(18)
	st.d	%r20, %r31, GENREG_OFF(20)
	st.d	%r22, %r31, GENREG_OFF(22)
	st.d	%r24, %r31, GENREG_OFF(24)
	st	%r30, %r31, GENREG_OFF(30)
	st	%r26, %r31, GENREG_OFF(31)

	/* vector is put in SRO (either 0 or 10 at this point) */
	st	%r29, %r31, EF_VECTOR

	/* save shadow registers (are OLD if error_handler, though) */
	ldcr	%r10, EPSR
	st	%r10, %r31, EF_EPSR
	ldcr	%r10, EXIP
	st	%r10, %r31, EF_EXIP
	ldcr	%r10, ENIP
	st	%r10, %r31, EF_ENIP
	ldcr	%r10, DSR
	st	%r10, %r31, EF_DSR
	ldcr	%r10, DLAR
	st	%r10, %r31, EF_DLAR
	ldcr	%r10, DPAR
	st	%r10, %r31, EF_DPAR
	ldcr	%r10, ISR
	st	%r10, %r31, EF_ISR
	ldcr	%r10, ILAR
	st	%r10, %r31, EF_ILAR
	ldcr	%r10, IPAR
	st	%r10, %r31, EF_IPAR
	ldcr	%r10, SR1
	st	%r10, %r31, EF_FLAGS

	/* shove sr2 into EF_FPLS1 */
	ldcr	%r10, SR2
	st	%r10, %r31, EF_FPLS1

	/* shove sr3 into EF_FPHS2 */
	ldcr	%r10, SR3
	st	%r10, %r31, EF_FPHS2

	/*
	 * Cheap way to enable FPU and start shadowing again.
	 */
	ldcr	%r10, PSR
	clr	%r10, %r10, 1<PSR_FPU_DISABLE_BIT>	/* enable the FPU */
	clr	%r10, %r10, 1<PSR_SHADOW_FREEZE_BIT>	/* and shadowing */
	stcr	%r10, PSR
	FLUSH_PIPELINE

	/* put pointer to regs into r30... r31 will become a simple stack */
	or	%r30, %r31, %r0

	subu	%r31, %r31, 0x10	/* make some breathing space */
	st	%r30, %r31, 0x0c	/* store frame pointer on the stack */
#ifdef DDB
	st	%r30, %r31, 0x08	/* store again for the debugger to recognize */
	or.u	%r20,  %r0, %hi16(0x87654321)
	or	%r20, %r20, %lo16(0x87654321)
	st	%r20, %r31, 0x04
	st	%r20, %r31, 0x00
#endif

	or	%r2, %r0, %r30
	bsr	error_fatal

	/* turn interrupts back on */
	ldcr	%r1, PSR
	clr	%r1, %r1, 1<PSR_INTERRUPT_DISABLE_BIT>
	stcr	%r1, PSR
	FLUSH_PIPELINE

1:
	br	1b
	/* NOTREACHED */

/*
 * 88110 exception handling setup
 *
 * This is much simpler than for 88100, because all exception are
 * precise. Therefore, when reenabling shadowing, we do not risk
 * getting new exceptions caught by the execution pipeline and not
 * reported yet.
 *
 * However, as soon as shadow freezing is over, we can receive a
 * non-maskable interrupt at any time. The code below will cope with
 * this, as long as the stack pointer (r31) is valid in the kernel
 * all the time shadowing is enabled.
 *
 * Thus, unlike the 88100 code, we setup both the exception frame
 * (in r30) and the exception stack (in r31) as early as possible.
 */

ASLOCAL(m88110_setup_phase_one)
	/*
	 * SR1: saved copy of exception-time register now holding FLAGS
	 * SR2: saved copy of exception-time r1
	 * SR3: free
	 * r1: return address to calling exception handler
	 * FLAGS: CPU status flags
	 *
	 * immediate goal:
	 *   Find out where to put the exception frame, and which
	 *   stack to use.
	 */

	NOP
	xcr	%r1,   %r1,   SR2	/* recover exception-time r1 */
	NOP
	NOP
	NOP

	/*
	 * If we were in the kernel when the exception occurred, we have
	 * a valid stack. Keep using it, and build the frame on it.
	 *
	 * Note that if this exception is an NMI, we might be currently
	 * switching processes, and curpcb and curproc won't match. It
	 * is nevertheless safe to use curpcb, since nmi are processed
	 * with interrupts disabled, and we won't check for AST or soft
	 * interrupts before returning to the cpu_switchto kernel code.
	 */
	bb1	FLAG_FROM_KERNEL, FLAGS, m88110_kernel_stack

	/*
	 * Otherwise, this is an exception in user mode, we'll use the PCB
	 * for the exception frame and the top of the PCB as the stack.
	 */

	/* compute frame address: in PCB */
	stcr	%r30, SR3			/* save r30, now free */
	ldcr	%r30, CPU
	ld	%r30, %r30, CI_CURPCB
	addu	%r30, %r30, PCB_USER_STATE	/* point to user save area */

	/* save a few registers before we lose them*/
	st	%r1,  %r30, GENREG_OFF(1)	/* save prev. r1 (now free)*/
	ldcr	%r1,  SR3			/* save previous r30 */
	st	%r31, %r30, GENREG_OFF(31)
	st	%r1,  %r30, GENREG_OFF(30)

	/* compute stack address: top of U area */
	ldcr	%r1,  CPU
	ld	%r31, %r1,  CI_CURPCB
	addu	%r31, %r31, USPACE

	br	m88110_have_stack

ASLOCAL(m88110_kernel_stack)		/* Exception in kernel mode */

	/* compute stack and frame address: allocate them on current stack */
	subu	%r31, %r31, TRAPFRAME_SIZEOF	/* r31 now our E.F. */

	/* save a few registers before we lose them */
	st	%r1,  %r31, GENREG_OFF(1)	/* save prev. r1 (now free) */
	addu	%r1,  %r31, TRAPFRAME_SIZEOF	/* save previous r31 */
	st	%r30, %r31, GENREG_OFF(30)
	st	%r1,  %r31, GENREG_OFF(31)

	/* frame = stack */
	or	%r30, %r31, %r0

	/* FALLTHROUGH */

ASLOCAL(m88110_have_stack)
	/*
	 * SR1: saved copy of exception-time register now holding FLAGS
	 * SR2: return address to the calling exception handler
	 * SR3: free
	 * r1:  free
	 * FLAGS: CPU status flags
	 * r30: incomplete exception frame
	 * r31: exception stack
	 * Valid in the exception frame:
	 *   Exception-time r1, r30, r31, FLAGS.
	 */

	stcr	TMP, SR3	/* free up TMP, TMP2, TMP3 */
	SAVE_TMP2(%r30)
	SAVE_TMP3(%r30)

	/* save some exception-time registers to the exception frame */
	st	FLAGS,%r30, EF_FLAGS		/* save flags */

	ldcr	TMP,  EPSR
	st	TMP,  %r30, EF_EPSR
	ldcr	TMP2, EXIP
	ldcr	TMP3, ENIP
	st	TMP2, %r30, EF_EXIP
	st	TMP3, %r30, EF_ENIP

	/* get and store the cpu_info pointer */
	ldcr	TMP,  CPU
	st	TMP,  %r30, EF_CPU

	/*
	 * Save and clear fault status registers.
	 */
	ldcr	TMP,  ISR
	st	TMP,  %r30, EF_ISR
	bcnd	eq0,  TMP,  1f
	ldcr	TMP2, ILAR
	ldcr	TMP3, IPAR
	st	TMP2, %r30, EF_ILAR
	st	TMP3, %r30, EF_IPAR
	ldcr	TMP,  ISAP
	ldcr	TMP2, IUAP
	st	TMP,  %r30, EF_ISAP
	st	TMP2, %r30, EF_IUAP
	stcr	%r0,  ISR
1:
	ldcr	TMP,  DSR
	st	TMP,  %r30,  EF_DSR
	bcnd	eq0,  TMP,  1f
	ldcr	TMP2, DLAR
	ldcr	TMP3, DPAR
	st	TMP2, %r30,  EF_DLAR
	st	TMP3, %r30,  EF_DPAR
	ldcr	TMP,  DSAP
	ldcr	TMP2, DUAP
	st	TMP,  %r30,  EF_DSAP
	st	TMP2, %r30,  EF_DUAP
	stcr	%r0,  DSR
1:
	ldcr	%r1,  SR2
	jmp	%r1

ASLOCAL(m88110_setup_phase_two)
	/*
	 * SR1: saved copy of exception-time register now holding FLAGS
	 * SR2: free
	 * SR3: saved TMP
	 * r1:  return address to calling exception handler
	 * TMP2: free
	 * TMP3: free
	 * FLAGS: CPU status flags
	 * r30: incomplete exception frame
	 * r31: exception stack
	 * Valid in the exception frame:
	 *   Exception-time r1, r30, r31, FLAGS.
	 *   Exception-time TMP2, TMP3.
	 *   Exception-time epsr, enip, exip.
	 *   Exception number (EF_VECTOR).
	 *
	 * immediate goal:
	 *   restore the system to the exception-time state.
	 */

	RESTORE_TMP2(%r30)	/* done with extra temp regs */
	RESTORE_TMP3(%r30)	/* done with extra temp regs */

	ldcr	TMP,  PSR
	clr	TMP,  TMP,  1<PSR_SHADOW_FREEZE_BIT>	/* enable shadowing */
	stcr	TMP,  EPSR

	or.u	TMP,  %r0,  %hi16(m88110_shadow_enable)
	or	TMP,  TMP,  %lo16(m88110_shadow_enable)
	stcr	TMP,  EXIP

	xcr	FLAGS,FLAGS,SR1
	st	%r1,  %r30, EF_RET		/* save the return address */

	xcr	TMP,  %r31, SR3	/* TMP now restored. R31 now saved in SR3 */

	/*
	 * SR1: CPU flags
	 * SR2: free
	 * SR3: pointer to our exception frame (our stack pointer)
	 * r2 through r29: original exception-time values
	 * r30: exception frame
	 * r31: exception stack
	 *
	 * Valid in the exception frame:
	 *   Exception-time r1, r30, r31, FLAGS.
	 *   Exception-time epsr, sfip, enip, exip.
	 *   Exception number (EF_VECTOR).
	 *   DSR/ISR, fault registers, if appropriate.
	 * Held temporarily in the exception frame:
	 *   Return address to the calling exception handler.
	 *
	 * immediate goal:
	 *   Do an RTE to unfreeze the shadow registers.
	 *   Another exception (NMI) may be raised in this.
	 */
	RTE	/* jumps to "m88110_shadow_enable" */

ASLOCAL(m88110_shadow_enable)
	FLUSH_PIPELINE				/* XXX necessary? */

	/* now save all missing regs to the exception frame. */
	st	%r0,  %r30, GENREG_OFF(0)
	st	%r2,  %r30, GENREG_OFF(2)
	st	%r3,  %r30, GENREG_OFF(3)
	st	%r4,  %r30, GENREG_OFF(4)
	st	%r5,  %r30, GENREG_OFF(5)
	st	%r6,  %r30, GENREG_OFF(6)
	st	%r7,  %r30, GENREG_OFF(7)
	st	%r8,  %r30, GENREG_OFF(8)
	st	%r9,  %r30, GENREG_OFF(9)
	st	%r10, %r30, GENREG_OFF(10)
	st	%r11, %r30, GENREG_OFF(11)
	st	%r12, %r30, GENREG_OFF(12)
	st	%r13, %r30, GENREG_OFF(13)
	st	%r14, %r30, GENREG_OFF(14)
	st	%r15, %r30, GENREG_OFF(15)
	st	%r16, %r30, GENREG_OFF(16)
	st	%r17, %r30, GENREG_OFF(17)
	st	%r18, %r30, GENREG_OFF(18)
	st	%r19, %r30, GENREG_OFF(19)
	st	%r20, %r30, GENREG_OFF(20)
	st	%r21, %r30, GENREG_OFF(21)
	st	%r22, %r30, GENREG_OFF(22)
	st	%r23, %r30, GENREG_OFF(23)
	st	%r24, %r30, GENREG_OFF(24)
	st	%r25, %r30, GENREG_OFF(25)
	st	%r26, %r30, GENREG_OFF(26)
	st	%r27, %r30, GENREG_OFF(27)
	st	%r28, %r30, GENREG_OFF(28)
	st	%r29, %r30, GENREG_OFF(29)

	/*
	 * SR1: free
	 * SR2: free
	 * SR3: free
	 * r1 through r29: free
	 * r30: exception frame
	 * r31: exception stack
	 *
	 * Valid in the exception frame:
	 *   Exception-time r0 through r31.
	 *   Exception-time FLAGS.
	 *   Exception-time epsr, enip, exip.
	 *   Exception number (EF_VECTOR).
	 *   Return value (EF_RET).
	 *   DSR/ISR, fault registers, if appropriate.
	 *
	 * immediate goal:
	 *   Bump the stack a doubleword and write the exception frame pointer.
	 *   If not an interrupt exception or an NMI, turn on interrupts.
	 *   Return to calling exception handler to service the exception.
	 */

	/* get and save IPL */
	bsr	getipl
	st	%r2,  %r30, EF_MASK

	subu	%r31, %r31, 8	/* make some breathing space */
	st	%r30, %r31, 0	/* store frame pointer on the stack */
#ifdef DDB
	st	%r30, %r31, 4	/* store it again for the debugger */
#endif

	ld	%r6,  %r30, EF_EPSR
	ld	%r14, %r30, EF_RET

	/* don't turn interrupts back on unless they were enabled when the
	   trap occurred */
	bb1	PSR_INTERRUPT_DISABLE_BIT, %r6, 8f

	ld	%r2,  %r30, EF_VECTOR
	bcnd	eq0,  %r2,  8f
	cmp	%r3,  %r2,  1		/* is this an interrupt? */
	bb1	eq,   %r3,  8f
	cmp	%r3,  %r2,  11		/* or NMI? */
	bb1	eq,   %r3,  8f

#ifdef DDB
	cmp	%r3,  %r2,  130		/* DDB break exception */
	bb1	eq,   %r3,  8f
	cmp	%r3,  %r2,  132		/* DDB entry exception */
	bb1	eq,   %r3,  8f
#endif

	ldcr	%r2,  PSR
	clr	%r2,  %r2,  1<PSR_INTERRUPT_DISABLE_BIT>
	stcr	%r2,  PSR
	FLUSH_PIPELINE

8:
	jmp	%r14		/* loaded above */
#endif	/* M88110 */

	.text

#define	FPTR	%r14

ASGLOBAL(check_ast)
	/*
	 * Regs r1-r30 are free. r31 is pointing at the word
	 * on the kernel stack where our pointer to the exception frame
	 * it stored. Reload it now.
	 *
	 * We load it into r14 since it is preserved across function
	 * calls, and we may have to call some routines from within here.
	 *
	 * Control is transferred here from obvious places in this file.
	 */
	ld	FPTR, %r31, 0	/* grab exception frame pointer */

	/*
	 * If the saved ipl is 0, then call spl0() to process soft
	 * interrupts. And if returning to userland, look for ASTs.
	 */

	/* do not service AST and soft interrupts if interrupts were disabled */
	ld	%r2, FPTR, EF_EPSR
	bb1	PSR_INTERRUPT_DISABLE_BIT, %r2, ast_done
	/* ...or we were not at spl0 */
	ld	%r2, FPTR, EF_MASK
	bcnd	ne0, %r2, ast_done

#ifdef MULTIPROCESSOR
	/*
	 * Check for IPI soft interrupt
	 */
	ldcr	%r3,  CPU
	or	%r15, %r0,  %r0
	addu	%r3,  %r3,  CI_SOFTIPI_CB
	xmem	%r15, %r3,  %r0
	bcnd	eq0,  %r15, do_softint

	bsr.n	setipl
	 or	%r2,  %r0,  IPL_NONE

	jsr	%r15
ASLOCAL(do_softint)
#endif

	/*
	 * Process soft interrupts, if any.
	 */
	bsr	spl0

	/* do not service AST if not returning to user mode */
	ld	%r2, FPTR, EF_EPSR
	bb1	PSR_SUPERVISOR_MODE_BIT, %r2, ast_done
1:
	ldcr	%r2, CPU
	ld	%r3, %r2, CI_CURPROC
	ld	%r2, %r3, P_ASTPENDING
	bcnd	eq0, %r2, ast_done

	bsr.n	ast
	 or	%r2, %r0, FPTR

	br	1b

/*
 * void proc_trampoline()
 *
 * When a process setup by cpu_fork() resumes, it will find itself in
 * proc_trampoline, with r14 and r15 referring to a function and its
 * argument. proc_trampoline will call the function, and return to userland.
 */

ENTRY(proc_trampoline)
	bsr	proc_trampoline_mi

	jsr.n	%r14
	 or	%r2,  %r15, 0		/* arg */

/*
 * Load FPTR with a pointer to the trap frame for the current proc and
 * continue near the end of check_ast, bypassing soft interrupts and AST
 * checks, to load all the registers and do an RTE.
 */

	ldcr	%r3,  CPU
	ld	%r2,  %r3,  CI_CURPROC

	ld	FPTR, %r2,   P_ADDR		/* p->p_addr */
	addu	FPTR, FPTR, PCB_USER_STATE	/* p->p_addr.u_pcb.user_state */

	/* FALLTHROUGH */

ASGLOBAL(ast_done)
	/* disable interrupts */
	ldcr	%r1, PSR
	set	%r1, %r1, 1<PSR_INTERRUPT_DISABLE_BIT>
	stcr	%r1, PSR
	FLUSH_PIPELINE

	/* now ready to return....*/
	bsr.n	setipl
	 ld	%r2, FPTR, EF_MASK		/* get pre-exception ipl */

#if defined(M88100) && defined(M88110)
	ldcr	%r2, PID
	extu	%r3, %r2, 8<8>
	bcnd	ne0, %r3, m88110_user_rte
	/* FALLTHROUGH */
#endif

#ifdef M88100
ASGLOBAL(m88100_user_rte)
	/*
	 * Transfer the frame pointer to r31, since we no longer need a stack.
	 * No page faults here, and interrupts are disabled.
	 */
	or	%r31, %r0,  FPTR

	/* restore r1 later */
	ld	%r2,  %r31, GENREG_OFF(2)
	ld	%r3,  %r31, GENREG_OFF(3)
	ld	%r4,  %r31, GENREG_OFF(4)
	ld	%r5,  %r31, GENREG_OFF(5)
	ld	%r6,  %r31, GENREG_OFF(6)
	ld	%r7,  %r31, GENREG_OFF(7)
	ld	%r8,  %r31, GENREG_OFF(8)
	ld	%r9,  %r31, GENREG_OFF(9)
	ld	%r10, %r31, GENREG_OFF(10)
	ld	%r11, %r31, GENREG_OFF(11)
	ld	%r12, %r31, GENREG_OFF(12)
	ld	%r13, %r31, GENREG_OFF(13)
	ld	%r14, %r31, GENREG_OFF(14)
	ld	%r15, %r31, GENREG_OFF(15)
	ld	%r16, %r31, GENREG_OFF(16)
	ld	%r17, %r31, GENREG_OFF(17)
	ld	%r18, %r31, GENREG_OFF(18)
	ld	%r19, %r31, GENREG_OFF(19)
	ld	%r20, %r31, GENREG_OFF(20)
	ld	%r21, %r31, GENREG_OFF(21)
	ld	%r22, %r31, GENREG_OFF(22)
	ld	%r23, %r31, GENREG_OFF(23)
	ld	%r24, %r31, GENREG_OFF(24)
	ld	%r25, %r31, GENREG_OFF(25)
	ld	%r26, %r31, GENREG_OFF(26)
	ld	%r27, %r31, GENREG_OFF(27)
	ld	%r28, %r31, GENREG_OFF(28)
	ld	%r29, %r31, GENREG_OFF(29)
	/* restore r1, r30, r31 later */

	/* disable shadowing */
	ldcr	%r1, PSR
	set	%r1, %r1, 1<PSR_SHADOW_FREEZE_BIT>
	stcr	%r1, PSR
	FLUSH_PIPELINE

	/* reload the control regs*/
	/*
	 * RTE will cause execution to continue first with the
	 * instruction pointed to by the NIP and then the FIP;
	 * it is not necessary to restore XIP.
	 */
	stcr	%r0,  SSBR
	ld	%r30, %r31, EF_SNIP
	ld	%r1,  %r31, EF_SFIP
	stcr	%r30, SNIP
	stcr	%r1,  SFIP

	ld	%r30, %r31, EF_EPSR
	stcr	%r30, EPSR

	/* Now restore r1, r30, and r31 */
	ld	%r1,  %r31, GENREG_OFF(1)
	ld	%r30, %r31, GENREG_OFF(30)
	ld	%r31, %r31, GENREG_OFF(31)

	RTE
#endif

#ifdef M88110
ASGLOBAL(m88110_user_rte)
	/*
	 * Disable shadowing. This used to be done after all the registers
	 * from the E.F. have been restored, but on 88110 we may receive
	 * an NMI anytime, unless shadowing is frozen, and we rely on r31
	 * being valid.
	 */
	ldcr	%r1, PSR
	set	%r1, %r1, 1<PSR_SHADOW_FREEZE_BIT>
	stcr	%r1, PSR
	FLUSH_PIPELINE

ASLOCAL(m88110_restore)
	/*
	 * Transfer the frame pointer to r31, since we no longer need a stack.
	 * No page faults here, and interrupts are disabled.
	 */
	or	%r31, %r0,  FPTR
	/* restore r1 later */
	ld	%r2,  %r31, GENREG_OFF(2)
	ld	%r3,  %r31, GENREG_OFF(3)
	ld	%r4,  %r31, GENREG_OFF(4)
	ld	%r5,  %r31, GENREG_OFF(5)
	ld	%r6,  %r31, GENREG_OFF(6)
	ld	%r7,  %r31, GENREG_OFF(7)
	ld	%r8,  %r31, GENREG_OFF(8)
	ld	%r9,  %r31, GENREG_OFF(9)
	ld	%r10, %r31, GENREG_OFF(10)
	ld	%r11, %r31, GENREG_OFF(11)
	ld	%r12, %r31, GENREG_OFF(12)
	ld	%r13, %r31, GENREG_OFF(13)
	ld	%r14, %r31, GENREG_OFF(14)
	ld	%r15, %r31, GENREG_OFF(15)
	ld	%r16, %r31, GENREG_OFF(16)
	ld	%r17, %r31, GENREG_OFF(17)
	ld	%r18, %r31, GENREG_OFF(18)
	ld	%r19, %r31, GENREG_OFF(19)
	ld	%r20, %r31, GENREG_OFF(20)
	ld	%r21, %r31, GENREG_OFF(21)
	ld	%r22, %r31, GENREG_OFF(22)
	ld	%r23, %r31, GENREG_OFF(23)
	ld	%r24, %r31, GENREG_OFF(24)
	ld	%r25, %r31, GENREG_OFF(25)
	ld	%r26, %r31, GENREG_OFF(26)
	ld	%r27, %r31, GENREG_OFF(27)
	ld	%r28, %r31, GENREG_OFF(28)
	ld	%r29, %r31, GENREG_OFF(29)
	/* restore r1, r30, r31 later */

	/* reload the control regs*/
	ld	%r30, %r31, EF_ENIP
	ld	%r1,  %r31, EF_EXIP
	stcr	%r30, ENIP
	stcr	%r1,  EXIP

	ld	%r30, %r31, EF_EPSR
	stcr	%r30, EPSR

	/* Now restore r1, r30, and r31 */
	ld	%r1,  %r31, GENREG_OFF(1)
	ld	%r30, %r31, GENREG_OFF(30)
	ld	%r31, %r31, GENREG_OFF(31)

	/*
	 * 88110 errata #18 (fixed in revision 5 cpus):
	 * ``An rte to user mode where the logical address of the rte
	 *   instruction is within two cache lines of the logical address
	 *   in the exip can cause the wrong instruction to be returned
	 *   to if the rte instruction misses in the icache.''
	 *
	 * Since we run with interrupts disabled at this point, all we
	 * need is make sure that the rte will not appear as the first
	 * instruction of a cache line.
	 */
	br	1f

	.align 6	/* 32 bytes might not be enough */
1:
	NOP
	RTE

	/*
	 * NMI return here after processing.
	 * We then decide whether to check for AST and soft interrupts,
	 * or not.
	 */
ASLOCAL(nmi_return)
	bcnd	ne0, %r2, check_ast

	ld	FPTR, %r31, 0	/* grab exception frame pointer */

	/*
	 * Disable interrupts and shadowing. The latter used to be done
	 * after all the registers from the E.F. have been restored, but
	 * on 88110 we may receive an NMI anytime, unless shadowing is frozen,
	 * and we rely on r31 being valid.
	 */
	ldcr	%r1, PSR
	set	%r1, %r1, 1<PSR_INTERRUPT_DISABLE_BIT>
	set	%r1, %r1, 1<PSR_SHADOW_FREEZE_BIT>
	stcr	%r1, PSR
	FLUSH_PIPELINE

	/* now ready to return....*/
	bsr.n	setipl
	 ld	%r2, FPTR, EF_MASK		/* get pre-exception ipl */

#ifdef MULTIPROCESSOR
	/*
	 * Reenable NMIs if necessary.
	 */
	or	%r2,   FPTR,  %r0
	bsr	nmi_wrapup
#endif

	br	m88110_restore
#endif
