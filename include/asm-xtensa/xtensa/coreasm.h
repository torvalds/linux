#ifndef XTENSA_COREASM_H
#define XTENSA_COREASM_H

/*
 * THIS FILE IS GENERATED -- DO NOT MODIFY BY HAND
 *
 * include/asm-xtensa/xtensa/coreasm.h -- assembler-specific
 * definitions that depend on CORE configuration.
 *
 * Source for configuration-independent binaries (which link in a
 * configuration-specific HAL library) must NEVER include this file.
 * It is perfectly normal, however, for the HAL itself to include this
 * file.
 *
 * This file must NOT include xtensa/config/system.h.  Any assembler
 * header file that depends on system information should likely go in
 * a new systemasm.h (or sysasm.h) header file.
 *
 *  NOTE: macro beqi32 is NOT configuration-dependent, and is placed
 *        here til we will have configuration-independent header file.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2002 Tensilica Inc.
 */


#include <xtensa/config/core.h>
#include <xtensa/config/specreg.h>

/*
 *  Assembly-language specific definitions (assembly macros, etc.).
 */

/*----------------------------------------------------------------------
 *  find_ms_setbit
 *
 *  This macro finds the most significant bit that is set in <as>
 *  and return its index + <base> in <ad>, or <base> - 1 if <as> is zero.
 *  The index counts starting at zero for the lsbit, so the return
 *  value ranges from <base>-1 (no bit set) to <base>+31 (msbit set).
 *
 *  Parameters:
 *	<ad>	destination address register (any register)
 *	<as>	source address register
 *	<at>	temporary address register (must be different than <as>)
 *	<base>	constant value added to result (usually 0 or 1)
 *  On entry:
 *	<ad> = undefined if different than <as>
 *	<as> = value whose most significant set bit is to be found
 *	<at> = undefined
 *	no other registers are used by this macro.
 *  On exit:
 *	<ad> = <base> + index of msbit set in original <as>,
 *	     = <base> - 1 if original <as> was zero.
 *	<as> clobbered (if not <ad>)
 *	<at> clobbered (if not <ad>)
 *  Example:
 *	find_ms_setbit a0, a4, a0, 0		-- return in a0 index of msbit set in a4
 */

	.macro	find_ms_setbit ad, as, at, base
#if XCHAL_HAVE_NSA
	movi	\at, 31+\base
	nsau	\as, \as	// get index of \as, numbered from msbit (32 if absent)
	sub	\ad, \at, \as	// get numbering from lsbit (0..31, -1 if absent)
#else /* XCHAL_HAVE_NSA */
	movi	\at, \base	// start with result of 0 (point to lsbit of 32)

	beqz	\as, 2f		// special case for zero argument: return -1
	bltui	\as, 0x10000, 1f	// is it one of the 16 lsbits? (if so, check lower 16 bits)
	addi	\at, \at, 16	// no, increment result to upper 16 bits (of 32)
	//srli	\as, \as, 16	// check upper half (shift right 16 bits)
	extui	\as, \as, 16, 16	// check upper half (shift right 16 bits)
1:	bltui	\as, 0x100, 1f	// is it one of the 8 lsbits? (if so, check lower 8 bits)
	addi	\at, \at, 8	// no, increment result to upper 8 bits (of 16)
	srli	\as, \as, 8	// shift right to check upper 8 bits
1:	bltui	\as, 0x10, 1f	// is it one of the 4 lsbits? (if so, check lower 4 bits)
	addi	\at, \at, 4	// no, increment result to upper 4 bits (of 8)
	srli	\as, \as, 4	// shift right 4 bits to check upper half
1:	bltui	\as, 0x4, 1f	// is it one of the 2 lsbits? (if so, check lower 2 bits)
	addi	\at, \at, 2	// no, increment result to upper 2 bits (of 4)
	srli	\as, \as, 2	// shift right 2 bits to check upper half
