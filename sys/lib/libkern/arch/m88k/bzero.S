/*	$OpenBSD: bzero.S,v 1.4 2022/12/06 18:51:00 guenther Exp $	*/
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

/*######################################################################*/

/*
 * April 1990, Omron Corporation
 * jfriedl@nff.ncl.omron.co.jp
 *
 * void bzero(destination, length)
 *
 * Clear (set to zero) LENGTH bytes of memory starting at DESTINATION.
 * Note that there is no return value.
 *
 * This is fast. Really fast. Especially for long lengths.
 */
#define R_dest			%r2
#define R_len			%r3

#define R_bytes			%r4
#define R_mark_address		%r5
#define R_addr			%r6	/* R_addr && R_temp SHARE */
#define R_temp			%r6	/* R_addr && R_temp SHARE */

ENTRY(bzero)
	/*
	 * If the destination is not word aligned, we'll word align
	 * it first to make things easier.
	 *
	 * We'll check to see first if bit #0 is set and then bit #1
	 * (of the destination address). If either are set, it's
	 * not word aligned.
	 */
	bb1	0, R_dest, not_initially_word_aligned
	bb1	1, R_dest, not_initially_word_aligned

ASLOCAL(now_word_aligned)
	/*
	 * before we get into the main loop, grab the
	 * address of the label "mark" below.
	 */
	or.u	R_mark_address, %r0, %hi16(mark)
	or	R_mark_address, R_mark_address, %lo16(mark)

ASLOCAL(top_of_main_loop)
#define MAX_AT_ONE_TIME 128
	/*
	 * Now we find out how many words we can zero-fill in a row.
	 * We do this by doing something like:
	 *
	 *	bytes &= 0xfffffffc;
	 *	if (bytes > MAX_AT_ONE_TIME)
	 *		bytes = MAX_AT_ONE_TIME;
	 */

	/*
	 * Clear lower two bits of length to give us the number of bytes
	 * ALIGNED TO THE WORD LENGTH remaining to move.
	 */
	clr	R_bytes, R_len, 2<0>

	/* if we're done clearing WORDS, jump out */
	bcnd	eq0, R_bytes, done_doing_words

	/* if the number of bytes > MAX_AT_ONE_TIME, do only the max */
	cmp	R_temp, R_bytes, MAX_AT_ONE_TIME
	bb1	lt, R_temp, 1f

	/*
	 * Since we're doing the max, we know exactly where we're
	 * jumping (the first one in the list!), so we can jump
	 * right there.  However, we've still got to adjust
	 * the length, so we'll jump to where we adjust the length
	 * which just happens to fall through to the first store zero
	 * in the list.
	 *
	 * Note, however, that we're jumping to an instruction that
	 * would be in the delay slot for the jump in front of it,
	 * so if you change things here, WATCH OUT.
	 */
	br.n	do_max
	 or	R_bytes, %r0, MAX_AT_ONE_TIME

1:
	/*
	 * Now we have the number of bytes to zero during this iteration,
	 * (which, as it happens, is the last iteration if we're here).
	 * We'll calculate the proper place to jump and then jump there,
	 * after adjusting the length.  NOTE that there is a label between
	 * the "jmp.n" and the "subu" below... the "subu" is NOT always
	 * executed in the delay slot of the "jmp.n".
 	 */
	subu	R_addr, R_mark_address, R_bytes

	/* and go there (after adjusting the length via ".n") */
	jmp.n	R_addr
ASLOCAL(do_max)
	  subu	R_len, R_len, R_bytes	/* NOTE: this is in the delay slot! */

	st	%r0, R_dest, 0x7c	/* 128 */
	st	%r0, R_dest, 0x78	/* 124 */
	st	%r0, R_dest, 0x74	/* 120 */
	st	%r0, R_dest, 0x70	/* 116 */
	st	%r0, R_dest, 0x6c	/* 112 */
	st	%r0, R_dest, 0x68	/* 108 */
	st	%r0, R_dest, 0x64	/* 104 */
	st	%r0, R_dest, 0x60	/* 100 */
	st	%r0, R_dest, 0x5c	/*  96 */
	st	%r0, R_dest, 0x58	/*  92 */
	st	%r0, R_dest, 0x54	/*  88 */
	st	%r0, R_dest, 0x50	/*  84 */
	st	%r0, R_dest, 0x4c	/*  80 */
	st	%r0, R_dest, 0x48	/*  76 */
	st	%r0, R_dest, 0x44	/*  72 */
	st	%r0, R_dest, 0x40	/*  68 */
	st	%r0, R_dest, 0x3c	/*  64 */
	st	%r0, R_dest, 0x38	/*  60 */
	st	%r0, R_dest, 0x34	/*  56 */
	st	%r0, R_dest, 0x30	/*  52 */
	st	%r0, R_dest, 0x2c	/*  44 */
	st	%r0, R_dest, 0x28	/*  40 */
	st	%r0, R_dest, 0x24	/*  36 */
	st	%r0, R_dest, 0x20	/*  32 */
	st	%r0, R_dest, 0x1c	/*  28 */
	st	%r0, R_dest, 0x18	/*  24 */
	st	%r0, R_dest, 0x14	/*  20 */
	st	%r0, R_dest, 0x10	/*  16 */
	st	%r0, R_dest, 0x0c	/*  12 */
	st	%r0, R_dest, 0x08	/*   8 */
	st	%r0, R_dest, 0x04	/*   4 */
	st	%r0, R_dest, 0x00	/*   0 */

ASLOCAL(mark)
	br.n	top_of_main_loop
	 addu	R_dest, R_dest, R_bytes	/* bump up the dest address */

ASLOCAL(done_doing_words)
	bcnd	ne0, R_len, 1f
	jmp	%r1

1:
	subu	R_len, R_len, 1
	bcnd.n	ne0, R_len, 1b
	 st.b	%r0, R_dest, R_len
1:
	jmp	%r1

ASLOCAL(not_initially_word_aligned)
	/*
	 * Bzero to word-align the address (at least if the length allows it).
	 */
	bcnd	eq0, R_len, 1b
	st.b	%r0, R_dest, 0
	addu	R_dest, R_dest, 1
	mask	R_temp, R_dest, 0x3
	bcnd.n	eq0, R_temp, now_word_aligned
	 subu	R_len, R_len, 1
	br	not_initially_word_aligned

#undef	R_dest
#undef	R_len
#undef	R_bytes
#undef	R_mark_address
#undef	R_addr
#undef	R_temp
#undef	MAX_AT_ONE_TIME
