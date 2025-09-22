/*	$OpenBSD: sbrk.S,v 1.4 2022/12/03 15:02:30 jca Exp $	*/
/*
 * Copyright (c) 2021 Dale Rahn <drahn@openbsd.org>
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

	.extern _end

	.data
	.align	3
	.globl	__curbrk
	.hidden __curbrk
	.type	__curbrk,@object
__curbrk:
	.dword _end
	END(__curbrk)
	
ENTRY_NB(sbrk)
	RETGUARD_SETUP(sbrk, t6)

	lla	t1, __curbrk
	ld	a2, 0(t1)
	add	a0, a0, a2	// compute current break
	mv	t3, a0
	SYSTRAP(break)
	beqz	t0, .L_sbrk_ok
	sw      a0, TP_OFFSET_ERRNO(tp)
	li	a0, -1
	j	.L_sbrk_fail
.L_sbrk_ok:
	sd	t3, 0(t1)	// save current val of curbrk from above
	mv	a0, a2		// return old val of curbrk from above
.L_sbrk_fail:
	RETGUARD_CHECK(sbrk, t6);
	ret
END(sbrk)
	.weak	sbrk
