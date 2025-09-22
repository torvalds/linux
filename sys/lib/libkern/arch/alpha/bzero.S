/*	$OpenBSD: bzero.S,v 1.2 1996/10/31 00:43:16 niklas Exp $	*/
/*	$NetBSD: bzero.S,v 1.2 1996/10/17 04:26:24 cgd Exp $	*/

/*
 * Copyright (c) 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Trevor Blackwell
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

#include <machine/asm.h>

LEAF(bzero,2)
	ble	a1,bzero_done
	bic	a1,63,t3	/* t3 is # bytes to do 64 bytes at a time */

	/* If nothing in first word, ignore it */
	subq	zero,a0,t0
	and	t0,7,t0		/* t0 = (0-size)%8 */
	beq	t0,bzero_nostart1

	cmpult	a1,t0,t1	/* if size > size%8 goto noshort */
	beq	t1,bzero_noshort

	/*
	 * The whole thing is less than a word.
	 * Mask off 1..7 bytes, and finish.
	 */
	ldq_u	t2,0(a0)
	lda	t0,-1(zero)	/* t0=-1 */
	mskql	t0,a1,t0	/* Get ff in bytes (a0%8)..((a0+a1-1)%8) */
	insql	t0,a0,t0
	bic	t2,t0,t2	/* zero those bytes in word */
	stq_u	t2,0(a0)
	RET

bzero_noshort:
	/* Handle the first partial word */
	ldq_u	t2,0(a0)
	subq	a1,t0,a1
	mskql	t2,a0,t2	/* zero bytes (a0%8)..7 in word */
	stq_u	t2,0(a0)

	addq	a0,t0,a0	/* round a0 up to next word */
	bic	a1,63,t3	/* recalc t3 (# bytes to do 64 bytes at a
				   time) */

bzero_nostart1:
	/*
	 * Loop, zeroing 64 bytes at a time
	 */
	beq	t3,bzero_lp_done
bzero_lp:
	stq	zero,0(a0)
	stq	zero,8(a0)
	stq	zero,16(a0)
	stq	zero,24(a0)
	subq	t3,64,t3
	stq	zero,32(a0)
	stq	zero,40(a0)
	stq	zero,48(a0)
	stq	zero,56(a0)
	addq	a0,64,a0
	bne	t3,bzero_lp

bzero_lp_done:
	/*
	 * Handle the last 0..7 words.
	 * We mask off the low bits, so we don't need an extra
	 * compare instruction for the loop (just a bne. heh-heh)
	 */
	and	a1,0x38,t4
	beq	t4,bzero_finish_lp_done
bzero_finish_lp:
	stq	zero,0(a0)
	subq	t4,8,t4
	addq	a0,8,a0
	bne	t4,bzero_finish_lp

	/* Do the last partial word */
bzero_finish_lp_done:
	and	a1,7,t5		/* 0..7 bytes left */
	beq	t5,bzero_done	/* mskqh won't change t0 if t5==0, but I
				   don't want to touch, say, a new VM page */
	ldq	t0,0(a0)
	mskqh	t0,t5,t0
	stq	t0,0(a0)
bzero_done:
	RET

	END(bzero)