1:	bltui	\as, 0x2, 1f	// is it the lsbit?
	addi	\at, \at, 2	// no, increment result to upper bit (of 2)
2:	addi	\at, \at, -1	// (from just above: add 1;  from beqz: return -1)
	//srli	\as, \as, 1
1:				// done! \at contains index of msbit set (or -1 if none set)
	.if	0x\ad - 0x\at	// destination different than \at ? (works because regs are a0-a15)
	mov	\ad, \at	// then move result to \ad
	.endif
#endif /* XCHAL_HAVE_NSA */
	.endm	// find_ms_setbit

/*----------------------------------------------------------------------
 *  find_ls_setbit
 *
 *  This macro finds the least significant bit that is set in <as>,
 *  and return its index in <ad>.
 *  Usage is the same as for the find_ms_setbit macro.
 *  Example:
 *	find_ls_setbit a0, a4, a0, 0	-- return in a0 index of lsbit set in a4
 */

	.macro	find_ls_setbit ad, as, at, base
	neg	\at, \as	// keep only the least-significant bit that is set...
	and	\as, \at, \as	// ... in \as
	find_ms_setbit	\ad, \as, \at, \base
	.endm	// find_ls_setbit

/*----------------------------------------------------------------------
 *  find_ls_one
 *
 *  Same as find_ls_setbit with base zero.
 *  Source (as) and destination (ad) registers must be different.
 *  Provided for backward compatibility.
 */

	.macro	find_ls_one ad, as
	find_ls_setbit	\ad, \as, \ad, 0
	.endm	// find_ls_one

/*----------------------------------------------------------------------
 *  floop, floopnez, floopgtz, floopend
 *
 *  These macros are used for fast inner loops that
 *  work whether or not the Loops options is configured.
 *  If the Loops option is configured, they simply use
 *  the zero-overhead LOOP instructions; otherwise
 *  they use explicit decrement and branch instructions.
 *
 *  They are used in pairs, with floop, floopnez or floopgtz
 *  at the beginning of the loop, and floopend at the end.
 *
 *  Each pair of loop macro calls must be given the loop count
 *  address register and a unique label for that loop.
 *
 *  Example:
 *
 *	movi	 a3, 16     // loop 16 times
 *	floop    a3, myloop1
 *	:
 *	bnez     a7, end1	// exit loop if a7 != 0
 *	:
 *	floopend a3, myloop1
 *  end1:
 *
 *  Like the LOOP instructions, these macros cannot be
 *  nested, must include at least one instruction,
 *  cannot call functions inside the loop, etc.
 *  The loop can be exited by jumping to the instruction
 *  following floopend (or elsewhere outside the loop),
 *  or continued by jumping to a NOP instruction placed
 *  immediately before floopend.
 *
 *  Unlike LOOP instructions, the register passed to floop*
 *  cannot be used inside the loop, because it is used as
 *  the loop counter if the Loops option is not configured.
 *  And its value is undefined after exiting the loop.
 *  And because the loop counter register is active inside
 *  the loop, you can't easily use this construct to loop
 *  across a register file using ROTW as you might with LOOP
 *  instructions, unless you copy the loop register along.
 */

	/*  Named label version of the macros:  */

	.macro	floop		ar, endlabel
	floop_		\ar, .Lfloopstart_\endlabel, .Lfloopend_\endlabel
	.endm

	.macro	floopnez	ar, endlabel
	floopnez_	\ar, .Lfloopstart_\endlabel, .Lfloopend_\endlabel
	.endm

	.macro	floopgtz	ar, endlabel
	floopgtz_	\ar, .Lfloopstart_\endlabel, .Lfloopend_\endlabel
	.endm

	.macro	floopend	ar, endlabel
	floopend_	\ar, .Lfloopstart_\endlabel, .Lfloopend_\endlabel
	.endm

	/*  Numbered local label version of the macros:  */
