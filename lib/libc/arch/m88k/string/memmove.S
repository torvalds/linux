/*
 * Copyright (c) 2025, Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This tries to be as efficient as possible, by performing 32-bit loads and
 * stores, while keeping the logic deciding what to do as short as possible
 * to be as less hurtful as possible to small copies, and those which, due
 * to alignment constraints, have to be performed byte by byte anyway.
 *
 * The general logic is as follows:
 * - check the low two bits of the difference between src and dst. If bit 0 set,
 *   perform byte-by-byte copies; if bit 1 is set, perform halfword-by-halfword
 *   copies; otherwise, perform word-by-word copies.
 * - the overlap occurs only if src < dst < src + len, which, in unsigned
 *   arithmetic, is equivalent to (dst - src) < len.
 * - if no overlap, copy forward by jumping into memcpy; if overlap, copy
 *   backwards.
 * - copy the necessary byte and/or halfword to reach copy unit alignment, do
 *   the bulk of the copy, and copy any possibly remaining byte and/or halfword
 *   to complete.
 */

#include "DEFS.h"

	ENTRY(memmove)

#define	SAVE_DST	%r2	/* never modified */
#define	SRC		%r3
#define	LEN		%r4
#define	UNIT		%r5
#define	TMP		%r6
#define	TMP2		%r7
#define	DST		%r8

	bcnd.n	eq0, LEN, .L_done	/* nothing to do! */
	 xor	TMP, SAVE_DST, SRC
	bcnd.n	eq0, TMP, .L_done	/* nothing to do! */
	 subu	TMP2, SAVE_DST, SRC
	cmp	UNIT, TMP2, LEN
	bb1.n	lo, UNIT, .L_overlap
	 or	DST, SAVE_DST, %r0

	/* no overlap, delegate operation to memcpy() */
#ifdef __PIC__
	br	_HIDDEN(memcpy)#plt
#else
	br	_HIDDEN(memcpy)
#endif

.L_overlap:
	 addu	SRC, SRC, LEN
	bb1.n	0, TMP, .L_byte
	 addu	DST, DST, LEN
	bb1	1, TMP, .L_half

	/* word copy, backwards */

	/* compute the number of words to copy */
	extu	UNIT, LEN, 0<2>
	bcnd	eq0, UNIT, .L_byte

	/* align to word boundary */
	bb0	0, SRC, .L_word_maybe_half
	subu	SRC, SRC, 1
	subu	DST, DST, 1
	subu	LEN, LEN, 1
	ld.b	TMP, SRC, 0
	st.b	TMP, DST, 0
.L_word_maybe_half:
	bb0	1, SRC, .L_word_copy
	subu	SRC, SRC, 2
	subu	DST, DST, 2
	subu	LEN, LEN, 2
	ld.h	TMP, SRC, 0
	st.h	TMP, DST, 0

.L_word_copy:
	/* worst case of 4 <= initial len < 7 and src and dst not aligned */
	/* in this case len may be < 4 at this point */
	extu	UNIT, LEN, 0<2>
	bcnd	eq0, UNIT, .L_byte

.L_word_loop:
	bcnd	eq0, UNIT, .L_word_done
	subu	SRC, SRC, 4
	subu	DST, DST, 4
	subu	UNIT, UNIT, 1
	ld	TMP, SRC, 0
	br.n	.L_word_loop
	 st	TMP, DST, 0
.L_word_done:
	bb0	1, LEN, .L_word_maybe_final_byte
	subu	SRC, SRC, 2
	subu	DST, DST, 2
	ld.h	TMP, SRC, 0
	st.h	TMP, DST, 0
.L_word_maybe_final_byte:
	bb0	0, LEN, .L_done
	subu	SRC, SRC, 1
	subu	DST, DST, 1
	ld.b	TMP, SRC, 0
	jmp.n	%r1
	 st.b	TMP, DST, 0
	
	/* halfword copy, backwards */

.L_half:
	/* compute the number of halfwords to copy */
	extu	UNIT, LEN, 0<1>
	bcnd	eq0, UNIT, .L_byte

	/* align to halfword boundary */
	bb0	0, SRC, .L_half_copy
	subu	SRC, SRC, 1
	subu	DST, DST, 1
	subu	LEN, LEN, 1
	ld.b	TMP, SRC, 0
	st.b	TMP, DST, 0

	/* worst case of initial len == 2 and src and dst not aligned */
	/* in this case len may be < 2 at this point */
	extu	UNIT, LEN, 0<1>
	bcnd	eq0, UNIT, .L_byte

.L_half_copy:
.L_half_loop:
	bcnd	eq0, UNIT, .L_half_done
	subu	SRC, SRC, 2
	subu	DST, DST, 2
	subu	UNIT, UNIT, 1
	ld.h	TMP, SRC, 0
	br.n	.L_half_loop
	 st.h	TMP, DST, 0
.L_half_done:
	bb0	0, LEN, .L_done
	subu	SRC, SRC, 1
	subu	DST, DST, 1
	ld.b	TMP, SRC, 0
	jmp.n	%r1
	 st.b	TMP, DST, 0

	/* byte copy, backwards */
#undef UNIT
.L_byte:
.L_byte_loop:
	bcnd	eq0, LEN, .L_done
	subu	SRC, SRC, 1
	subu	DST, DST, 1
	subu	LEN, LEN, 1
	ld.b	TMP, SRC, 0
	br.n	.L_byte_loop
	 st.b	TMP, DST, 0
.L_done:
	jmp	%r1

	END_STRONG(memmove)
