/*	$OpenBSD: memmove.S,v 1.6 2015/08/31 02:53:56 guenther Exp $	*/
/*	$NetBSD: bcopy.S,v 1.3 1996/10/17 03:08:11 cgd Exp $	*/

/*
 * Copyright (c) 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Trevor Blackwell.  Support for use as memcpy() and memmove()
 *	   added by Chris Demetriou.
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

#define	SRCREG		a1
#define	DSTREG		a0

#define	SIZEREG		a2

/*
 * Copy bytes.
 *
 * char *memmove(void *to, const void *from, size_t len);
 *
 * No matter how invoked, the source and destination registers
 * for calculation.  There's no point in copying them to "working"
 * registers, since the code uses their values "in place," and
 * copying them would be slower.
 */

LEAF(memmove,3)
	/* set up return value, while we still can */
	mov	DSTREG,v0

	/* Check for zero length */
	beq	SIZEREG,bcopy_done

	/* Check for overlap */
	subq	DSTREG,SRCREG,t5
	cmpult	t5,SIZEREG,t5
	bne	t5,bcopy_overlap

	/* a3 = end address */
	addq	SRCREG,SIZEREG,a3

	/* Get the first word */
	ldq_u	t2,0(SRCREG)

	/* Do they have the same alignment? */
	xor	SRCREG,DSTREG,t0
	and	t0,7,t0
	and	DSTREG,7,t1
	bne	t0,bcopy_different_alignment

	/* src & dst have same alignment */
	beq	t1,bcopy_all_aligned

	ldq_u	t3,0(DSTREG)
	addq	SIZEREG,t1,SIZEREG
	mskqh	t2,SRCREG,t2
	mskql	t3,SRCREG,t3
	or	t2,t3,t2

	/* Dst is 8-byte aligned */

bcopy_all_aligned:
	/* If less than 8 bytes,skip loop */
	subq	SIZEREG,1,t0
	and	SIZEREG,7,SIZEREG
	bic	t0,7,t0
	beq	t0,bcopy_samealign_lp_end

bcopy_samealign_lp:
	stq_u	t2,0(DSTREG)
	addq	DSTREG,8,DSTREG
	ldq_u	t2,8(SRCREG)
	subq	t0,8,t0
	addq	SRCREG,8,SRCREG
	bne	t0,bcopy_samealign_lp

bcopy_samealign_lp_end:
	/* If we're done, exit */
	bne	SIZEREG,bcopy_small_left
	stq_u	t2,0(DSTREG)
	RET

bcopy_small_left:
	mskql	t2,SIZEREG,t4
	ldq_u	t3,0(DSTREG)
	mskqh	t3,SIZEREG,t3
	or	t4,t3,t4
	stq_u	t4,0(DSTREG)
	RET

bcopy_different_alignment:
	/*
	 * this is the fun part
	 */
	addq	SRCREG,SIZEREG,a3
	cmpule	SIZEREG,8,t0
	bne	t0,bcopy_da_finish

	beq	t1,bcopy_da_noentry

	/* Do the initial partial word */
	subq	zero,DSTREG,t0
	and	t0,7,t0
	ldq_u	t3,7(SRCREG)
	extql	t2,SRCREG,t2
	extqh	t3,SRCREG,t3
	or	t2,t3,t5
	insql	t5,DSTREG,t5
	ldq_u	t6,0(DSTREG)
	mskql	t6,DSTREG,t6
	or	t5,t6,t5
	stq_u	t5,0(DSTREG)
	addq	SRCREG,t0,SRCREG
	addq	DSTREG,t0,DSTREG
	subq	SIZEREG,t0,SIZEREG
	ldq_u	t2,0(SRCREG)

bcopy_da_noentry:
	subq	SIZEREG,1,t0
	bic	t0,7,t0
	and	SIZEREG,7,SIZEREG
	beq	t0,bcopy_da_finish2

