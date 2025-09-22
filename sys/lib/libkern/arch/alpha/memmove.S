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

#include <machine/asm.h>

/*
 * Copy a bytes within the kernel's address space.  The bcopy and memmove
 * variants handle overlapping regions, the memcpy variant does not.
 *
 * void* memcpy(void *to, void *from, size_t len);
 * void* memmove(void *to, void *from, size_t len);
 * void bcopy(void *from, void *to, size_t len);
 */
LEAF(memcpy,3)
	/* Swap arguments, also saving the original `from' in v0 */
	cmoveq  zero,a0,v0
	cmoveq  zero,a1,a0
	cmoveq  zero,v0,a1

 	/* Check for zero length */
	beq	a2,bcopy_done

	br	bcopy_forward

XLEAF(memmove,3)
	/* Swap arguments, also saving the original `from' in v0 */
	cmoveq  zero,a0,v0
	cmoveq  zero,a1,a0
	cmoveq  zero,v0,a1

XLEAF(bcopy,3)
 	/* Check for zero length */
	beq	a2,bcopy_done

	/* Check for overlap */
	subq	a1,a0,t5
	cmpult	t5,a2,t5
	bne	t5,bcopy_overlap

bcopy_forward:
	/* a3 = end address */
	addq	a0,a2,a3

	/* Get the first word */
	ldq_u	t2,0(a0)

	/* Do they have the same alignment? */
	xor	a0,a1,t0
	and	t0,7,t0
	and	a1,7,t1
	bne	t0,bcopy_different_alignment

	/* src & dst have same alignment */
	beq	t1,bcopy_all_aligned

	ldq_u	t3,0(a1)
	addq	a2,t1,a2
	mskqh	t2,a0,t2
	mskql	t3,a0,t3
	or	t2,t3,t2

	/* Dst is 8-byte aligned */

bcopy_all_aligned:
	/* If less than 8 bytes,skip loop */
	subq	a2,1,t0
	and	a2,7,a2
	bic	t0,7,t0
	beq	t0,bcopy_samealign_lp_end

bcopy_samealign_lp:
	stq_u	t2,0(a1)
	addq	a1,8,a1
	ldq_u	t2,8(a0)
	subq	t0,8,t0
	addq	a0,8,a0
	bne	t0,bcopy_samealign_lp

bcopy_samealign_lp_end:
	/* If we're done, exit */
	bne	a2,bcopy_small_left
	stq_u	t2,0(a1)
	RET

bcopy_small_left:
	mskql	t2,a2,t4
	ldq_u	t3,0(a1)
	mskqh	t3,a2,t3
	or	t4,t3,t4
	stq_u	t4,0(a1)
	RET

bcopy_different_alignment:
	/*
	 * this is the fun part
	 */
	addq	a0,a2,a3
	cmpule	a2,8,t0
	bne	t0,bcopy_da_finish

	beq	t1,bcopy_da_noentry

	/* Do the initial partial word */
	subq	zero,a1,t0
	and	t0,7,t0
	ldq_u	t3,7(a0)
	extql	t2,a0,t2
	extqh	t3,a0,t3
	or	t2,t3,t5
	insql	t5,a1,t5
	ldq_u	t6,0(a1)
	mskql	t6,a1,t6
	or	t5,t6,t5
	stq_u	t5,0(a1)
	addq	a0,t0,a0
	addq	a1,t0,a1
	subq	a2,t0,a2
	ldq_u	t2,0(a0)

bcopy_da_noentry:
	subq	a2,1,t0
	bic	t0,7,t0
	and	a2,7,a2
	beq	t0,bcopy_da_finish2

bcopy_da_lp:
	ldq_u	t3,7(a0)
	addq	a0,8,a0
	extql	t2,a0,t4
	extqh	t3,a0,t5
	subq	t0,8,t0
	or	t4,t5,t5
	stq	t5,0(a1)
	addq	a1,8,a1
	beq	t0,bcopy_da_finish1
	ldq_u	t2,7(a0)
	addq	a0,8,a0
	extql	t3,a0,t4
	extqh	t2,a0,t5
	subq	t0,8,t0
	or	t4,t5,t5
	stq	t5,0(a1)
	addq	a1,8,a1
	bne	t0,bcopy_da_lp

bcopy_da_finish2:
	/* Do the last new word */
	mov	t2,t3

bcopy_da_finish1:
	/* Do the last partial word */
	ldq_u	t2,-1(a3)
	extql	t3,a0,t3
	extqh	t2,a0,t2
	or	t2,t3,t2
	br	zero,bcopy_samealign_lp_end

bcopy_da_finish:
	/* Do the last word in the next source word */
	ldq_u	t3,-1(a3)
	extql	t2,a0,t2
	extqh	t3,a0,t3
	or	t2,t3,t2
	insqh	t2,a1,t3
	insql	t2,a1,t2
	lda	t4,-1(zero)
	mskql	t4,a2,t5
	cmovne	t5,t5,t4
	insqh	t4,a1,t5
	insql	t4,a1,t4
	addq	a1,a2,a4
	ldq_u	t6,0(a1)
	ldq_u	t7,-1(a4)
	bic	t6,t4,t6
	bic	t7,t5,t7
	and	t2,t4,t2
	and	t3,t5,t3
	or	t2,t6,t2
	or	t3,t7,t3
	stq_u	t3,-1(a4)
	stq_u	t2,0(a1)
	RET

bcopy_overlap:
	/*
	 * Basically equivalent to previous case, only backwards.
	 * Not quite as highly optimized
	 */
	addq	a0,a2,a3
	addq	a1,a2,a4

	/* less than 8 bytes - don't worry about overlap */
	cmpule	a2,8,t0
	bne	t0,bcopy_ov_short

	/* Possibly do a partial first word */
	and	a4,7,t4
	beq	t4,bcopy_ov_nostart2
	subq	a3,t4,a3
	subq	a4,t4,a4
	ldq_u	t1,0(a3)
	subq	a2,t4,a2
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
	bic	a2,7,t4
	and	a2,7,a2
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
	beq	a2,bcopy_done

	ldq_u	t0,0(a0)
	ldq_u	t1,7(a0)
	ldq_u	t2,0(a1)
	extql	t0,a0,t0
	extqh	t1,a0,t1
	or	t0,t1,t0
	insql	t0,a1,t0
	mskql	t2,a1,t2
	or	t2,t0,t2
	stq_u	t2,0(a1)

bcopy_done:
	RET

bcopy_ov_short:
	ldq_u	t2,0(a0)
	br	zero,bcopy_da_finish

	END(memcpy)
