/*	$OpenBSD: _setjmp.S,v 1.3 2021/05/14 00:57:03 drahn Exp $	*/
/* 
 * Copyright (c) 2020 Dale Rahn <drahn@openbsd.org>
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


#include "DEFS.h"
#include <machine/setjmp.h>

ENTRY(_setjmp)
	RETGUARD_SETUP(_setjmp, t6)

	sd	sp, (1 * 8)(a0)
	/* Store the general purpose registers and ra */
	sd	s0, (2 * 8)(a0)
	sd	s1, (3 * 8)(a0)
	sd	s2, (4 * 8)(a0)
	sd	s3, (5 * 8)(a0)
	sd	s4, (6 * 8)(a0)
	sd	s5, (7 * 8)(a0)
	sd	s6, (8 * 8)(a0)
	sd	s7, (9 * 8)(a0)
	sd	s8, (10 * 8)(a0)
	sd	s9, (11 * 8)(a0)
	sd	s10, (12 * 8)(a0)
	sd	s11, (13 * 8)(a0)
	sd	ra, (14 * 8)(a0)

#ifndef _STANDALONE
	/* Store the fp registers */
	fsd	fs0, (15 * 8)(a0)
	fsd	fs1, (16 * 8)(a0)
	fsd	fs2, (17 * 8)(a0)
	fsd	fs3, (18 * 8)(a0)
	fsd	fs4, (19 * 8)(a0)
	fsd	fs5, (20 * 8)(a0)
	fsd	fs6, (21 * 8)(a0)
	fsd	fs7, (22 * 8)(a0)
	fsd	fs8, (23 * 8)(a0)
	fsd	fs9, (24 * 8)(a0)
	fsd	fs10, (25 * 8)(a0)
	fsd	fs11, (26 * 8)(a0)
	frcsr	t0	
	sd	t0, (27 * 8)(a0)
#endif

	/* Return value */
	mv	a0, x0
	RETGUARD_CHECK(_setjmp, t6)
	ret
END_STRONG(_setjmp)

ENTRY(_longjmp)
	RETGUARD_SYMBOL(_longjmp)
	RETGUARD_LOAD_RANDOM(_longjmp, t6)

	/* Restore the stack pointer */
	ld	t0, (1 * 8)(a0)
	mv	sp, t0

	/* Store the general purpose registers and ra */
	ld	s0, (2 * 8)(a0)
	ld	s1, (3 * 8)(a0)
	ld	s2, (4 * 8)(a0)
	ld	s3, (5 * 8)(a0)
	ld	s4, (6 * 8)(a0)
	ld	s5, (7 * 8)(a0)
	ld	s6, (8 * 8)(a0)
	ld	s7, (9 * 8)(a0)
	ld	s8, (10 * 8)(a0)
	ld	s9, (11 * 8)(a0)
	ld	s10, (12 * 8)(a0)
	ld	s11, (13 * 8)(a0)
	ld	ra, (14 * 8)(a0)

#ifndef _STANDALONE
	/* Store the fp registers */
	fld	fs0, (15 * 8)(a0)
	fld	fs1, (16 * 8)(a0)
	fld	fs2, (17 * 8)(a0)
	fld	fs3, (18 * 8)(a0)
	fld	fs4, (19 * 8)(a0)
	fld	fs5, (20 * 8)(a0)
	fld	fs6, (21 * 8)(a0)
	fld	fs7, (22 * 8)(a0)
	fld	fs8, (23 * 8)(a0)
	fld	fs9, (24 * 8)(a0)
	fld	fs10, (25 * 8)(a0)
	fld	fs11, (26 * 8)(a0)
	ld	t0, (27 * 8)(a0)
	fscsr	t0	
#endif

	/* Load the return value */
	li	a0, 1
	beqz	a1, 1f
	mv	a0, a1
1:
	RETGUARD_CHECK(_longjmp, t6)
	ret

END_STRONG(_longjmp)
