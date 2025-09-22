/*	$OpenBSD: prom_disp.S,v 1.4 1996/10/30 22:40:34 niklas Exp $	*/
/*	$NetBSD: prom_disp.S,v 1.5 1996/10/17 02:50:39 cgd Exp $	*/

/*
 * Copyright (c) 1994, 1995, 1996 Carnegie-Mellon University.
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

#ifndef _LOCORE
#include "include/asm.h"
#include "include/prom.h"
#include "include/rpb.h"
#endif

	.globl	prom_dispatch_v
	.comm	prom_dispatch_v,16

	.text
	.align	4

/*
 * Dispatcher routine.  Implements prom's calling machinery, saves our
 * callee-saved registers as required by C.
 */
#define	D_RA			 (7*8)
#define	D_S0			 (8*8)
#define	D_S1			 (9*8)
#define	D_S2			(10*8)
#define	D_S3			(11*8)
#define	D_S4			(12*8)
#define	D_S5			(13*8)
#define	D_S6			(14*8)
#define	DISPATCH_FRAME_SIZE	(15*8)
#define	DISPATCH_REGS		IM_RA|IM_S0|IM_S1|IM_S2|IM_S3|IM_S4|IM_S5|IM_S6

NESTED(prom_dispatch, 5, DISPATCH_FRAME_SIZE, ra, DISPATCH_REGS, 0)
	LDGP(pv)

	lda	sp, -DISPATCH_FRAME_SIZE(sp)
	stq	ra, D_RA(sp)
	stq	s0, D_S0(sp)
	stq	s1, D_S1(sp)
	stq	s2, D_S2(sp)
	stq	s3, D_S3(sp)
	stq	s4, D_S4(sp)
	stq	s5, D_S5(sp)
	stq	s6, D_S6(sp)

	/* Lord have mercy because.. I would not. */
/* #define	STUPID_PROM_IS_32_BITS */
#ifdef	STUPID_PROM_IS_32_BITS
	ldah	s0, 0x2000(zero)	/* hack for hack */
	lda	s0, (0x2000-8)(s0)

	stq	sp, 0(s0)
	or	s0, zero, sp
#endif	/* STUPID_PROM_IS_32_BITS */

	lda	pv, prom_dispatch_v
	ldq	v0, 0(pv)		/* routine */
	ldq	pv, 8(pv)		/* routine_arg */
	
	jsr	ra, (v0)

#ifdef	STUPID_PROM_IS_32_BITS
	ldah	s0, 0x2000(zero)	/* hack for hack */
	lda	s0, (0x2000-8)(s0)

	ldq	sp, 0(s0)
#endif	/* STUPID_PROM_IS_32_BITS */

	ldq	ra, D_RA(sp)
	ldq	s0, D_S0(sp)
	ldq	s1, D_S1(sp)
	ldq	s2, D_S2(sp)
	ldq	s3, D_S3(sp)
	ldq	s4, D_S4(sp)
	ldq	s5, D_S5(sp)
	ldq	s6, D_S6(sp)
	lda	sp, DISPATCH_FRAME_SIZE(sp)
	RET
END(prom_dispatch)

#undef	D_RA			
#undef	D_S0			
#undef	D_S1			
#undef	D_S2			
#undef	D_S3			
#undef	D_S4			
#undef	D_S5			
#undef	D_S6			
#undef	DISPATCH_FRAME_SIZE
#undef	DISPATCH_REGS
