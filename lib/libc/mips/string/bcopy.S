/*	$NetBSD: bcopy.S,v 1.3 2009/12/14 00:39:00 matt Exp $	*/

/*
 * Mach Operating System
 * Copyright (c) 1993 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

/*
 *	File:	mips_bcopy.s
 *	Author:	Chris Maeda
 *	Date:	June 1993
 *
 *	Fast copy routine.  Derived from aligned_block_copy.
 */


#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#define _LOCORE		/* XXX not really, just assembly-code source */
#include <machine/endian.h>

#if defined(LIBC_SCCS) && !defined(lint)
#if 0
	ASMSTR("from: @(#)mips_bcopy.s	2.2 CMU 18/06/93")
#else
	ASMSTR("$NetBSD: bcopy.S,v 1.3 2009/12/14 00:39:00 matt Exp $")
#endif
#endif /* LIBC_SCCS and not lint */

#ifdef __ABICALLS__
	.abicalls
#endif

/*
 *	bcopy(caddr_t src, caddr_t dst, unsigned int len)
 *
 *	a0 	src address
 *	a1	dst address
 *	a2	length
 */

#if defined(MEMCOPY) || defined(MEMMOVE)
#ifdef MEMCOPY
#define	FUNCTION	memcpy
#else
#define FUNCTION	memmove
#endif
#define	SRCREG		a1
#define	DSTREG		a0
#else
#define	FUNCTION	bcopy
#define	SRCREG		a0
#define	DSTREG		a1
#endif

#define	SIZEREG		a2

LEAF(FUNCTION)
	.set	noat
	.set	noreorder

#if defined(MEMCOPY) || defined(MEMMOVE)
	/* set up return value, while we still can */
	move	v0,DSTREG
#endif
	/*
	 *	Make sure we can copy forwards.
	 */
	sltu	t0,SRCREG,DSTREG	# t0 == SRCREG < DSTREG
	bne	t0,zero,6f		# copy backwards

	/*
	 * 	There are four alignment cases (with frequency)
	 *	(Based on measurements taken with a DECstation 5000/200
	 *	 inside a Mach kernel.)
	 *
	 * 	aligned   -> aligned		(mostly)
	 * 	unaligned -> aligned		(sometimes)
	 * 	aligned,unaligned -> unaligned	(almost never)
	 *
	 *	Note that we could add another case that checks if
	 *	the destination and source are unaligned but the
	 *	copy is alignable.  eg if src and dest are both
	 *	on a halfword boundary.
	 */
	andi		t1,DSTREG,(SZREG-1)	# get last bits of dest
	bne		t1,zero,3f		# dest unaligned
	andi		t0,SRCREG,(SZREG-1)	# get last bits of src
	bne		t0,zero,5f

	/*
	 *	Forward aligned->aligned copy, 8 words at a time.
	 */
98:
	li		AT,-(SZREG*8)
	and		t0,SIZEREG,AT		# count truncated to multiples
	PTR_ADDU	a3,SRCREG,t0		# run fast loop up to this addr
	sltu		AT,SRCREG,a3		# any work to do?
	beq		AT,zero,2f
	PTR_SUBU	SIZEREG,t0

	/*
	 *	loop body
	 */
1:	# cp
	REG_L		t3,(0*SZREG)(SRCREG)
	REG_L		v1,(1*SZREG)(SRCREG)
	REG_L		t0,(2*SZREG)(SRCREG)
	REG_L		t1,(3*SZREG)(SRCREG)
	PTR_ADDU	SRCREG,SZREG*8
	REG_S		t3,(0*SZREG)(DSTREG)
	REG_S		v1,(1*SZREG)(DSTREG)
	REG_S		t0,(2*SZREG)(DSTREG)
	REG_S		t1,(3*SZREG)(DSTREG)
	REG_L		t1,(-1*SZREG)(SRCREG)
	REG_L		t0,(-2*SZREG)(SRCREG)
	REG_L		v1,(-3*SZREG)(SRCREG)
	REG_L		t3,(-4*SZREG)(SRCREG)
	PTR_ADDU	DSTREG,SZREG*8
	REG_S		t1,(-1*SZREG)(DSTREG)
	REG_S		t0,(-2*SZREG)(DSTREG)
	REG_S		v1,(-3*SZREG)(DSTREG)
	bne		SRCREG,a3,1b
	REG_S		t3,(-4*SZREG)(DSTREG)

	/*
	 *	Copy a word at a time, no loop unrolling.
	 */
2:	# wordcopy
	andi		t2,SIZEREG,(SZREG-1)	# get byte count / SZREG
	PTR_SUBU	t2,SIZEREG,t2		# t2 = words to copy * SZREG
	beq		t2,zero,3f
	PTR_ADDU	t0,SRCREG,t2		# stop at t0
	PTR_SUBU	SIZEREG,SIZEREG,t2
1:
	REG_L		t3,0(SRCREG)
	PTR_ADDU	SRCREG,SZREG
	REG_S		t3,0(DSTREG)
	bne		SRCREG,t0,1b
	PTR_ADDU	DSTREG,SZREG

