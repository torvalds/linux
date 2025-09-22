/*	$OpenBSD: _setjmp.S,v 1.6 2016/05/09 16:33:48 guenther Exp $	*/
/*	$NetBSD: _setjmp.S,v 1.2 1996/10/17 03:08:03 cgd Exp $	*/

/*
 * Copyright (c) 1994, 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
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

#include "SYS.h"

/*
 * C library -- _setjmp, _longjmp
 *
 *	_longjmp(a,v)
 * will generate a "return(v)" from
 * the last call to
 *	_setjmp(a)
 * by restoring registers from the stack,
 * The previous signal state is NOT restored.
 */

	.set	noreorder

LEAF(_setjmp, 1)
	LDGP(pv)
	stq	ra, (2 * 8)(a0)			/* sc_pc = return address */
	stq	s0, (( 9 + 4) * 8)(a0)		/* saved bits of sc_regs */
	stq	s1, ((10 + 4) * 8)(a0)
	stq	s2, ((11 + 4) * 8)(a0)
	stq	s3, ((12 + 4) * 8)(a0)
	stq	s4, ((13 + 4) * 8)(a0)
	stq	s5, ((14 + 4) * 8)(a0)
	stq	s6, ((15 + 4) * 8)(a0)
	stq	ra, ((26 + 4) * 8)(a0)
	stq	sp, ((30 + 4) * 8)(a0)
	ldiq	t0, 0xacedbadd			/* sigcontext magic number */
	stq	t0, ((31 + 4) * 8)(a0)		/* magic in sc_regs[31] */
	/* Too bad we can't check if we actually used FP */
	ldiq	t0, 1
	stq	t0, (36 * 8)(a0)		/* say we've used FP.  */
	stt	fs0, ((2 + 37) * 8)(a0)		/* saved bits of sc_fpregs */
	stt	fs1, ((3 + 37) * 8)(a0)
	stt	fs2, ((4 + 37) * 8)(a0)
	stt	fs3, ((5 + 37) * 8)(a0)
	stt	fs4, ((6 + 37) * 8)(a0)
	stt	fs5, ((7 + 37) * 8)(a0)
	stt	fs6, ((8 + 37) * 8)(a0)
	stt	fs7, ((9 + 37) * 8)(a0)
	mf_fpcr ft0				/* get FP control reg */
	stt	ft0, (69 * 8)(a0)		/* and store it in sc_fpcr */
	stq	zero, (70 * 8)(a0)		/* FP software control XXX */
	stq	zero, (71 * 8)(a0)		/* sc_reserved[0] */
	stq	zero, (72 * 8)(a0)		/* sc_reserved[1] */
	stq	zero, (73 * 8)(a0)		/* sc_xxx[0] */
	stq	zero, (74 * 8)(a0)		/* sc_xxx[1] */
	stq	zero, (75 * 8)(a0)		/* sc_xxx[2] */
	stq	zero, (76 * 8)(a0)		/* sc_xxx[3] */
	stq	zero, (77 * 8)(a0)		/* sc_xxx[4] */
	stq	zero, (78 * 8)(a0)		/* sc_xxx[5] */
	stq	zero, (79 * 8)(a0)		/* sc_xxx[6] */
	stq	zero, (80 * 8)(a0)		/* sc_xxx[7] */

	mov	zero, v0			/* return zero */
	RET
END_STRONG(_setjmp)

LEAF(_longjmp, 2)
	LDGP(pv)
	ldq	t0, ((31 + 4) * 8)(a0)		/* magic in sc_regs[31] */
	ldiq	t1, 0xacedbadd
	cmpeq	t0, t1, t0
	beq	t0, botch			/* If the magic was bad, punt */

	ldq	ra, (2 * 8)(a0)			/* sc_pc = return address */
	ldq	s0, (( 9 + 4) * 8)(a0)		/* saved bits of sc_regs */
	ldq	s1, ((10 + 4) * 8)(a0)
	ldq	s2, ((11 + 4) * 8)(a0)
	ldq	s3, ((12 + 4) * 8)(a0)
	ldq	s4, ((13 + 4) * 8)(a0)
	ldq	s5, ((14 + 4) * 8)(a0)
	ldq	s6, ((15 + 4) * 8)(a0)
	/* ldq	ra, ((26 + 4) * 8)(a0)		set above */
	ldq	sp, ((30 + 4) * 8)(a0)
	ldt	fs0, ((2 + 37) * 8)(a0)		/* saved bits of sc_fpregs */
	ldt	fs1, ((3 + 37) * 8)(a0)
	ldt	fs2, ((4 + 37) * 8)(a0)
	ldt	fs3, ((5 + 37) * 8)(a0)
	ldt	fs4, ((6 + 37) * 8)(a0)
	ldt	fs5, ((7 + 37) * 8)(a0)
	ldt	fs6, ((8 + 37) * 8)(a0)
	ldt	fs7, ((9 + 37) * 8)(a0)
	ldt	ft0, (69 * 8)(a0)		/* get sc_fpcr */
	mt_fpcr	ft0				/* and restore it. */

	bne	a1, 1f
	addq	a1, 1, a1
1:
	mov	a1, v0				/* return second arg */
	RET

botch:
	CALL(_HIDDEN(abort))
	RET					/* "can't" get here... */
END_STRONG(_longjmp)
