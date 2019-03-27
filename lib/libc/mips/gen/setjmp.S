/*	$NetBSD: setjmp.S,v 1.17 2005/09/17 11:49:39 tsutsui Exp $	*/

/*-
 * Copyright (c) 1991, 1993
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
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");
#include <machine/regnum.h>

#if defined(LIBC_SCCS) && !defined(lint)
	ASMSTR("from: @(#)setjmp.s	8.1 (Berkeley) 6/4/93")
	ASMSTR("$NetBSD: setjmp.S,v 1.17 2005/09/17 11:49:39 tsutsui Exp $")
#endif /* LIBC_SCCS and not lint */

#include "SYS.h"

#ifdef __ABICALLS__
	.abicalls
#endif

/*
 * C library -- setjmp, longjmp
 *
 *	longjmp(a,v)
 * will generate a "return(v)" from
 * the last call to
 *	setjmp(a)
 * by restoring registers from the stack,
 * and a struct sigcontext, see <signal.h>
 */

#define SETJMP_FRAME_SIZE	(CALLFRAME_SIZ + (SZREG * 2))


NESTED(setjmp, SETJMP_FRAME_SIZE, ra)
	.mask	0x80000000, (CALLFRAME_RA - CALLFRAME_SIZ)
	SETUP_GP
	PTR_SUBU sp, sp, SETJMP_FRAME_SIZE	# allocate stack frame
	SAVE_GP(CALLFRAME_GP)
	SETUP_GP64(CALLFRAME_GP, setjmp)

	REG_S	ra, CALLFRAME_RA(sp)		# save RA
	REG_S	a0, CALLFRAME_SIZ(sp)		# store env

	/* Get the signal mask. */
	PTR_ADDU a2, a0, _JB_SIGMASK * SZREG	# &oenv
	li	a0, 1				# SIG_SETBLOCK
	move	a1, zero			# &env == 0
	PTR_LA	t9, _C_LABEL(sigprocmask)	# get current signal mask
	jalr	t9

	RESTORE_GP64
	REG_L	a0, CALLFRAME_SIZ(sp)		# restore env pointer
	REG_L	ra, CALLFRAME_RA(sp)		# restore RA
	PTR_ADDU sp, sp, SETJMP_FRAME_SIZE	# pop stack frame

	REG_LI	v0, _JB_MAGIC_SETJMP
	REG_S	v0, (_JB_MAGIC  * SZREG)(a0)
	REG_S	ra, (_JB_REG_RA * SZREG)(a0)
	/*
	 * From "MIPSpro N32 ABI Handbook", Table 2-1:
	 * Registers s0..s7 are callee-saved.
	 * The sp register is callee-saved.
	 * The fp (or s8) register is callee-saved.
	 * The gp register is callee-saved (for n32/n64).
	 */
	REG_S	s0, (_JB_REG_S0 * SZREG)(a0)
	REG_S	s1, (_JB_REG_S1 * SZREG)(a0)
	REG_S	s2, (_JB_REG_S2 * SZREG)(a0)
	REG_S	s3, (_JB_REG_S3 * SZREG)(a0)
	REG_S	s4, (_JB_REG_S4 * SZREG)(a0)
	REG_S	s5, (_JB_REG_S5 * SZREG)(a0)
	REG_S	s6, (_JB_REG_S6 * SZREG)(a0)
	REG_S	s7, (_JB_REG_S7 * SZREG)(a0)
	REG_S	sp, (_JB_REG_SP * SZREG)(a0)
	REG_S	s8, (_JB_REG_S8 * SZREG)(a0)
#if defined(__mips_n32) || defined(__mips_n64)
	REG_S	gp, (_JB_REG_GP * SZREG)(a0)
#endif
#ifndef __mips_soft_float
	/*
	 * From "MIPSpro N32 ABI Handbook", Table 2-1:
	 * In N32, FP registers F20, F22, F24, F26, F28, F30 are callee-saved.
	 * In N64, FP registers F24 .. F31 are callee-saved.
	 * In O32, FP registers F20 .. F23 are callee-saved.
	 */
	cfc1	v0, $31
	INT_S	v0, (_JB_FPREG_FCSR * SZREG)(a0)
#if defined(__mips_o32) || defined(__mips_o64) || defined(__mips_n32)
	FP_S	$f20, (_JB_FPREG_F20 * SZREG)(a0)
	FP_S	$f22, (_JB_FPREG_F22 * SZREG)(a0)
#endif
#if defined(__mips_o32) || defined(__mips_o64)
	FP_S	$f21, (_JB_FPREG_F21 * SZREG)(a0)
	FP_S	$f23, (_JB_FPREG_F23 * SZREG)(a0)
#endif
#if defined(__mips_n32) || defined(__mips_n64)
	FP_S	$f24, (_JB_FPREG_F24 * SZREG)(a0)
	FP_S	$f26, (_JB_FPREG_F26 * SZREG)(a0)
	FP_S	$f28, (_JB_FPREG_F28 * SZREG)(a0)
	FP_S	$f30, (_JB_FPREG_F30 * SZREG)(a0)
