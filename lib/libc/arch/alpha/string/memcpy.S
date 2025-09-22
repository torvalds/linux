/*	$OpenBSD: memcpy.S,v 1.6 2015/08/31 02:53:56 guenther Exp $	*/
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
 * char *memcpy(void *to, const void *from, size_t len);
 *
 * No matter how invoked, the source and destination registers
 * for calculation.  There's no point in copying them to "working"
 * registers, since the code uses their values "in place," and
 * copying them would be slower.
 */

LEAF(memcpy,3)
	/* set up return value, while we still can */
	mov	DSTREG,v0

	/* Check for zero length */
	beq	SIZEREG,bcopy_done

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
bcopy_done:
	RET

	END_STRONG(memcpy)
