/*	$OpenBSD: copy_subr.S,v 1.8 2025/07/19 05:23:37 miod Exp $	*/
/*
 * Mach Operating System
 * Copyright (c) 1993-1992 Carnegie Mellon University
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

#include <machine/asm.h>

/*
 * copy count bytes of data from source to destination
 * Don Harper (don@omron.co.jp), Omron Corporation.
 */

#if defined(MEMCPY) || defined(MEMMOVE)
#define	SRC	%r3
#define	DEST	%r2
#define	SAVE	%r5
#else
#define	SRC	%r2
#define	DEST	%r3
#endif
#define	LEN	%r4

#ifdef MEMCPY
ENTRY(memcpy)
#endif
#ifdef MEMMOVE
ENTRY(memmove)
#endif
#ifdef BCOPY
#define	OVBCOPY
ENTRY(bcopy)
#endif

#if defined(MEMCPY) || defined(MEMMOVE)
	or	SAVE, DEST, %r0
#endif

	bcnd	eq0,LEN,bcopy_out		/* nothing to do if == 0 */

/*
 * check position of source and destination data
 */
	cmp 	%r9,SRC,DEST	/* compare source address to destination */
	bb1	eq,%r9,bcopy_out		/* nothing to do if equal */
#if defined(MEMMOVE) || defined(OVBCOPY)
	bb1	lo,%r9,bcopy_reverse			/* reverse copy if src < dest */
#endif

/*
 * source address is greater than destination address, or we do
 * not have to care about overlapping areas: copy forward
 */
	cmp 	%r9,LEN,16	/* see if we have at least 16 bytes */
	bb1	lt,%r9,f_byte_copy		/* copy bytes for small data length */
/*
 * determine copy strategy based on alignment of source and destination
 */
	mask	%r6,SRC,3	/* get 2 low order bits of source address */
	mask	%r7,DEST,3	/* get 2 low order bits of destination addr */
	mak	%r6,%r6,0<4>	/* convert source bits to table offset */
	mak	%r7,%r7,0<2>	/* convert destination bits to table offset */
	or.u	%r12,%r0,%hi16(f_strat)
	or	%r12,%r12,%lo16(f_strat)
	addu	%r6,%r6,%r7	/* compute final table offset for strategy */
	ld	%r12,%r12,%r6	/* load the strategy routine */
	jmp	%r12		/* branch to strategy routine */

/*
 * Copy three bytes from src to destination then copy words
 */
ASLOCAL(f_3byte_word_copy)
	ld.bu	%r6,SRC,0	/* load byte from source */
	ld.bu	%r7,SRC,1	/* load byte from source */
	ld.bu	%r8,SRC,2	/* load byte from source */
	st.b	%r6,DEST,0	/* store byte to destination */
	st.b	%r7,DEST,1	/* store byte to destination */
	st.b	%r8,DEST,2	/* store byte to destination */
	addu	SRC,SRC,3	/* increment source pointer */
	addu	DEST,DEST,3	/* increment destination pointer */
	br.n	f_word_copy		/* copy full words */
	 subu	LEN,LEN,3	/* decrement length */

/*
 * Copy 1 halfword from src to destination then copy words
 */
ASLOCAL(f_1half_word_copy)
	ld.hu	%r6,SRC,0	/* load half-word from source */
	st.h	%r6,DEST,0	/* store half-word to destination */
	addu	SRC,SRC,2	/* increment source pointer */
	addu	DEST,DEST,2	/* increment destination pointer */
	br.n	f_word_copy		/* copy full words */
	 subu	LEN,LEN,2	/* decrement remaining length */

/*
 * Copy 1 byte from src to destination then copy words
 */
ASLOCAL(f_1byte_word_copy)
	ld.bu	%r6,SRC,0	/* load 1 byte from source */
	st.b	%r6,DEST,0	/* store 1 byte to destination */
	addu	SRC,SRC,1	/* increment source pointer */
	addu	DEST,DEST,1	/* increment destination pointer */
	subu	LEN,LEN,1	/* decrement remaining length */
	/* FALLTHROUGH */
/*
 * Copy as many full words as possible, 4 words per loop
 */
ASLOCAL(f_word_copy)
	cmp	%r10,LEN,16	/* see if we have 16 bytes remaining */
	bb1	lo,%r10,f_byte_copy		/* not enough left, copy bytes */
	ld	%r6,SRC,0	/* load first word */
	ld	%r7,SRC,4	/* load second word */
	ld	%r8,SRC,8	/* load third word */
	ld	%r9,SRC,12	/* load fourth word */
	st	%r6,DEST,0	/* store first word */
	st	%r7,DEST,4	/* store second word */
	st 	%r8,DEST,8	/* store third word */
	st 	%r9,DEST,12	/* store fourth word */
	addu	SRC,SRC,16	/* increment source pointer */
	addu	DEST,DEST,16	/* increment destination pointer */
	br.n	f_word_copy		/* branch to copy another block */
	 subu	LEN,LEN,16	/* decrement remaining length */

