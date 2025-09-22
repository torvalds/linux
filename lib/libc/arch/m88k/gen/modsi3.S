/*	$OpenBSD: modsi3.S,v 1.3 2022/12/06 18:50:59 guenther Exp $	*/

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


#include "SYS.h"

/*
 * int __modsi3(int n, int d);
 */

#ifndef LDSO
.weak	__modsi3
#endif
ENTRY(__modsi3)
	/* if divider is zero, trap */
	bcnd	ne0, %r3, 1f
	tb0	0, %r0, 503
1:

	bb1	31, %r2, 1f
	bb1	31, %r3, 2f

	/* both operands are positive */
	divu	%r4, %r2, %r3
9:
	mul	%r5, %r4, %r3
	jmp.n	%r1
	 subu	%r2, %r2, %r5

1:
	bb1	31, %r3, 3f

	/* r2 is negative, r3 is positive */
	subu	%r4, %r0, %r2
	divu	%r5, %r4, %r3
	subu	%r4, %r0, %r5
	br	9b

2:
	/* r2 is positive, r3 is negative */
	subu	%r5, %r0, %r3
	divu	%r4, %r2, %r5
	subu	%r4, %r0, %r4
	br	9b

3:
	/* both operands are negative */
	subu	%r4, %r0, %r2
	subu	%r5, %r0, %r3
	divu	%r4, %r4, %r5
	br	9b
END(__modsi3)