#if 0 /*UNTESTED*/
	.macro	floop89		ar
	floop_		\ar, 8, 9f
	.endm

	.macro	floopnez89	ar
	floopnez_	\ar, 8, 9f
	.endm

	.macro	floopgtz89	ar
	floopgtz_	\ar, 8, 9f
	.endm

	.macro	floopend89	ar
	floopend_	\ar, 8b, 9
	.endm
#endif /*0*/

	/*  Underlying version of the macros:  */

	.macro	floop_	ar, startlabel, endlabelref
	.ifdef	_infloop_
	.if	_infloop_
	.err	// Error: floop cannot be nested
	.endif
	.endif
	.set	_infloop_, 1
#if XCHAL_HAVE_LOOPS
	loop	\ar, \endlabelref
#else /* XCHAL_HAVE_LOOPS */
\startlabel:
	addi	\ar, \ar, -1
#endif /* XCHAL_HAVE_LOOPS */
	.endm	// floop_

	.macro	floopnez_	ar, startlabel, endlabelref
	.ifdef	_infloop_
	.if	_infloop_
	.err	// Error: floopnez cannot be nested
	.endif
	.endif
	.set	_infloop_, 1
#if XCHAL_HAVE_LOOPS
	loopnez	\ar, \endlabelref
#else /* XCHAL_HAVE_LOOPS */
	beqz	\ar, \endlabelref
\startlabel:
	addi	\ar, \ar, -1
#endif /* XCHAL_HAVE_LOOPS */
	.endm	// floopnez_

	.macro	floopgtz_	ar, startlabel, endlabelref
	.ifdef	_infloop_
	.if	_infloop_
	.err	// Error: floopgtz cannot be nested
	.endif
	.endif
	.set	_infloop_, 1
#if XCHAL_HAVE_LOOPS
	loopgtz	\ar, \endlabelref
#else /* XCHAL_HAVE_LOOPS */
	bltz	\ar, \endlabelref
	beqz	\ar, \endlabelref
\startlabel:
	addi	\ar, \ar, -1
#endif /* XCHAL_HAVE_LOOPS */
	.endm	// floopgtz_


	.macro	floopend_	ar, startlabelref, endlabel
	.ifndef	_infloop_
	.err	// Error: floopend without matching floopXXX
	.endif
	.ifeq	_infloop_
	.err	// Error: floopend without matching floopXXX
	.endif
	.set	_infloop_, 0
#if ! XCHAL_HAVE_LOOPS
	bnez	\ar, \startlabelref
#endif /* XCHAL_HAVE_LOOPS */
\endlabel:
	.endm	// floopend_

/*----------------------------------------------------------------------
 *  crsil  --  conditional RSIL (read/set interrupt level)
 *
 *  Executes the RSIL instruction if it exists, else just reads PS.
 *  The RSIL instruction does not exist in the new exception architecture
 *  if the interrupt option is not selected.
 */

	.macro	crsil	ar, newlevel
#if XCHAL_HAVE_OLD_EXC_ARCH || XCHAL_HAVE_INTERRUPTS
	rsil	\ar, \newlevel
#else
	rsr	\ar, PS
#endif
	.endm	// crsil

