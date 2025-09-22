/*	$OpenBSD: divsi3.S,v 1.1 2013/06/07 17:31:31 miod Exp $	*/

/*
 * Copyright (c) 2013 Miodrag Vallat.
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


#include <machine/asm.h>

/*
 * int __divsi3(int n, int d);
 */

ENTRY(__divsi3)
	/* if divider is zero, trap */
	bcnd	ne0, %r3, 1f
	tb0	0, %r0, 503
1:

	bb1	31, %r2, 1f
	bb1	31, %r3, 2f

	/* both operands are positive */
	jmp.n	%r1
	 divu	%r2, %r2, %r3

1:
	bb1	31, %r3, 3f

	/* r2 is negative, r3 is positive */
	subu	%r4, %r0, %r2
	divu	%r5, %r4, %r3
	jmp.n	%r1
	 subu	%r2, %r0, %r5

2:
	/* r2 is positive, r3 is negative */
	subu	%r5, %r0, %r3
	divu	%r4, %r2, %r5
	jmp.n	%r1
	 subu	%r2, %r0, %r4

3:
	/* both operands are negative */
	subu	%r4, %r0, %r2
	subu	%r5, %r0, %r3
	jmp.n	%r1
	 divu	%r2, %r4, %r5
END(__divsi3)