3:	# bytecopy
	beq		SIZEREG,zero,4f		# nothing left to do?
	nop
1:
	lb		t3,0(SRCREG)
	PTR_ADDU	SRCREG,1
	sb		t3,0(DSTREG)
	PTR_SUBU	SIZEREG,1
	bgtz		SIZEREG,1b
	PTR_ADDU	DSTREG,1

4:	# copydone
	j	ra
	nop

	/*
	 *	Copy from unaligned source to aligned dest.
	 */
5:	# destaligned
	andi		t0,SIZEREG,(SZREG-1)	# t0 = bytecount mod SZREG
	PTR_SUBU	a3,SIZEREG,t0		# number of words to transfer
	beq		a3,zero,3b
	nop
	move		SIZEREG,t0		# this many to do after we are done
	PTR_ADDU	a3,SRCREG,a3		# stop point

1:
	REG_LHI		t3,0(SRCREG)
	REG_LLO		t3,SZREG-1(SRCREG)
	PTR_ADDI	SRCREG,SZREG
	REG_S		t3,0(DSTREG)
	bne		SRCREG,a3,1b
	PTR_ADDI	DSTREG,SZREG

	b		3b
	nop

6:	# backcopy -- based on above
	PTR_ADDU	SRCREG,SIZEREG
	PTR_ADDU	DSTREG,SIZEREG
	andi		t1,DSTREG,SZREG-1	# get last 3 bits of dest
	bne		t1,zero,3f
	andi		t0,SRCREG,SZREG-1	# get last 3 bits of src
	bne		t0,zero,5f

	/*
	 *	Forward aligned->aligned copy, 8*4 bytes at a time.
	 */
	li		AT,(-8*SZREG)
	and		t0,SIZEREG,AT		# count truncated to multiple of 32
	beq		t0,zero,2f		# any work to do?
	PTR_SUBU	SIZEREG,t0
	PTR_SUBU	a3,SRCREG,t0

	/*
	 *	loop body
	 */
1:	# cp
	REG_L		t3,(-4*SZREG)(SRCREG)
	REG_L		v1,(-3*SZREG)(SRCREG)
	REG_L		t0,(-2*SZREG)(SRCREG)
	REG_L		t1,(-1*SZREG)(SRCREG)
	PTR_SUBU	SRCREG,8*SZREG
	REG_S		t3,(-4*SZREG)(DSTREG)
	REG_S		v1,(-3*SZREG)(DSTREG)
	REG_S		t0,(-2*SZREG)(DSTREG)
	REG_S		t1,(-1*SZREG)(DSTREG)
	REG_L		t1,(3*SZREG)(SRCREG)
	REG_L		t0,(2*SZREG)(SRCREG)
	REG_L		v1,(1*SZREG)(SRCREG)
	REG_L		t3,(0*SZREG)(SRCREG)
	PTR_SUBU	DSTREG,8*SZREG
	REG_S		t1,(3*SZREG)(DSTREG)
	REG_S		t0,(2*SZREG)(DSTREG)
	REG_S		v1,(1*SZREG)(DSTREG)
	bne		SRCREG,a3,1b
	REG_S		t3,(0*SZREG)(DSTREG)

	/*
	 *	Copy a word at a time, no loop unrolling.
	 */
2:	# wordcopy
	andi		t2,SIZEREG,SZREG-1	# get byte count / 4
	PTR_SUBU	t2,SIZEREG,t2		# t2 = number of words to copy
	beq		t2,zero,3f
	PTR_SUBU	t0,SRCREG,t2		# stop at t0
	PTR_SUBU	SIZEREG,SIZEREG,t2
1:
	REG_L		t3,-SZREG(SRCREG)
	PTR_SUBU	SRCREG,SZREG
	REG_S		t3,-SZREG(DSTREG)
	bne		SRCREG,t0,1b
	PTR_SUBU	DSTREG,SZREG

3:	# bytecopy
	beq		SIZEREG,zero,4f		# nothing left to do?
	nop
1:
	lb		t3,-1(SRCREG)
	PTR_SUBU	SRCREG,1
	sb		t3,-1(DSTREG)
	PTR_SUBU	SIZEREG,1
	bgtz		SIZEREG,1b
	PTR_SUBU	DSTREG,1

4:	# copydone
	j	ra
	nop

	/*
	 *	Copy from unaligned source to aligned dest.
	 */
5:	# destaligned
	andi		t0,SIZEREG,SZREG-1	# t0 = bytecount mod 4
	PTR_SUBU	a3,SIZEREG,t0		# number of words to transfer
	beq		a3,zero,3b
	nop
	move		SIZEREG,t0		# this many to do after we are done
	PTR_SUBU	a3,SRCREG,a3		# stop point

1:
	REG_LHI		t3,-SZREG(SRCREG)
	REG_LLO		t3,-1(SRCREG)
	PTR_SUBU	SRCREG,SZREG
	REG_S		t3,-SZREG(DSTREG)
	bne		SRCREG,a3,1b
	PTR_SUBU	DSTREG,SZREG

	b		3b
	nop

	.set	reorder
	.set	at
	END(FUNCTION)