bcopy_da_lp:
	ldq_u	t3,7(SRCREG)
	addq	SRCREG,8,SRCREG
	extql	t2,SRCREG,t4
	extqh	t3,SRCREG,t5
	subq	t0,8,t0
	or	t4,t5,t5
	stq	t5,0(DSTREG)
	addq	DSTREG,8,DSTREG
	beq	t0,bcopy_da_finish1
	ldq_u	t2,7(SRCREG)
	addq	SRCREG,8,SRCREG
	extql	t3,SRCREG,t4
	extqh	t2,SRCREG,t5
	subq	t0,8,t0
	or	t4,t5,t5
	stq	t5,0(DSTREG)
	addq	DSTREG,8,DSTREG
	bne	t0,bcopy_da_lp

bcopy_da_finish2:
	/* Do the last new word */
	mov	t2,t3

bcopy_da_finish1:
	/* Do the last partial word */
	ldq_u	t2,-1(a3)
	extql	t3,SRCREG,t3
	extqh	t2,SRCREG,t2
	or	t2,t3,t2
	br	zero,bcopy_samealign_lp_end

bcopy_da_finish:
	/* Do the last word in the next source word */
	ldq_u	t3,-1(a3)
	extql	t2,SRCREG,t2
	extqh	t3,SRCREG,t3
	or	t2,t3,t2
	insqh	t2,DSTREG,t3
	insql	t2,DSTREG,t2
	lda	t4,-1(zero)
	mskql	t4,SIZEREG,t5
	cmovne	t5,t5,t4
	insqh	t4,DSTREG,t5
	insql	t4,DSTREG,t4
	addq	DSTREG,SIZEREG,a4
	ldq_u	t6,0(DSTREG)
	ldq_u	t7,-1(a4)
	bic	t6,t4,t6
	bic	t7,t5,t7
	and	t2,t4,t2
	and	t3,t5,t3
	or	t2,t6,t2
	or	t3,t7,t3
	stq_u	t3,-1(a4)
	stq_u	t2,0(DSTREG)
	RET

bcopy_overlap:
	/*
	 * Basically equivalent to previous case, only backwards.
	 * Not quite as highly optimized
	 */
	addq	SRCREG,SIZEREG,a3
	addq	DSTREG,SIZEREG,a4

	/* less than 8 bytes - don't worry about overlap */
	cmpule	SIZEREG,8,t0
	bne	t0,bcopy_ov_short

	/* Possibly do a partial first word */
	and	a4,7,t4
	beq	t4,bcopy_ov_nostart2
	subq	a3,t4,a3
	subq	a4,t4,a4
	ldq_u	t1,0(a3)
	subq	SIZEREG,t4,SIZEREG
	ldq_u	t2,7(a3)
	ldq	t3,0(a4)
	extql	t1,a3,t1
	extqh	t2,a3,t2
	or	t1,t2,t1
	mskqh	t3,t4,t3
	mskql	t1,t4,t1
	or	t1,t3,t1
	stq	t1,0(a4)

bcopy_ov_nostart2:
	bic	SIZEREG,7,t4
	and	SIZEREG,7,SIZEREG
	beq	t4,bcopy_ov_lp_end

bcopy_ov_lp:
	/* This could be more pipelined, but it doesn't seem worth it */
	ldq_u	t0,-8(a3)
	subq	a4,8,a4
	ldq_u	t1,-1(a3)
	subq	a3,8,a3
	extql	t0,a3,t0
	extqh	t1,a3,t1
	subq	t4,8,t4
	or	t0,t1,t0
	stq	t0,0(a4)
	bne	t4,bcopy_ov_lp

bcopy_ov_lp_end:
	beq	SIZEREG,bcopy_done

	ldq_u	t0,0(SRCREG)
	ldq_u	t1,7(SRCREG)
	ldq_u	t2,0(DSTREG)
	extql	t0,SRCREG,t0
	extqh	t1,SRCREG,t1
	or	t0,t1,t0
	insql	t0,DSTREG,t0
	mskql	t2,DSTREG,t2
	or	t2,t0,t2
	stq_u	t2,0(DSTREG)

bcopy_done:
	RET

bcopy_ov_short:
	ldq_u	t2,0(SRCREG)
	br	zero,bcopy_da_finish

	END_STRONG(memmove)
