/*	$OpenBSD: pagezero.S,v 1.2 2021/05/12 01:20:52 jsg Exp $	*/

/*
 * Copyright (c) 2020 Brian Bamsch <bbamsch@google.com>
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
#include <machine/param.h>

#include "assym.h"

ENTRY(pagezero)
	RETGUARD_SETUP(pagezero, t6)
	li	a1, PAGE_SIZE
	add	a1, a0, a1

1:	sd	x0, 0(a0)
	sd	x0, 8(a0)
	sd	x0, 16(a0)
	sd	x0, 24(a0)
	sd	x0, 32(a0)
	sd	x0, 40(a0)
	sd	x0, 48(a0)
	sd	x0, 56(a0)
	addi	a0, a0, 64
	blt	a0, a1, 1b

	RETGUARD_CHECK(pagezero, t6)
	ret
END(pagezero)