ASLOCAL(f_1byte_half_copy)
	ld.bu	%r6,SRC,0	/* load 1 byte from source */
	st.b	%r6,DEST,0	/* store 1 byte to destination */
	addu	SRC,SRC,1	/* increment source pointer */
	addu	DEST,DEST,1	/* increment destination pointer */
	subu	LEN,LEN,1	/* decrement remaining length */
	/* FALLTHROUGH */

ASLOCAL(f_half_copy)
	cmp	%r10,LEN,16	/* see if we have 16 bytes remaining */
	bb1	lo,%r10,f_byte_copy		/* not enough left, copy bytes */
	ld.hu	%r6,SRC,0	/* load first half-word */
	ld.hu	%r7,SRC,2	/* load second half-word */
	ld.hu	%r8,SRC,4	/* load third half-word */
	ld.hu	%r9,SRC,6	/* load fourth half-word */
	ld.hu	%r10,SRC,8	/* load fifth half-word */
	ld.hu	%r11,SRC,10	/* load sixth half-word */
	ld.hu	%r12,SRC,12	/* load seventh half-word */
	ld.hu	%r13,SRC,14	/* load eighth half-word */
	st.h	%r6,DEST,0	/* store first half-word */
	st.h	%r7,DEST,2	/* store second half-word */
	st.h 	%r8,DEST,4	/* store third half-word */
	st.h 	%r9,DEST,6	/* store fourth half-word */
	st.h	%r10,DEST,8	/* store fifth half-word */
	st.h	%r11,DEST,10	/* store sixth half-word */
	st.h 	%r12,DEST,12	/* store seventh half-word */
	st.h 	%r13,DEST,14	/* store eighth half-word */
	addu	SRC,SRC,16	/* increment source pointer */
	addu	DEST,DEST,16	/* increment destination pointer */
	br.n	f_half_copy		/* branch to copy another block */
	 subu	LEN,LEN,16	/* decrement remaining length */

ASLOCAL(f_byte_copy)
	bcnd	eq0,LEN,bcopy_out		/* branch if nothing left to copy */
	ld.bu	%r6,SRC,0	/* load byte from source */
	st.b	%r6,DEST,0	/* store byte in destination */
	addu	SRC,SRC,1	/* increment source pointer */
	addu	DEST,DEST,1	/* increment destination pointer */
	br.n	f_byte_copy		/* branch for next byte */
	 subu	LEN,LEN,1	/* decrement remaining length */

#if defined(MEMMOVE) || defined(OVBCOPY)
/*
 * source address is less than destination address, copy in reverse
 */
ASLOCAL(bcopy_reverse)
/*
 * start copy pointers at end of data
 */
	addu	SRC,SRC,LEN	/* start source at end of data */
	addu	DEST,DEST,LEN	/* start destination at end of data */
/*
 * check for short data
 */
	cmp 	%r9,LEN,16	/* see if we have at least 16 bytes */
	bb1	lt,%r9,r_byte_copy		/* copy bytes for small data length */
/*
 *	determine copy strategy based on alignment of source and destination
 */
	mask	%r6,SRC,3	/* get 2 low order bits of source address */
	mask	%r7,DEST,3	/* get 2 low order bits of destination addr */
	mak	%r6,%r6,0<4>	/* convert source bits to table offset */
	mak	%r7,%r7,0<2>	/* convert destination bits to table offset */
	or.u	%r12,%r0,%hi16(r_strat)
	or	%r12,%r12,%lo16(r_strat)
	addu	%r6,%r6,%r7	/* compute final table offset for strategy */
	ld	%r12,%r12,%r6	/* load the strategy routine */
	jmp	%r12		/* branch to strategy routine */

/*
 * Copy three bytes from src to destination then copy words
 */
ASLOCAL(r_3byte_word_copy)
	subu	SRC,SRC,3	/* decrement source pointer */
	subu	DEST,DEST,3	/* decrement destination pointer */
	ld.bu	%r6,SRC,0	/* load byte from source */
	ld.bu	%r7,SRC,1	/* load byte from source */
	ld.bu	%r8,SRC,2	/* load byte from source */
	st.b	%r6,DEST,0	/* store byte to destination */
	st.b	%r7,DEST,1	/* store byte to destination */
	st.b	%r8,DEST,2	/* store byte to destination */
	br.n	r_word_copy		/* copy full words */
	 subu	LEN,LEN,3	/* decrement length */

/*
 * Copy 1 halfword from src to destination then copy words
 */
ASLOCAL(r_1half_word_copy)
	subu	SRC,SRC,2	/* decrement source pointer */
	subu	DEST,DEST,2	/* decrement destination pointer */
	ld.hu	%r6,SRC,0	/* load half-word from source */
	st.h	%r6,DEST,0	/* store half-word to destination */
	br.n	r_word_copy		/* copy full words */
	 subu	LEN,LEN,2	/* decrement remaining length */

/*
 * Copy 1 byte from src to destination then copy words
 */
ASLOCAL(r_1byte_word_copy)
	subu	SRC,SRC,1	/* decrement source pointer */
	subu	DEST,DEST,1	/* decrement destination pointer */
	ld.bu	%r6,SRC,0	/* load 1 byte from source */
	st.b	%r6,DEST,0	/* store 1 byte to destination */
	subu	LEN,LEN,1	/* decrement remaining length */
	/* FALLTHROUGH */
