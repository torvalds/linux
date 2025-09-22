/*	$OpenBSD: brk.S,v 1.5 2023/12/10 16:45:52 deraadt Exp $	*/
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

	.extern __curbrk
	.extern _end

	.data
	.align 3
__minbrk:
	.dword _end
	

ENTRY_NB(brk)
	RETGUARD_SETUP(brk, t6)
	lla	t1, __minbrk
	ld	t5, 0(t1)
	bgeu	a0, t5, .L_brk_call
	mv	a0, t5
	
.L_brk_call:
	mv	a2, a0
	lla	t1, __curbrk

	li t0, SYS_break
99:	ecall
	PINSYSCALL(SYS_break, 99b)

	/* check for error */
	beqz	t0, .L_brk_ok

	sw	a0, TP_OFFSET_ERRNO(tp)
	li	a0, -1
	j	.L_done

	/* update, __curbrk and return */
.L_brk_ok:
	sd	a2, 0(t1)
	li	a0, 0		/* # return 0 */
.L_done:
	RETGUARD_CHECK(brk, t6);
	ret
END(brk)
	.weak	brk