/*----------------------------------------------------------------------
 *  window_spill{4,8,12}
 *
 *  These macros spill callers' register windows to the stack.
 *  They work for both privileged and non-privileged tasks.
 *  Must be called from a windowed ABI context, eg. within
 *  a windowed ABI function (ie. valid stack frame, window
 *  exceptions enabled, not in exception mode, etc).
 *
 *  This macro requires a single invocation of the window_spill_common
 *  macro in the same assembly unit and section.
 *
 *  Note that using window_spill{4,8,12} macros is more efficient
 *  than calling a function implemented using window_spill_function,
 *  because the latter needs extra code to figure out the size of
 *  the call to the spilling function.
 *
 *  Example usage:
 *
 *		.text
 *		.align	4
 *		.global	some_function
 *		.type	some_function,@function
 *	some_function:
 *		entry	a1, 16
 *		:
 *		:
 *
 *		window_spill4	// spill windows of some_function's callers; preserves a0..a3 only;
 *				// to use window_spill{8,12} in this example function we'd have
 *				// to increase space allocated by the entry instruction, because
 *				// 16 bytes only allows call4; 32 or 48 bytes (+locals) are needed
 *				// for call8/window_spill8 or call12/window_spill12 respectively.
 *		:
 *
 *		retw
 *
 *		window_spill_common	// instantiates code used by window_spill4
 *
 *
 *  On entry:
 *	none (if window_spill4)
 *	stack frame has enough space allocated for call8 (if window_spill8)
 *	stack frame has enough space allocated for call12 (if window_spill12)
 *  On exit:
 *	 a4..a15 clobbered (if window_spill4)
 *	 a8..a15 clobbered (if window_spill8)
 *	a12..a15 clobbered (if window_spill12)
 *	no caller windows are in live registers
 */

	.macro	window_spill4
#if XCHAL_HAVE_WINDOWED
# if XCHAL_NUM_AREGS == 16
	movi	a15, 0			// for 16-register files, no need to call to reach the end
# elif XCHAL_NUM_AREGS == 32
	call4	.L__wdwspill_assist28	// call deep enough to clear out any live callers
# elif XCHAL_NUM_AREGS == 64
	call4	.L__wdwspill_assist60	// call deep enough to clear out any live callers
# endif
#endif
	.endm	// window_spill4

	.macro	window_spill8
#if XCHAL_HAVE_WINDOWED
# if XCHAL_NUM_AREGS == 16
	movi	a15, 0			// for 16-register files, no need to call to reach the end
# elif XCHAL_NUM_AREGS == 32
	call8	.L__wdwspill_assist24	// call deep enough to clear out any live callers
# elif XCHAL_NUM_AREGS == 64
	call8	.L__wdwspill_assist56	// call deep enough to clear out any live callers
# endif
#endif
	.endm	// window_spill8

	.macro	window_spill12
#if XCHAL_HAVE_WINDOWED
# if XCHAL_NUM_AREGS == 16
	movi	a15, 0			// for 16-register files, no need to call to reach the end
# elif XCHAL_NUM_AREGS == 32
	call12	.L__wdwspill_assist20	// call deep enough to clear out any live callers
# elif XCHAL_NUM_AREGS == 64
	call12	.L__wdwspill_assist52	// call deep enough to clear out any live callers
# endif
#endif
	.endm	// window_spill12

/*----------------------------------------------------------------------
 *  window_spill_function
 *
 *  This macro outputs a function that will spill its caller's callers'
 *  register windows to the stack.  Eg. it could be used to implement
 *  a version of xthal_window_spill() that works in non-privileged tasks.
 *  This works for both privileged and non-privileged tasks.
 *
 *  Typical usage:
 *
 *		.text
 *		.align	4
 *		.global	my_spill_function
 *		.type	my_spill_function,@function
 *	my_spill_function:
 *		window_spill_function
 *
 *  On entry to resulting function:
 *	none
 *  On exit from resulting function:
 *	none (no caller windows are in live registers)
 */

	.macro	window_spill_function
#if XCHAL_HAVE_WINDOWED
# if XCHAL_NUM_AREGS == 32
	entry	sp, 48
	bbci.l	a0, 31, 1f		// branch if called with call4
	bbsi.l	a0, 30, 2f		// branch if called with call12
	call8	.L__wdwspill_assist16	// called with call8, only need another 8
	retw
1:	call12	.L__wdwspill_assist16	// called with call4, only need another 12
	retw
2:	call4	.L__wdwspill_assist16	// called with call12, only need another 4
	retw