/*
 * Copy as many full words as possible, 4 words per loop
 */
ASLOCAL(r_word_copy)
	cmp	%r10,LEN,16	/* see if we have 16 bytes remaining */
	bb1	lo,%r10,r_byte_copy		/* not enough left, copy bytes */
	subu	SRC,SRC,16	/* decrement source pointer */
	subu	DEST,DEST,16	/* decrement destination pointer */
	ld	%r6,SRC,0	/* load first word */
	ld	%r7,SRC,4	/* load second word */
	ld	%r8,SRC,8	/* load third word */
	ld	%r9,SRC,12	/* load fourth word */
	st	%r6,DEST,0	/* store first word */
	st	%r7,DEST,4	/* store second word */
	st 	%r8,DEST,8	/* store third word */
	st 	%r9,DEST,12	/* store fourth word */
	br.n	r_word_copy		/* branch to copy another block */
	 subu	LEN,LEN,16	/* decrement remaining length */

ASLOCAL(r_1byte_half_copy)
	subu	SRC,SRC,1	/* decrement source pointer */
	subu	DEST,DEST,1	/* decrement destination pointer */
	ld.bu	%r6,SRC,0	/* load 1 byte from source */
	st.b	%r6,DEST,0	/* store 1 byte to destination */
	subu	LEN,LEN,1	/* decrement remaining length */
	/* FALLTHROUGH */

ASLOCAL(r_half_copy)
	cmp	%r10,LEN,16	/* see if we have 16 bytes remaining */
	bb1	lo,%r10,r_byte_copy		/* not enough left, copy bytes */
	subu	SRC,SRC,16	/* decrement source pointer */
	subu	DEST,DEST,16	/* decrement destination pointer */
	ld.hu	%r6,SRC,0	/* load first half-word */
	ld.hu	%r7,SRC,2	/* load second half-word */
	ld.hu	%r8,SRC,4	/* load third half-word */
	ld.hu	%r9,SRC,6	/* load fourth half-word */
	ld.hu	%r10,SRC,8	/* load fifth half-word */
	ld.hu	%r11,SRC,10	/* load sixth half-word */
	ld.hu	%r12,SRC,12	/* load seventh half-word */
	ld.hu	%r13,SRC,14	/* load eighth half-word */
	st.h	%r6,DEST,0	/* store first half-word */
	st.h	%r7,DEST,2	/* store second half-word */
	st.h 	%r8,DEST,4	/* store third half-word */
	st.h 	%r9,DEST,6	/* store fourth half-word */
	st.h	%r10,DEST,8	/* store fifth half-word */
	st.h	%r11,DEST,10	/* store sixth half-word */
	st.h 	%r12,DEST,12	/* store seventh half-word */
	st.h 	%r13,DEST,14	/* store eighth half-word */
	br.n	r_half_copy		/* branch to copy another block */
	 subu	LEN,LEN,16	/* decrement remaining length */

ASLOCAL(r_byte_copy)
	bcnd	eq0,LEN,bcopy_out		/* branch if nothing left to copy */
	subu	SRC,SRC,1		/* decrement source pointer */
	subu	DEST,DEST,1		/* decrement destination pointer */
	ld.bu	%r6,SRC,0		/* load byte from source */
	st.b	%r6,DEST,0		/* store byte in destination */
	br.n	r_byte_copy		/* branch for next byte */
	 subu	LEN,LEN,1		/* decrement remaining length */
#endif	/* MEMMOVE || OVBCOPY */

ASLOCAL(bcopy_out)
#if defined(MEMCPY) || defined(MEMMOVE)
	jmp.n	%r1		/* all done, return to caller */
	 or	%r2, SAVE, %r0
#else
	jmp	%r1		/* all done, return to caller */
#endif

	.section .rodata
	.align	2
ASLOCAL(f_strat)
	.word	f_word_copy
	.word	f_byte_copy
	.word	f_half_copy
	.word	f_byte_copy
	.word	f_byte_copy
	.word	f_3byte_word_copy
	.word	f_byte_copy
	.word	f_1byte_half_copy
	.word	f_half_copy
	.word	f_byte_copy
	.word	f_1half_word_copy
	.word	f_byte_copy
	.word	f_byte_copy
	.word	f_1byte_half_copy
	.word	f_byte_copy
	.word	f_1byte_word_copy

#if defined(MEMMOVE) || defined(OVBCOPY)
ASLOCAL(r_strat)
	.word	r_word_copy
	.word	r_byte_copy
	.word	r_half_copy
	.word	r_byte_copy
	.word	r_byte_copy
	.word	r_1byte_word_copy
	.word	r_byte_copy
	.word	r_1byte_half_copy
	.word	r_half_copy
	.word	r_byte_copy
	.word	r_1half_word_copy
	.word	r_byte_copy
	.word	r_byte_copy
	.word	r_1byte_half_copy
	.word	r_byte_copy
	.word	r_3byte_word_copy
#endif