#endif
#if defined(__mips_n64)
	FP_S	$f25, (_JB_FPREG_F25 * SZREG)(a0)
	FP_S	$f27, (_JB_FPREG_F27 * SZREG)(a0)
	FP_S	$f29, (_JB_FPREG_F29 * SZREG)(a0)
	FP_S	$f31, (_JB_FPREG_F31 * SZREG)(a0)
#endif
#endif	/* ! __mips_soft_float */

	move	v0, zero
	jr	ra
END(setjmp)

#define LONGJMP_FRAME_SIZE	(CALLFRAME_SIZ + (SZREG * 2))

NESTED(longjmp, LONGJMP_FRAME_SIZE, ra)
	.mask	0x80000000, (CALLFRAME_RA - CALLFRAME_SIZ)
	PIC_PROLOGUE(longjmp)
	PTR_SUBU sp, sp, LONGJMP_FRAME_SIZE	# allocate stack frame
	SAVE_GP(CALLFRAME_GP)

	REG_S	ra, CALLFRAME_RA(sp)		# save RA
	REG_L	v0, (_JB_MAGIC  * SZREG)(a0)
	REG_LI	t0, _JB_MAGIC_SETJMP
	bne	v0, t0, botch		# jump if error
	nop

	REG_S	a0, CALLFRAME_SIZ(sp)		# save env
	REG_S	a1, (CALLFRAME_SIZ + SZREG)(sp)	# save return value

	# set sigmask
	PTR_ADDU a1, a0, _JB_SIGMASK * SZREG	# &set
	move	a2, zero			# &oset == NULL
	li	a0, 3				# SIG_SETMASK
	PTR_LA	t9,_C_LABEL(sigprocmask)	# set current signal mask
	jal	t9
	nop

	REG_L	a0, CALLFRAME_SIZ(sp)		# restore env
	REG_L	a1, (CALLFRAME_SIZ + SZREG)(sp)	# restore return value

	REG_L	ra, (_JB_REG_RA * SZREG)(a0)
	/*
	 * From "MIPSpro N32 ABI Handbook", Table 2-1:
	 * Registers s0..s7 are callee-saved.
	 * The sp register is callee-saved.
	 * The fp (or s8) register is callee-saved.
	 * The gp register is callee-saved (for n32/n64).
	 */
	REG_L	s0, (_JB_REG_S0 * SZREG)(a0)
	REG_L	s1, (_JB_REG_S1 * SZREG)(a0)
	REG_L	s2, (_JB_REG_S2 * SZREG)(a0)
	REG_L	s3, (_JB_REG_S3 * SZREG)(a0)
	REG_L	s4, (_JB_REG_S4 * SZREG)(a0)
	REG_L	s5, (_JB_REG_S5 * SZREG)(a0)
	REG_L	s6, (_JB_REG_S6 * SZREG)(a0)
	REG_L	s7, (_JB_REG_S7 * SZREG)(a0)
	REG_L	sp, (_JB_REG_SP * SZREG)(a0)
	REG_L	s8, (_JB_REG_S8 * SZREG)(a0)
#if defined(__mips_n32) || defined(__mips_n64)
	REG_L	gp, (_JB_REG_GP * SZREG)(a0)
#endif
#ifndef __mips_soft_float
	/*
	 * From "MIPSpro N32 ABI Handbook", Table 2-1:
	 * In N32, FP registers F20, F22, F24, F26, F28, F30 are callee-saved.
	 * In N64, FP registers F23 .. F31 are callee-saved.
	 * In O32, FP registers F20 .. F23 are callee-saved.
	 */
        INT_L           v0, (_JB_FPREG_FCSR * SZREG)(a0)
        ctc1            v0, $31
#if defined(__mips_n64) || defined(__mips_n32)
	FP_L	$f30, (_JB_FPREG_F30 * SZREG)(a0)
	FP_L	$f28, (_JB_FPREG_F28 * SZREG)(a0)
	FP_L	$f26, (_JB_FPREG_F26 * SZREG)(a0)
	FP_L	$f24, (_JB_FPREG_F24 * SZREG)(a0)
#endif
#if defined(__mips_n32) || defined(__mips_o32) || defined(__mips_o64)
	FP_L	$f22, (_JB_FPREG_F22 * SZREG)(a0)
	FP_L	$f20, (_JB_FPREG_F20 * SZREG)(a0)
#endif
#if defined(__mips_o32) || defined(__mips_o64)
	FP_L	$f21, (_JB_FPREG_F21 * SZREG)(a0)
	FP_L	$f23, (_JB_FPREG_F23 * SZREG)(a0)
#endif
#if defined(__mips_n64)
	FP_L	$f25, (_JB_FPREG_F25 * SZREG)(a0)
	FP_L	$f27, (_JB_FPREG_F27 * SZREG)(a0)
	FP_L	$f29, (_JB_FPREG_F29 * SZREG)(a0)
	FP_L	$f31, (_JB_FPREG_F31 * SZREG)(a0)
#endif
#endif	/* ! __mips_soft_float */

	move	v0, a1
	j	ra
	nop

botch:
	/*
	 * We know we aren't returning so we don't care about restoring
	 * our caller's GP.
	 */
	PTR_LA	t9, _C_LABEL(longjmperror)
	jalr	t9
	nop

	PIC_TAILCALL(abort)
END(longjmp)