# elif XCHAL_NUM_AREGS == 64
	entry	sp, 48
	bbci.l	a0, 31, 1f		// branch if called with call4
	bbsi.l	a0, 30, 2f		// branch if called with call12
	call4	.L__wdwspill_assist52	// called with call8, only need a call4
	retw
1:	call8	.L__wdwspill_assist52	// called with call4, only need a call8
	retw
2:	call12	.L__wdwspill_assist40	// called with call12, can skip a call12
	retw
# elif XCHAL_NUM_AREGS == 16
	entry	sp, 16
	bbci.l	a0, 31, 1f	// branch if called with call4
	bbsi.l	a0, 30, 2f	// branch if called with call12
	movi	a7, 0		// called with call8
	retw
1:	movi	a11, 0		// called with call4
2:	retw			// if called with call12, everything already spilled

//	movi	a15, 0		// trick to spill all but the direct caller
//	j	1f
//	//  The entry instruction is magical in the assembler (gets auto-aligned)
//	//  so we have to jump to it to avoid falling through the padding.
//	//  We need entry/retw to know where to return.
//1:	entry	sp, 16
//	retw
# else
#  error "unrecognized address register file size"
# endif
#endif /* XCHAL_HAVE_WINDOWED */
	window_spill_common
	.endm	// window_spill_function

/*----------------------------------------------------------------------
 *  window_spill_common
 *
 *  Common code used by any number of invocations of the window_spill##
 *  and window_spill_function macros.
 *
 *  Must be instantiated exactly once within a given assembly unit,
 *  within call/j range of and same section as window_spill##
 *  macro invocations for that assembly unit.
 *  (Is automatically instantiated by the window_spill_function macro.)
 */

	.macro	window_spill_common
#if XCHAL_HAVE_WINDOWED && (XCHAL_NUM_AREGS == 32 || XCHAL_NUM_AREGS == 64)
	.ifndef	.L__wdwspill_defined
# if XCHAL_NUM_AREGS >= 64
.L__wdwspill_assist60:
	entry	sp, 32
	call8	.L__wdwspill_assist52
	retw
.L__wdwspill_assist56:
	entry	sp, 16
	call4	.L__wdwspill_assist52
	retw
.L__wdwspill_assist52:
	entry	sp, 48
	call12	.L__wdwspill_assist40
	retw
.L__wdwspill_assist40:
	entry	sp, 48
	call12	.L__wdwspill_assist28
	retw
# endif
.L__wdwspill_assist28:
	entry	sp, 48
	call12	.L__wdwspill_assist16
	retw
.L__wdwspill_assist24:
	entry	sp, 32
	call8	.L__wdwspill_assist16
	retw
.L__wdwspill_assist20:
	entry	sp, 16
	call4	.L__wdwspill_assist16
	retw
.L__wdwspill_assist16:
	entry	sp, 16
	movi	a15, 0
	retw
	.set	.L__wdwspill_defined, 1
	.endif
#endif /* XCHAL_HAVE_WINDOWED with 32 or 64 aregs */
	.endm	// window_spill_common

/*----------------------------------------------------------------------
 *  beqi32
 *
 *  macro implements version of beqi for arbitrary 32-bit immidiate value
 *
 *     beqi32 ax, ay, imm32, label
 *
 *  Compares value in register ax with imm32 value and jumps to label if
 *  equal. Clobberes register ay if needed
 *
 */
   .macro beqi32	ax, ay, imm, label
    .ifeq ((\imm-1) & ~7)	// 1..8 ?
		beqi	\ax, \imm, \label
    .else
      .ifeq (\imm+1)		// -1 ?
		beqi	\ax, \imm, \label
      .else
        .ifeq (\imm)		// 0 ?
		beqz	\ax, \label
        .else
		//  We could also handle immediates 10,12,16,32,64,128,256
		//  but it would be a long macro...
		movi	\ay, \imm
		beq	\ax, \ay, \label
        .endif
      .endif
    .endif
   .endm // beqi32

#endif /*XTENSA_COREASM_H*/

